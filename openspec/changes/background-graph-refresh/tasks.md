## 1. Engine fix — never-idle daemon mode

- [x] 1.1 `daemon_lifecycle_test` (red): `should_shutdown_for_idle` returns `false` for any elapsed
      time when `idle_timeout <= 0`, and keeps returning `true` past a positive timeout.
- [x] 1.2 `daemon_server_test` (red): a served daemon started with `idle_timeout = 0`, advanced well
      past the default 300s of inactivity, is still listening and answers `status`; a daemon with a
      positive timeout still shuts down.
- [x] 1.3 Implement: treat `idle_timeout <= 0` as "never idle" in `should_shutdown_for_idle`
      (`daemon_lifecycle.cpp:33`) and in the serve-loop idle checks (`daemon_server.cpp:668`,
      and the static-seam loop at `:217`). `--idle-timeout 0` reaches this path from
      `src/daemon/main.cpp` (already parses `--idle-timeout SECONDS` into `idle_timeout`).

## 2. Engine fix — single-owner endpoint bind

- [x] 2.1 Single-owner bind coverage lives in `daemon_server_test` (has the real server-spin
      harness): with a live daemon on a root's socket, a second `run_daemon_server` for the same
      root returns `0` (defer) without unlinking, and a `query`/`status` is still answered by the
      original; with only a stale socket file (no listener), startup unlinks and binds successfully.
- [x] 2.2 Implemented `endpoint_has_live_daemon` connect-probe + `kEndpointAlreadyServed` sentinel
      in `open_listen_socket` (`daemon_server.cpp`): before `::unlink`, `::connect()` to the socket;
      on success (live listener) log `already serving <root>` and return the sentinel so both
      `run_daemon_server` and `run_static_seam_server` exit `0`; on failure (stale/absent) unlink and
      bind as before.

## 3. Supervisor — discovery + reconcile (portable, pure)

- [x] 3.1 Register `daemon_supervisor.{hpp,cpp}` in `src/engine/CMakeLists.txt`; add
      `daemon_supervisor_test.cpp` + `add_test` in `tests/smoke/CMakeLists.txt`.
- [x] 3.2 `daemon_supervisor_test` (red): `discover_tracked_repos` over a temp fixture tree (cgraph
      `.mcp.json`, non-cgraph `.mcp.json`, none) returns exactly the cgraph set, and each result's
      key equals the `daemon_identity` canonical-root hash.
- [x] 3.3 Implement `discover_tracked_repos(search_roots)` — shallow scan, parse `.mcp.json`, select
      `mcpServers.cgraph`, key by canonical-root hash (reuse `daemon_identity`).
- [x] 3.4 `daemon_supervisor_test` (red): `reconcile(discovered, installed)` yields correct
      `to_add`/`to_remove`; identical inputs yield empty diffs (idempotent).
- [x] 3.5 Implement `reconcile(...)` as a pure set-difference keyed by root hash.

## 4. Supervisor — LaunchAgent rendering + launchctl glue (macOS seam)

- [x] 4.1 `daemon_supervisor_test` (red): `render_launch_agent` emits the expected
      `ProgramArguments` (per-repo: `graphd --root <repo> --idle-timeout 0`; supervisor:
      `cgraph daemon sync`), `KeepAlive`, `RunAtLoad`, `Label` (`com.cgraph.graphd.<hash>` /
      `com.cgraph.supervisor`), and supervisor `StartInterval`; output parses as a valid plist.
- [x] 4.2 Implement `render_launch_agent(...)` (pure string) and a thin macOS-guarded
      `launchctl bootstrap`/`bootout` wrapper in `launch_agent.{hpp,cpp}`. `sync` SHALL return a
      structured plan (applied `to_add`/`to_remove`) for assertion; the `launchctl` call is the
      untestable seam (validated in Step 6).

## 5. CLI — `cgraph daemon` subcommands

- [x] 5.1 `daemon_supervisor_test` (red): `install` then `uninstall` (against a temp
      LaunchAgents dir) leaves no managed plist; `status` reports per-repo liveness from a set of
      real/absent sockets.
- [x] 5.2 Wire `cgraph daemon <install|sync|status|uninstall>` in `src/cli/main.cpp` delegating to
      the supervisor; `install` reconciles once and registers the supervisor LaunchAgent, `uninstall`
      is its exact inverse, `status` probes each tracked root's socket for liveness. Allow overriding
      the LaunchAgents dir + search roots for tests.

## 6. Verify

- [x] 6.1 Full suite `ctest --preset default` — 61/61 passed.
- [x] 6.2a End-to-end on a throwaway temp repo + temp LaunchAgents dir (real launchd, real query):
      `daemon status` -> `[stopped]`; `daemon sync` bootstraps a real resident `graphd` and writes
      `com.cgraph.graphd.<hash>.plist`; `daemon status` -> `[live]`; `cgraph-client query` returns the
      seeded symbol from the resident daemon; `daemon uninstall` boots it out and removes the plist
      (0 left); `daemon status` -> `[stopped]`, no stray graphd, no `com.cgraph.*` in user launchd.
- [ ] 6.2b Real install on frontend/backend/turing-agents (`cgraph daemon install` with the three
      repos' parent as a search root) — persistent login LaunchAgents on this machine. Deferred to a
      user-confirmed step; the mechanism is proven by 6.2a.
