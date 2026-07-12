## Context

The incremental-refresh engine is complete and correct; the gap is *presence*. Two forces have to
be reconciled: (1) a daemon must be resident whenever a tracked repo is being edited, and (2) there
must be exactly one owner per project root even though both the supervisor and the MCP client's
connect-first/spawn-if-absent path (`client_runtime`) can try to start one. The design leans on
macOS `launchd` for the things it does well — start-at-login, `KeepAlive` restart-on-crash — and
keeps the portable, testable logic (discovery, reconcile diff, plist rendering) in the engine.

Hard constraints: the change must not touch the `graph.json` parity golden (it doesn't — no
pipeline change), must not alter untracked-repo behavior (default 300s idle stays), and every
behavior must be expressed as an observable scenario per `openspec/config.yaml`.

## Architecture

```
  login / boot
      │  RunAtLoad
      ▼
  com.cgraph.supervisor  (LaunchAgent, KeepAlive)
      │  runs: cgraph daemon sync   (at load + on StartInterval, e.g. 300s)
      ▼
  discover_tracked_repos(search_roots)         reconcile_launch_agents(discovered, installed)
      scan for dirs whose .mcp.json         →     to_add   → render plist + launchctl bootstrap
      registers an mcpServers.cgraph              to_remove → launchctl bootout + rm plist
      │                                           unchanged→ leave running
      ▼
  per-repo LaunchAgent  com.cgraph.graphd.<root-hash>   (KeepAlive, RunAtLoad)
      └─► graphd --root <repo> --idle-timeout 0     ← immortal (never idle-shuts-down)
              │ watches tree every 2s, folds edits incrementally, persists graph.json every 30s
              ▼
          MCP / cgraph-client connect to the LIVE socket → queries always fresh
```

Two independent layers, matching the capability split:

- **`graph-daemon-client` (engine primitives).** Make a single daemon capable of being immortal and
  safe to co-exist with a racing start. Small, surgical, unit-testable.
- **`resident-daemon-supervisor` (orchestration).** Discover, reconcile, and supervise N daemons via
  launchd. Portable logic in the engine; the `launchctl`/plist glue is a thin platform seam.

### Why supervisor-generates-LaunchAgents, not supervisor-owns-processes

Two sub-designs were considered:

| | Supervisor owns daemons (spawn/monitor/restart itself) | Supervisor reconciles per-repo LaunchAgents (chosen) |
|---|---|---|
| Restart-on-crash | our code (backoff loop to get right) | `launchd` `KeepAlive` (battle-tested) |
| Start-at-login | supervisor must be the one always-on proc | each daemon `RunAtLoad`; supervisor only reconciles |
| Supervisor crash | takes all daemons with it | daemons keep running independently |
| Code we own | process lifecycle mgmt | a pure discovery + diff function |

The chosen design makes the supervisor a **stateless reconciler**: it computes the set difference
between discovered repos and installed per-repo LaunchAgents and applies it. launchd owns every
daemon's lifecycle, so a supervisor crash or restart never interrupts a running daemon. The
supervisor LaunchAgent's `StartInterval` (+ `RunAtLoad`) re-runs `sync` so newly-cloned repos are
picked up and removed repos are torn down without any manual step.

### The two engine fixes

| Fix | Today | Change |
|---|---|---|
| Never-idle | `idle_timeout{300}`; `--idle-timeout 0` insta-kills because `now - last_activity >= 0` is always true (`daemon_server.cpp:668`, and `should_shutdown_for_idle` `daemon_lifecycle.cpp:33`) | `idle_timeout <= 0` disables the idle check entirely (both the serve-loop check and `should_shutdown_for_idle`); daemon stays resident until an explicit `shutdown` op or signal |
| Safe single-owner bind | `open_listen_socket` calls `::unlink(socket_path)` unconditionally, then `bind` (`daemon_server.cpp:141`) | before unlink, `::connect()` to the socket; if a live daemon answers, log `already serving <root>` and exit `0` without touching the endpoint; only unlink + rebind when connect fails (ECONNREFUSED/ENOENT = stale) |

The connect-probe distinguishes the two real cases cleanly: a **stale** socket file left by a crash
refuses the connection (safe to unlink), a **live** daemon accepts it (defer). This is the standard
Unix-domain single-instance guard and is what makes the always-on model race-safe.

### Discovery

