## Why

The engine already knows how to keep a graph fresh as code changes: while a daemon is resident it
polls the project tree every `code_poll_interval` (2s) and folds edits into the graph
incrementally (`daemon_server.cpp:614-653`), bumping `last_activity` so "active editing keeps the
daemon alive" (`daemon_server.cpp:650`). None of that ran for our three working repos. Their graphs
sat frozen at the Jun 23 one-shot build through two weeks of editing (frontend 120 dirty files,
backend 8 + 80 commits ahead, turing-agents 21 dirty) until a manual `cgraph --root … --out …`
rebuild. The refresh machinery was never the problem — **nothing kept a daemon present to run it.**

The root cause is that daemon presence is coupled to *query* activity, not *edit* activity. A
daemon spawns only when the MCP server or `cgraph-client` issues a request (`client_runtime`
connect-first, spawn-if-absent), and it idle-shuts-down after `idle_timeout{300}` (5 min) of no
requests and no edits (`daemon_server.cpp:668`). Edit a repo you are not actively querying and the
edits pile up unindexed:

```
   query arrives ──► daemon spawns ──► watches 2s, folds edits in, stays alive ──┐
        ▲                                                                         │
        └──────────────── 5 min no query AND no edit ── idle_timeout ────────────┘
   edit while resident → folded in  ✓        edit while no daemon → lost until next query  ✗
```

We want the opposite coupling: a daemon **present whenever the codebase is being edited**, so
"always fresh" and "auto-update as the code changes" fall out of code that already exists.

## What Changes

- **New `resident-daemon-supervisor` capability.** A supervisor, started once at login by a single
  macOS LaunchAgent (`KeepAlive`), keeps one **immortal** `graphd` resident per tracked repo. It
  **auto-discovers** tracked repos (any directory whose `.mcp.json` registers a `cgraph` MCP
  server), reconciles the running set as repos are cloned or removed, and restarts a daemon that
  dies. Managed through a new `cgraph daemon <install|sync|status|uninstall>` subcommand family.
- **Engine fix — expressible "never idle" (`graph-daemon-client`).** `--idle-timeout 0` currently
  insta-kills the daemon (`now - last_activity >= 0` is always true, `daemon_server.cpp:668`). Make
  `0` (or negative) mean *never idle-shutdown*, so a supervised daemon stays resident indefinitely.
  Untracked repos keep the default 300s spawn-on-query / die-when-idle behavior.
- **Engine fix — safe single-owner bind (`graph-daemon-client`).** `open_listen_socket`
  unconditionally `::unlink`s the endpoint before binding (`daemon_server.cpp:141`). For an
  always-on model that means a second `graphd` for the same root *steals* the live endpoint instead
  of deferring. Add a connect-probe: if a healthy daemon already answers on the socket, the new
  instance logs "already serving root X" and exits cleanly; only a stale (unanswered) socket is
  unlinked and rebound. Guarantees exactly one owner per root under any start race.

## Capabilities

### New Capabilities

- `resident-daemon-supervisor`: continuously keep every tracked repo's graph fresh via an
  auto-started, auto-discovered, self-healing set of immortal resident daemons — no manual refresh.

### Modified Capabilities

- `graph-daemon-client`: a daemon can be configured to never idle-shut-down (`--idle-timeout 0`),
  and daemon startup defers to an already-running daemon for the same root instead of unlinking its
  live endpoint.

## Non-Goals

- **No Linux/systemd or Windows integration in this change.** macOS LaunchAgent first. The portable
  discovery + reconcile logic is factored so a systemd-user backend can follow, but it is not built
  here.
- **No change to the incremental-update or dedup algorithm.** The existing 2s watcher, neighborhood
  dedup, and every-5th-update full-dedup reconcile (`daemon_server.cpp:633`) are reused unchanged;
  they already bound incremental drift.
- **No idle eviction / memory cap for tracked repos.** "Immortal resident" is the chosen behavior;
  each tracked repo costs one resident process plus its graph in RAM. A future change may add
  idle-eviction with respawn-on-edit if the tracked set grows large.
- **No new query semantics or MCP tool.** Queries still hit the resident daemon through the existing
  socket; the MCP `graph_*` tools are unchanged and simply connect to an always-present daemon.
- **No auto-commit or git interaction.** The supervisor indexes the working tree as it is; it never
  pulls, commits, or mutates any tracked repo.

## Impact

- New engine sources: `src/engine/daemon_supervisor.{hpp,cpp}` (discovery + reconcile diff, portable),
  `src/engine/launch_agent.{hpp,cpp}` (plist rendering + `launchctl` glue, macOS-guarded); registered
  in `src/engine/CMakeLists.txt`.
- `src/cli/main.cpp`: new `cgraph daemon <install|sync|status|uninstall>` subcommands delegating to
  the supervisor.
- `src/daemon/main.cpp` + `src/engine/daemon_server.cpp` + `daemon_lifecycle.{hpp,cpp}`: never-idle
  semantics for `idle_timeout <= 0`; connect-probe before unlink in `open_listen_socket`.
- New artifacts at runtime: one supervisor LaunchAgent (`com.cgraph.supervisor`) and one per-repo
  LaunchAgent (`com.cgraph.graphd.<root-hash>`) under `~/Library/LaunchAgents`.
- Tests: `daemon_supervisor_test.cpp` (discovery, reconcile diff, plist render), plus additions to
  `daemon_lifecycle_test` / `daemon_server_test` (never-idle) and `daemon_hardening_test` /
  `daemon_endpoint_test` (single-owner bind).
