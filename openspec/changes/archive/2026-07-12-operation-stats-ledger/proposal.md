## Why

cgraph already reaches for a stats surface and stops halfway. The `status` daemon op
(`daemon_ops.cpp:791-823`) emits `uptime_seconds`, `cache_hit_rate`, and `enrichment_running`
as real numbers, but all three are **declared, serialized, and permanently zero** —
`uptime_seconds` is never set, `cache_hit_rate` (`types.hpp:75`) is never computed, and
`EnrichmentState::Running` is never assigned. Meanwhile nothing in the pipeline records how long
any phase takes, the one-shot CLI prints only `processed N files`, and there is no way to answer
the operator's actual question: **is this tool doing useful work, or spinning?**

That question has two layers. You cannot trust a value claim ("the cache saved time") without a
measurement substrate underneath it. So this change builds both: a measured substrate of per-phase
timings and counters (Layer A), and a derived value ledger on top (Layer B) that interprets those
numbers — never inventing its own.

## What Changes

- **Layer A — substrate, measured at the orchestration seams.** Add scoped phase timers and
  counters at `run_one_shot` (extract / merge / resolve / dedup / communities / analyze),
  `handle_daemon_request` (per-op latency + counts), and the incremental-apply path
  (`files_reextracted` vs `files_cache_hit`, phase timings). Instrumentation lives in the ~3
  orchestrator functions, not threaded into leaf functions.
- **Layer B — ledger, pure derivation, no I/O.** From Layer A numbers derive: a **modeled** cache
  saving (`files_cache_hit × mean(extract_ms)`) for build/rescan, and **revealed** query stats
  (count, p50/latency, zero-hit rate) for the daemon. No counterfactual is invented — there is no
  "beat grep by Ns" claim, because it cannot be proven.
- **One-shot output.** Write a sidecar `cgraph-out/stats.json` (durable, diffable) plus a one-line
  human-readable stderr summary. `graph.json` is left byte-identical — the Graphify parity golden
  is untouched.
- **Daemon output.** Surface stats live through the existing `status` op: since-boot lifetime
  totals **and** a rolling recent window (last N ops), so status answers both "total work done"
  and "useful right now." Daemon stats are since-boot, not persisted across restarts (consistent
  with `uptime_seconds` being since-boot).
- **Dead-field cleanup (required rider).** Populate `uptime_seconds` (daemon start delta), compute
  `cache_hit_rate` from Layer A, and actually set `EnrichmentState::Running` on the enrichment path.
  No stats field may remain a permanent zero.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `deterministic-graph-pipeline`: a one-shot build records per-phase timings and node/edge/file
  counters, writes them to a sidecar `stats.json` and a one-line stderr summary, and derives a
  modeled cache-saving figure — without altering `graph.json`.
- `graph-daemon-client`: the `status` op reports real per-op counts and latencies, a modeled cache
  saving, lifetime totals plus a rolling recent window, and previously-dead fields
  (`uptime_seconds`, `cache_hit_rate`, `enrichment_running`) now carry true values.

## Non-Goals

- **No "vs grep" or "vs cold rebuild" measured counterfactual.** The savings number is explicitly
  modeled from real per-file timings, never measured by running the slow path, and never hardcoded.
  A modeled estimate is labeled as such.
- **No structured logging framework.** This change does not introduce spdlog/log levels/timestamps;
  it keeps the existing `std::cerr` summary line and adds the sidecar + status JSON.
- **No cross-restart stats persistence.** Daemon counters reset on restart, matching uptime.
- **No new MCP surface.** `graph_status` already passes the daemon `status` payload through; the
  richer fields appear there for free. No new MCP tool.
- **No embedding stats in `graph.json`.** Rejected to protect the byte-identical parity golden.

## Impact

- `src/engine/pipeline.cpp` (`run_one_shot` timers), `src/engine/daemon_ops.cpp`
  (`handle_daemon_request` counters, `status`), `src/engine/daemon_server.cpp` (uptime,
  `EnrichmentState::Running`), `src/engine/incremental_update.cpp` (cache-hit counters),
  `src/engine/include/cgraph/types.hpp` + `pipeline.hpp` + `daemon_ops.hpp` (stat fields),
  a new `src/engine/operation_stats.{hpp,cpp}` for Layer B derivation + `stats.json` writer.
- New sidecar artifact `cgraph-out/stats.json`. `graph.json` output unchanged.
- Tests: `operation_stats_test.cpp` (derivation math, zero-hit edge cases), plus assertions in
  `pipeline_test` / `daemon_ops_test` / `incremental_*_test` that the new fields are non-zero on
  real work and that the three previously-dead fields now carry true values.