`discover_tracked_repos(search_roots)` walks a configured list of search roots (default derived from
where cgraph `.mcp.json` files already live, e.g. `~/turinglabs/full-turing/*`, `~/tools/*`;
overridable via a supervisor config file) and returns each directory whose `.mcp.json` contains an
`mcpServers.cgraph` entry. It is a shallow, deterministic scan over a bounded set of roots — pure
except for the filesystem read — so it is testable against a temp fixture tree. A repo already
carrying a `cgraph` `.mcp.json` is exactly the signal that the user wants cgraph on it, so discovery
needs no separate opt-in registry (though the config file may add/exclude roots).

### Reconcile diff

`reconcile(discovered, installed) -> {to_add, to_remove}` is a pure set difference keyed by the
per-root hash (reuse `daemon_identity`'s canonical-root hash so the LaunchAgent label, socket name,
and reconcile key all agree). `sync` renders + `launchctl bootstrap`s each `to_add`, and
`launchctl bootout`s + removes each `to_remove`. Idempotent: running `sync` twice with no repo
changes is a no-op.

### Plist rendering

`render_launch_agent(label, program_args, {keep_alive, run_at_load, start_interval?})` produces a
plist string. Per-repo daemon: `ProgramArguments = [<graphd>, --root, <repo>, --idle-timeout, 0]`,
`KeepAlive = true`, `RunAtLoad = true`, `Label = com.cgraph.graphd.<root-hash>`. Supervisor:
`ProgramArguments = [<cgraph>, daemon, sync]`, `RunAtLoad = true`, `StartInterval = 300`. Pure string
function — fully unit-testable; the only untestable step is the `launchctl` call itself.

### Freshness & drift over a long-lived daemon

Queries hit the resident daemon's live in-memory snapshot, so they are always current (≤ one 2s
poll behind). On-disk `graph.json` trails by at most the 30s persist interval. Incremental
neighborhood dedup drifts the node set slightly above the canonical full build, but the existing
full-dedup reconcile every 5th update (`daemon_server.cpp:633`) bounds it — no new mechanism needed.

## Test strategy

Per `config.yaml`, each behavior gets a red-first test asserting observable behavior:

- **Never-idle** (`daemon_lifecycle_test`, `daemon_server_test`): `should_shutdown_for_idle` returns
  `false` for any elapsed time when `idle_timeout <= 0`; a served daemon with `idle_timeout = 0`
  advanced well past 300s of inactivity is still listening and answers `status`.
- **Single-owner bind** (`daemon_endpoint_test` / `daemon_hardening_test`): with a live daemon on a
  root's socket, a second `run_daemon_server` for the same root returns the "already serving" exit
  path without unlinking, and the original still owns the socket and answers a `query`; with only a
  **stale** socket file present (no listener), startup unlinks and binds successfully.
- **Discovery** (`daemon_supervisor_test`): against a temp tree mixing dirs with a `cgraph`
  `.mcp.json`, dirs with a non-cgraph `.mcp.json`, and dirs with none, `discover_tracked_repos`
  returns exactly the cgraph set; canonical-root hashing matches `daemon_identity`.
- **Reconcile diff** (`daemon_supervisor_test`): pure `reconcile(discovered, installed)` yields the
  correct `to_add`/`to_remove`; identical inputs yield empty diffs (idempotent).
- **Plist render** (`daemon_supervisor_test`): `render_launch_agent` emits the expected
  `ProgramArguments`, `KeepAlive`, `RunAtLoad`, `Label`, and (supervisor) `StartInterval`; parses as
  a valid plist.

### Cannot be tested directly

- **`launchctl bootstrap/bootout`** and the LaunchAgent actually starting at login are OS side
  effects. Validation approach: `sync` returns a structured plan (the `to_add`/`to_remove` it
  applied) that the test asserts on; the actual launchctl invocation is exercised in the manual
  end-to-end step (install supervisor, edit a tracked file, query within 2s and observe the change
  with zero manual refresh; clone a repo, wait one `StartInterval`, observe a new daemon appear;
  `launchctl print` shows both agents loaded). Record this in the PR.
- **Absolute freshness latency** is machine/poll dependent; tests assert "reflected after a poll",
  not an exact millisecond bound.

## Open questions

- Default search roots — derive from existing `.mcp.json` locations vs. ship a config file the user
  edits. Proposed: a `~/.config/cgraph/supervisor.toml` with `search_roots` + optional
  `exclude`, defaulting to the parents of currently-known cgraph repos. Settle during implementation.
- `StartInterval` cadence for re-discovery (300s proposed) vs. a `WatchPaths` on the search roots for
  near-instant pickup of a freshly cloned repo. Tunable, not a contract.
- Whether `cgraph daemon status` should also surface each daemon's graph freshness (last persist /
  incremental_updates via the `status` op) — nice-to-have, in scope for the status subcommand.
