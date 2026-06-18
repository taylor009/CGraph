# Design — resident static seam daemon (Scope C stage 2)

## Mode selection by marker

```
seam fuse --out DIR  →  writes DIR/graph.json, DIR/graph.html, and DIR/.cgraph-seam (marker)

graphd --root DIR
   │
   ├─ is_seam_directory(DIR)  (DIR/.cgraph-seam exists)?
   │        yes ▼                              no ▼
   │   run_static_seam_server(DIR)        run_daemon_server(DIR)   (unchanged)
   └─ client/MCP auto-spawn `graphd --root DIR` exactly as today; graphd picks the mode
```

Identity is unchanged (`daemon_identity_for(DIR)` → `graphd-<hash(DIR)>`), so a seam directory is a
normal addressable root. The seam daemon and the real per-service daemons (different roots) get
different endpoints and coexist. The client and MCP server need **no change** — they already address
a daemon by `--root` / `project_root` and auto-spawn `graphd` for it.

## run_static_seam_server(root, options)

```
identity   = daemon_identity_for(root); socket_path = unix_socket_path(identity)
listen_fd  = open_listen_socket(socket_path)          // shared helper (see below)
DaemonState state; load_graph_snapshot(state, root/"graph.json")
state.update_handler = [reload root/"graph.json" into the snapshot]   // re-fuse → update refreshes
// memory_dir left empty  → remember/ingest rejected (the spike's read-only behavior)
accept loop while !state.shutdown_requested:
    select(listen_fd, idle_timeout)
      timed out with no activity for idle_timeout  → break (idle exit)
      ready → accept → read_frame → handle_daemon_request(state, req) → write_frame
cleanup_daemon_endpoint(socket_path)
```

This is the existing accept loop (daemon_server.cpp) with the build worker, file/drop watchers,
incremental updates, persistence, and enrichment **removed** — a select/accept/dispatch loop over a
fixed snapshot. The spike already verified the dispatch half (all read ops correct over a fused
graph, writes rejected); this adds only the resident socket loop.

## Shared listen-socket helper (avoid duplication)

The socket / bind / listen block (≈25 lines, self-contained) is factored out of `run_daemon_server`
into `open_listen_socket(socket_path) -> int` and reused by both servers. This is a safe, local
extraction (no entanglement with the build/watch loop), so the static server doesn't duplicate it.

## Refresh & staleness

The seam is a static snapshot. To refresh after service code changes: re-run `seam fuse` (it reads
each service's latest persisted `graph.json`), then `update` on the seam daemon, which the static
server handles by reloading `root/graph.json` (no rebuild). `status` reports the node/edge counts so
an agent can see what it's querying. Auto-watching the N service repos is a deliberate non-goal.

## MCP exposure (no new code)

The MCP `graph_query` / `graph_path` / `graph_explain` / `graph_impact` / `graph_context` tools take
a `project_root`; pointing them at the seam directory routes to the static seam daemon and returns
cross-service answers. So "MCP exposure" is automatic — no new tools, no MCP server change. (A
one-line hint in the tool descriptions that a seam directory is a valid `project_root` is optional
and not required for function.)

## Risks

- **New resident socket loop** — the one genuinely-new code path (the spike covered dispatch, not the
  accept loop). Mitigated by mirroring the proven loop in `run_daemon_server` and sharing
  `read_frame`/`write_frame` + the listen-socket helper; verified by a live client round-trip.
- **Stale graph served silently** — accepted for v1 (snapshot semantics, like the fuse view);
  `status` exposes counts and refresh is explicit. A staleness signal is a possible follow-up.
- **Marker false-positive** — only `seam fuse` writes `.cgraph-seam`; a normal project never has it,
  so a real project is never mistaken for a seam.
