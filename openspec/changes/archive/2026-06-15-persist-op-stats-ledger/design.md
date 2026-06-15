## Context

The live op-stats are correct but ephemeral. `DaemonOpStats` (`operation_stats.hpp:84`) holds
per-op `count` / `total_ms`, a `query_zero_hits` counter, and a 256-cap `RollingWindow` of recent
latencies; it is recorded at the dispatch boundary (`daemon_ops.cpp:1095`) and read by `status`
(`daemon_ops.cpp:961`). graphd is one-per-root (`daemon_identity_for`, `daemon_server.cpp:132`) and
idle-shuts-down (`daemon_server.cpp:541`), so the in-memory totals die with the process. This change
persists them at the existing teardown seam and adds an offline rollup. It does not touch the live
path.

## Architecture

### The flush seam, not a new lifecycle

The daemon already has an on-exit hook: after the serve loop breaks (idle / `shutdown` op / socket
error), `daemon_server.cpp:564-573` joins the build thread and flushes the dirty graph. The
op-stats flush is one more best-effort step in that same teardown block, before the socket close.
Putting it there means it fires for every exit path the graph-persist already covers, and shares
the wind-down — no new shutdown trigger, no signal handler added.

### Monotonic substrate, wall-clock only at the boundary

Layer A must stay monotonic (the `ScopedTimer` contract: "the wall clock is never read"). So the
ledger reads `system_clock::now()` **exactly once**, at flush, as `shutdown_wall`. The boot
timestamp is *derived*, not measured live:

```
shutdown_wall = system_clock::now()                       // single wall read, at flush
elapsed       = StatsClock::now() - state.start_time      // monotonic, already tracked for uptime
boot_wall     = shutdown_wall - elapsed                   // derived
```

The running daemon therefore never reads a wall clock; only the flush does, and only once. Both
timestamps serialize as ISO-8601 UTC.

### Flush gate

Write a line only when `count[query]+count[path]+count[explain]+count[impact]+count[context] >= 1`.
A daemon that only answered `status`/`shutdown` (e.g. a probe spawn) writes nothing, so the ledger
records *work*, not wake-ups.

### Append-only, crash-safe

One `chunk`-free JSONL line per lifetime, opened `std::ios::app`, single `write` + newline. Never a
read-modify-rewrite. A crash mid-write can only corrupt the final line; the reader parses each line
with the tolerant path (`json::parse(line, nullptr, false)`, discard on failure — the same pattern
the parity reader uses) and skips a torn trailing line. Per-service single-writer (one daemon per
root) means no inter-process append interleaving on a given ledger.

### Pinned histogram (versioned, migration-gated)

Lifetime totals give an exact mean but no p50. For an honest *merged* percentile across lifetimes,
each op carries a fixed-layout latency histogram. The layout is pinned in the schema now:

```
LEDGER_SCHEMA_VERSION = 1
HIST_BUCKET_UPPER_MS  = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]   // log2-spaced, ms
// 12 counts per op: 11 bounded buckets [0,1),[1,2),...,[512,1024) + 1 overflow [1024, inf)
```

A bucket index is `floor(log2(max(latency,epsilon)))` clamped to `[0, 11]`. Changing the layout
bumps `LEDGER_SCHEMA_VERSION`; the rollup groups lines by version and merges only same-version
histograms, reporting a note when a window mixes versions rather than silently summing
incomparable buckets. Percentiles are read off the merged histogram by linear interpolation within
the containing bucket and labeled approximate — never presented as exact.

### Data shapes (illustrative)

Ledger line (`op-stats-ledger.jsonl`, one per lifetime):

```json
{
  "schema_version": 1,
  "boot": "2026-06-15T09:36:00Z",
  "shutdown": "2026-06-15T11:20:12Z",
  "uptime_seconds": 6252.0,
  "query_zero_hits": 7,
  "ops": {
    "query":   {"count": 42, "total_ms": 380.5, "hist_ms": [0,3,9,18,8,3,1,0,0,0,0,0]},
    "path":    {"count": 3,  "total_ms": 22.1,  "hist_ms": [0,0,1,1,1,0,0,0,0,0,0,0]},
    "explain": {"count": 5,  "total_ms": 14.0,  "hist_ms": [1,2,2,0,0,0,0,0,0,0,0,0]},
    "impact":  {"count": 0,  "total_ms": 0.0,   "hist_ms": [0,0,0,0,0,0,0,0,0,0,0,0]},
    "context": {"count": 9,  "total_ms": 540.0, "hist_ms": [0,0,0,1,2,3,2,1,0,0,0,0]}
  }
}
```

Rollup (`cgraph stats --since today`), headline-first:

```
DURABLE (ledger: backend, since 2026-06-15T00:00 — 3 daemon lifetimes)
  query     54   zero-hit 15%   p50 6ms   p90 41ms      ◄ headline: counts + zero-hit
  context   21                  p50 58ms  p90 210ms
  explain   12   ...            p50 3ms
  path       4 ; impact 0
LIVE (this daemon, since boot 00:04:11)
  query      2   zero-hit 0%    p50 5ms
```

### Rollup derivation (Layer B, pure)

`aggregate_op_stats_ledger(lines, since)`: filter lines whose `shutdown >= since` (a partly-in-window
lifetime is included whole — the substrate has no intra-lifetime wall breakdown, and splitting would
fabricate one); sum per-op `count` and `total_ms` (→ exact weighted mean); sum same-version
histograms (→ approximate p50/p90); overall query zero-hit rate = `Σ query_zero_hits / Σ
count[query]`. No I/O, no clock — same discipline as the existing Layer B functions.

## Test strategy

Per the project's TDD rule, every behavior-bearing task is preceded by a failing test. The pure
derivations are the high-value targets and are fully unit-testable in `operation_stats_test.cpp`:

- **Ledger line**: a `DaemonOpStats` with known counts/latencies + a fixed `(boot, shutdown)` →
  expected JSON (counts, total_ms, correct histogram buckets for known latencies, derived boot).
- **Histogram bucketing**: latencies on bucket boundaries (1, 2, 1024 ms) land in the documented
  bucket; an overflow latency lands in the last bucket.
- **Rollup math**: N lines → summed counts; weighted mean == `Σtotal / Σcount`; merged-histogram p50
  matches a hand-computed value; zero-hit rate exact; a line outside the `--since` window is
  excluded (boundary inclusive/exclusive pinned by a scenario); mixed `schema_version` lines produce
  the documented note rather than a bad merge.
- **Flush round-trip**: `op_stats_ledger_line` written to a temp dir via the append path, read back,
  re-aggregated — and the gate: a stats-only lifetime writes no line.

### Cannot be tested directly

The end-to-end "real idle-timeout fires the flush in the live serve loop" is not unit-tested (it
needs a running daemon and a 5-minute idle). Validation is by construction: the flush is the same
teardown block as the already-tested graph persist, the line-builder and aggregator are unit-tested
in isolation, and the round-trip test exercises the write+read+aggregate path the daemon uses.

## Open questions

- **Ledger growth / rotation.** One line per lifetime is tiny, but an aggressive idle-timeout could
  produce many lines/day. Out of scope here (a `--since` read already bounds the work); a future
  change could add size-based rotation or a compaction op. Noted, not built.
- **Cross-service rollup.** `cgraph stats` reads one service's ledger (via `--root`). A "roll up all
  my projects" view would need a registry of known roots — deferred.
