## Why

`operation-stats-ledger` built a measured op-stats substrate in the daemon — `DaemonOpStats`
(per-op counts + total latency, a 256-op rolling window, query zero-hit rate), surfaced live
through the `status` op (`daemon_ops.cpp:961`, `op_stats_json` in `operation_stats.cpp:121`). But it
explicitly listed **"No cross-restart stats persistence"** as a non-goal: the counters are
since-boot and in-memory only. graphd idle-shuts-down between bursts (`daemon_server.cpp:541`), so
every restart discards the window and nothing aggregates across lifetimes.

Net effect: a full day of real query usage across several project daemons leaves **zero durable
trace**. Build metrics persist to a `cgraph-out/stats.json` sidecar; op-stats do not persist at all.
The operator cannot answer the question the live stats were built to answer, *over time*: **"how
useful has cgraph actually been for me — how many queries, how many came back empty?"**

This change lifts that non-goal. It persists each daemon lifetime's op-stats to an append-only
ledger and adds an offline rollup, **without** changing the live, monotonic since-boot path.

## What Changes

- **Flush-on-shutdown (daemon).** When graphd leaves its serve loop — idle timeout, `shutdown` op,
  or clean termination — it appends **one JSON line** describing the lifetime's op-stats to
  `cgraph-out/op-stats-ledger.jsonl`, beside the existing on-exit graph persist
  (`daemon_server.cpp:564-573`). Append-only JSONL, never a rewrite, so it is cheap and crash-safe
  (a torn trailing line is parse-skipped on read). The flush is **best-effort**: wrapped so it can
  never block or fail shutdown, and is **gated on ≥1 substantive op** (query/path/explain/impact/
  context) so idle status-only spawns do not spam the ledger.
- **Wall-clock only at the boundary.** The line carries wall-clock `boot` and `shutdown`
  timestamps. The single `system_clock` read happens **once, at flush**; `boot` is derived as
  `shutdown − (steady_now − start_time)`. The live daemon's timing substrate stays **purely
  monotonic** — no `system_clock` enters Layer A.
- **Versioned latency histogram.** Lifetime totals only yield an exact mean (no lifetime p50). To
  derive an honest *merged* percentile across lifetimes, each line records a per-op latency
  histogram over a **pinned, log-spaced bucket layout** carried under `schema_version`. Changing
  the buckets is a versioned, migration-gated decision; the rollup merges only same-version
  histograms and reports when versions are mixed. No percentile is fabricated.
- **Cross-session rollup + surface.** A new `cgraph stats [--root PATH] [--since today|<ISO>|<dur>]`
  subcommand reads the ledger, filters by window (default: today), and prints an aggregate:
  **headline = per-op query counts and overall zero-hit rate** (the "is it useful" signal), with
  latency percentiles secondary. It optionally also queries the live daemon `status` and prints
  that as a clearly **LIVE (this daemon, since boot)** section above the **DURABLE (ledger, since
  X)** section, so the two are never confused.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: graphd persists each lifetime's op-stats to an append-only
  `op-stats-ledger.jsonl` on shutdown (best-effort, gated on substantive ops, wall-clock derived
  from the monotonic substrate); a new `cgraph stats --since` subcommand rolls the ledger up across
  lifetimes — summed per-op counts, overall query zero-hit rate, weighted-mean and merged-histogram
  percentile latency over a time window — and labels live vs durable.

## Non-Goals

- **No change to the live since-boot path.** `DaemonOpStats`, `op_stats_json`, and the `status`
  payload are untouched; the ledger is strictly additive.
- **No `system_clock` in Layer A.** The substrate stays monotonic; wall-clock is read once at flush
  only, never in `ScopedTimer` or per-op recording.
- **No central/cross-service aggregation store.** Each daemon writes its own per-service ledger
  beside that service's `stats.json`/`graph.json`; rolling up across services is the reader's job,
  not a shared database.
- **No exact lifetime p50.** Cross-lifetime percentiles are an explicit histogram approximation,
  labeled as such; only summed counts and weighted-mean latency are exact.
- **No new MCP surface.** The ledger is an offline CLI read; `graph_status` continues to pass the
  live `status` payload through unchanged.
- **No blocking or fail-on-flush.** A failed ledger append logs and is dropped; it never delays or
  aborts shutdown, and never loses the graph-persist that shares the teardown.

## Impact

- `src/engine/operation_stats.{hpp,cpp}`: a pinned versioned histogram type + bucket constant;
  `op_stats_ledger_line(stats, boot_wall, shutdown_wall) -> json` (Layer B, pure); and
  `aggregate_op_stats_ledger(lines, since) -> json` (Layer B rollup, pure — window filter, summed
  counts, weighted mean, merged-histogram percentiles, zero-hit rate).
- `src/engine/daemon_server.cpp`: capture a `start_time`-anchored boot reference; in teardown
  append one ledger line to `cgraph-out/op-stats-ledger.jsonl`, best-effort and gated.
- `src/cli/main.cpp`: new `stats` subcommand (`--root`, `--since`) that reads + aggregates the
  ledger and prints labeled live + durable sections.
- New durable artifact `cgraph-out/op-stats-ledger.jsonl` (gitignored telemetry, per service).
  `graph.json`/`stats.json`/the live `status` payload are unchanged.
- Tests: `operation_stats_test.cpp` extended — ledger-line shape, histogram bucketing, and the
  rollup math (window boundaries, weighted mean, histogram merge + approximate percentile, zero-hit
  rate, mixed schema_version handling). A best-effort flush round-trip against a temp dir.
