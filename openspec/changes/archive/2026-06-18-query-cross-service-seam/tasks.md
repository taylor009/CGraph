## 1. Queryable-seam integration test

- [x] 1.1 `seam_test.cpp`: publishes the fused snapshot on a `DaemonState` and asserts
      `handle_daemon_request` returns cross-service results — `impact` on the schema reaches the
      endpoint; `path` backend::scoreModel → endpoint resolves (≥2 nodes); `explain` on the endpoint
      has ≥3 neighbors; a `remember` write op is rejected (`ok=false`).

## 2. CLI subcommand

- [x] 2.1 `cli/main.cpp`: added `seam query --graph FUSED.json <op> [PARAMS_JSON]` — gates to
      `query`/`path`/`explain`/`impact`/`context` (others rejected, exit 2), `load_graph_snapshot`,
      dispatch via `handle_daemon_request(make_request(op, params))`, print response, exit 1 on
      `ok=false`.
- [x] 2.2 Extended the `seam` dispatch to `<gen|fuse|query>` + usage line.

## 3. Verify

- [x] 3.1 Live smoke: `gen` → `fuse` → `seam query … impact` → dependents `[endpoint, service:backend]`
      (cross-service); `seam query … path` → `[backend::scoreModel, endpoint:ml-api:POST /v3/score]`;
      `seam query … remember` rejected ("a seam graph is read-only").
- [x] 3.2 `ctest --preset default -R cgraph_seam_test` → passed.
- [x] 3.3 Full suite `ctest --preset default` → 60/60; parity goldens unchanged.