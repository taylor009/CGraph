## 1. Seam marker

- [x] 1.1 `seam fuse` writes a `.cgraph-seam` marker into `--out` (shared `kSeamMarkerFile`
      constant). Verified live: the marker appears in the fuse output dir.
- [x] 1.2 `is_seam_directory(root)` helper (`seam.hpp`/`.cpp`): true iff `root/.cgraph-seam` exists.
      `seam_test.cpp`: false before the marker, true after.

## 2. Static seam serve loop (daemon)

- [x] 2.1 Factored `open_listen_socket(socket_path) -> int` out of `run_daemon_server` (anon ns);
      both servers use it. Behavior-preserving — daemon/hardening tests stay green.
- [x] 2.2 Added `run_static_seam_server(root, options)`: `open_listen_socket` +
      `load_graph_snapshot(root/"graph.json")` + select/accept/`read_frame`/`handle_daemon_request`/
      `write_frame` loop with idle-timeout + `shutdown`; `update_handler` reloads `graph.json`; no
      build/watch/persist/enrichment; endpoint cleaned up on exit.
- [x] 2.3 `src/daemon/main.cpp`: branches to `run_static_seam_server` when `is_seam_directory(root)`.

## 3. Verify

- [x] 3.1 Daemon + hardening tests green after the `open_listen_socket` extraction (11/11).
- [x] 3.2 Full suite `ctest --preset default` → 60/60; parity unchanged.
- [x] 3.3 Live smoke: `seam fuse` wrote the marker; `graphd --root <seamdir>` logged "serving static
      seam graph (6 nodes)"; `cgraph-client --root <seamdir>` got `status` ready/6-nodes, `explain
      endpoint` → 4 cross-service neighbors (resident: 4 calls, one daemon), `remember` rejected
      ("session memory is not enabled"), clean `shutdown`. (`update`→reload implemented via
      `update_handler`; the smoke exercised the resident query/reject/shutdown path.)