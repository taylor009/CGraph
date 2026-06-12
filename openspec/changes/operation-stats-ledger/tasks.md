## 1. Layer A substrate — one-shot build timings

- [x] 1.1 Add `pipeline_test` assertions (red): after a one-shot build, `PipelineResult.stats`
      phase timings are all > 0 and `nodes`/`edges` equal the snapshot sizes.
- [x] 1.2 Add a `ScopedTimer` (RAII, `steady_clock`, writes elapsed ms on destruct) and a
      `BuildStats` struct on `PipelineResult` (`pipeline.hpp`). Wrap each phase call in
      `run_one_shot` (`pipeline.cpp`) with a scoped timer; record file/node/edge counts.
- [x] 1.3 Register `operation_stats.{hpp,cpp}` in `src/engine/CMakeLists.txt`.

## 2. Layer B derivation + stats.json + parity guard

- [x] 2.1 `operation_stats_test.cpp` (red): modeled saving == `hits × mean`; zero hits → no estimate
      key; zero queries → no divide-by-zero; p50 from a known latency multiset.
- [x] 2.2 Implement Layer B derivation in `operation_stats.cpp` (modeled cache saving, query
      summary) — pure functions over the structs, no I/O.
- [x] 2.3 Write `cgraph-out/stats.json` + one-line stderr summary from the CLI (`src/cli/main.cpp`).
- [x] 2.4 Confirm `graph.json` parity golden (`extractor_goldens_test` / node-link golden) still
      passes byte-identical with stats enabled.

## 3. Daemon op stats + rolling window

- [x] 3.1 `daemon_ops_test` (red): after N queries (some zero-hit), `status` reports
      `op_count[query]==N`, non-zero query latency, and a matching zero-hit rate; rolling window
      never exceeds capacity.
- [x] 3.2 Add `DaemonOpStats` (lifetime arrays + `RollingWindow` ring buffer) to `DaemonState`;
      accumulate per-op count/latency and query zero-hits at the `handle_daemon_request` dispatch
      boundary (`daemon_ops.cpp`).
- [x] 3.3 Extend `status()` to emit lifetime totals, the recent window, query zero-hit rate, and the
      modeled cache-saving estimate.

## 4. Dead-field cleanup (no permanent zeros)

- [x] 4.1 Test (red): `uptime_seconds` > 0 and increases; `cache_hit_rate` in (0,1] after a warm
      rescan; `enrichment_running` >= 1 and `enrichment_state == running` during an in-flight ingest.
- [x] 4.2 Set `state.started_at` at daemon start and compute `uptime_seconds` in `status`
      (`daemon_server.cpp` / `daemon_ops.cpp`).
- [x] 4.3 Compute `cache_hit_rate` from Layer A on each (re)build and store on the snapshot
      (`incremental_update.cpp`, `types.hpp`).
- [x] 4.4 Set `EnrichmentState::Running` around the active ingest in `run_enrichment_refresh`
      (`daemon_server.cpp:210-238`) and increment/clear `enrichment_running`.

## 5. Verify

- [x] 5.1 Full suite `ctest --preset default` (report pass/total).
- [x] 5.2 Manual: run one-shot on a fixture, inspect `stats.json` + stderr line; start daemon, issue
      mixed queries + a warm `update .`, inspect `status` for non-zero uptime, real cache_hit_rate,
      query counts/zero-hit rate, and the rolling window. Record before/after numbers in the PR.
