## 1. Not-ready accounting

- [x] 1.1 `operation_stats_test.cpp`: a `record` with `not_ready=true` bumps `not_ready` and leaves
      `count`, `query_zero_hits`, latency, and the recent window untouched; with `not_ready=false`
      the existing behavior is unchanged.
- [x] 1.2 `operation_stats.hpp`: add `std::size_t not_ready` to `DaemonOpStats`; extend `record` with
      a `not_ready` flag that, when set, increments `not_ready` and returns before any other
      accounting.
- [x] 1.3 `daemon_ops.cpp`: pass `graph->build_state == BuildState::Empty` as `not_ready` to
      `record`; add `not_ready` to the `status` `ops` block.

## 2. Query route distribution

- [x] 2.1 `operation_stats.hpp`: add `query_route_entity` / `query_route_structural` /
      `query_route_search` counters; the dispatch buckets the query op's `route` (`entity` → entity,
      `search`/absent → search, otherwise → structural) and records it.
- [x] 2.2 `daemon_ops.cpp` + `operation_stats.cpp`: surface the route counts in `status` and persist
      them in the ledger line + cross-session rollup (beside `adaptive_context`).
- [x] 2.3 `operation_stats_test.cpp`: recording query routes tallies the three buckets; the ledger
      round-trips them (write → aggregate).

## 3. stats default window

- [x] 3.1 `cli/main.cpp`: default `run_stats` `--since` to all-time (no lower bound); `--since` still
      narrows. Update the usage line.

## 4. Verify

- [x] 4.1 `ctest --preset default -R 'cgraph_operation_stats_test|cgraph_daemon_ops_test'` passes.
- [x] 4.2 Full suite `ctest --preset default` passes; parity unchanged.
- [x] 4.3 Live: fire queries at the turing frontend during the rebuild window AND after ready;
      confirm `status` shows the build-window ops under `not_ready` (not `query_zero_hits`), the
      ready-state `query_zero_hit_rate` reads ~0%, and the route counts are populated; `cgraph stats`
      (no `--since`) shows the full history.