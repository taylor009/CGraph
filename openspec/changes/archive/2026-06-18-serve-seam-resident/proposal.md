## Why

`cgraph seam query` answers cross-service questions one-shot (load → answer → exit), which reloads
the fused graph every call. For an agent querying a seam *interactively* — repeated `impact` /
`explain` / `context` while reasoning about a change — that per-call reload is wasteful, and the seam
isn't reachable through the MCP tools an agent already uses (`graph_query`/`graph_impact`/…), which
address a daemon by project root.

A spike already proved the engine half: a bare `DaemonState` + `load_graph_snapshot(fused.json)` +
`handle_daemon_request` serves every read op over a fused seam graph with correct cross-service
answers, and writes are safely rejected (no `memory_dir`). What's missing is making that *resident*:
a daemon that serves the fused graph statically, addressable like any project root, so the existing
client and MCP tools reach it unchanged.

## What Changes

- `cgraph seam fuse` writes a `.cgraph-seam` marker into its `--out` directory (alongside
  `graph.json`).
- `graphd`, when started on a root containing that marker, runs a **static seam serve loop** instead
  of the build+watch server: it loads `<root>/graph.json`, publishes the snapshot, and serves the
  read ops (`query`/`path`/`explain`/`impact`/`context`/`status`) via the existing
  `handle_daemon_request` — **no build, no file watcher, no persistence, no enrichment**. Writes
  (`remember`/ingest) are rejected (no `memory_dir`, as in the spike); `update` reloads
  `graph.json` from disk (so the refresh flow is re-`fuse` → `update`); `shutdown` stops it.
- Because identity is `hash(canonical root)`, a seam directory is just another addressable root: the
  **client and MCP are unchanged** — `cgraph-client --root <seamdir> …` and the MCP `graph_*` tools
  with `project_root = <seamdir>` auto-spawn `graphd`, which selects static mode by the marker. The
  seam daemon coexists with the real per-service daemons (distinct roots → distinct endpoints).

## Capabilities

### Modified Capabilities

- `cross-service-seam`: a fused seam is served by a resident, read-only daemon — addressable like any
  project root, so the existing client and MCP read ops query it with low latency and no per-call
  reload, completing Scope C.

## Non-Goals

- **Live watching of the service repos** — the seam stays a static snapshot; refresh is an explicit
  re-`fuse` + `update`. Watching N source trees to auto-re-fuse is out (and would reintroduce the
  multi-root coupling Scope C deliberately avoids).
- **Cross-daemon federation (C2)** — the seam daemon serves the pre-fused graph; it does not delegate
  to the per-service daemons.
- **Writes / mutation of a seam** — rejected; a seam is a derived view.
- **New MCP tools** — none; seams are queried through the existing `graph_*` tools by pointing
  `project_root` at the seam directory.

## Impact

- `src/engine/seam.cpp` / CLI: `seam fuse` writes the `.cgraph-seam` marker; an `is_seam_directory`
  helper.
- `src/engine/daemon_server.cpp`: a `run_static_seam_server(root, options)` (socket setup + load +
  accept loop + `handle_daemon_request`, `update` = reload), reusing `read_frame`/`write_frame`,
  `unix_socket_path`/`cleanup_daemon_endpoint`, `daemon_identity_for`, `load_graph_snapshot`. The
  listen-socket open/bind/listen is factored into a small shared helper used by both servers (no
  duplication). `src/daemon/main.cpp`: branch to it when the root is a seam directory.
- No change to the client, the MCP server, the query ops, or any parity surface.
- Verified by: an `is_seam_directory` unit test; a live smoke — `fuse` (marker written) → query the
  seam through `cgraph-client --root <seamdir>` (resident: cross-service `impact`/`explain` answers,
  a second call served without reload), a write op rejected, `update` reloading after a re-`fuse`;
  full suite + parity unchanged.
