# Design — cgraph seam query (one-shot, staged stage 1)

## Flow

```
cgraph seam query --graph FUSED.json <op> [PARAMS_JSON]
        │
        ├─ op ∈ {query, path, explain, impact, context}?  else -> error, exit 2
        ├─ DaemonState state;  load_graph_snapshot(state, FUSED.json)   (publishes the snapshot)
        ├─ params = PARAMS_JSON ? parse : {}
        ├─ response = handle_daemon_request(state, make_request(op, params))
        └─ print response.dump(2); exit 0 (or 1 if response.ok == false)
```

This is exactly the spike, productized. No new engine code — `load_graph_snapshot`,
`make_request`, and `handle_daemon_request` are already public and were exercised by the spike over
a real fused seam graph.

## Why this answers cross-service questions for free

The fused graph (`seam fuse` output) contains every service's real nodes/edges PLUS the contract
edges (`CONSUMES`, `SERVED_BY`, `RESPONDS_WITH`, `CONSUMED_AT`, `MIRRORED_BY`). The read ops traverse
the snapshot without caring about node provenance, so:

| question | op | traversal |
|---|---|---|
| who consumes endpoint X | `explain X` | incoming `CONSUMES` / `CONSUMED_AT` neighbors |
| what breaks if schema X changes | `impact X --direction dependents` | `RESPONDS_WITH`/`CONSUMES` (and `MIRRORED_BY` is a dependency edge) |
| how does backend fn reach endpoint X | `path fn X` | shortest path across the contract edge |
| load context around a call site | `context --query …` | adaptive gather pulls the endpoint+schema+services |

(Spike output confirmed each of these returned cross-service nodes, with `context` carrying
`source_file` paths into both repos.)

## Op gating

Only the five read ops are accepted. The others are rejected up front with a clear message rather
than relying on incidental safety, even though `handle_daemon_request` already degrades them on a
bare state (`update` is a no-op without an `update_handler`; `remember`/ingest return
"session memory is not enabled" without a `memory_dir`; `shutdown` only flips a local flag). Explicit
gating keeps the contract obvious: a seam is queryable, not mutable.

## Testing

The CLI is thin wiring, but the *capability* ("a fused seam is queryable, cross-service") is worth a
regression test independent of the CLI: in `seam_test.cpp`, build a fused snapshot via the
already-tested `fuse_seam`, publish it on a `DaemonState`, and assert `handle_daemon_request`
returns cross-service results for `impact` (schema → endpoint/service) and `path`
(backend node → endpoint), plus that a write op is rejected. This pins the exact path the CLI drives.

## Staleness & the deferred resident daemon

A seam is a static snapshot; refresh = re-run `seam fuse` (which reads each service's latest
persisted `graph.json`). One-shot reload-per-call is acceptable for occasional/batch agent use.

If interactive latency or MCP exposure later matters, stage 2 is the resident static daemon the spike
validated: a `.cgraph-seam` marker written by `seam fuse`; `graphd` selecting a read-only
static-serve loop (`DaemonState` + `load_graph_snapshot` + endpoint/accept + `handle_daemon_request`,
no build/watch) when the marker is present; client/MCP reaching it via `--root <seamdir>` unchanged.
Explicitly out of scope here.

## Risks

- **Op/params surface confusion** — mitigated by mirroring the daemon op names + JSON params the
  client already uses, and gating to the five read ops with a clear error otherwise.
- **Local-only snippets** — `context` needs the service repos at their absolute paths; documented,
  same constraint as the fuse view.
