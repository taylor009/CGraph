## Why

`cgraph seam gen` + `seam fuse` build and *render* a cross-service seam, but the real agent payoff
is *querying* it: "what backend call sites + mirror types break if this ml-api endpoint's schema
changes?" (`impact`), "how does this backend function reach that endpoint?" (`path`), "what consumes
this endpoint?" (`explain`). A fused seam graph already encodes those cross-service edges, and the
daemon's read ops operate purely on a `GraphSnapshot` — so they answer cross-service questions with
no query-logic change.

A spike confirmed this end-to-end: a bare `DaemonState` + `load_graph_snapshot(fused.json)` +
`handle_daemon_request` returned correct cross-service answers for `explain` / `impact` / `path` /
`context` over a fused seam graph, and write ops were already safely rejected (no `memory_dir`).
That spike *is* a one-shot query, productized in ~30 lines of CLI wiring over existing public
engine functions — zero new engine logic.

This change ships that first, deliberately staged: cross-service querying reaches agents/users
immediately via the CLI, while the larger resident-daemon question (low-latency repeated queries,
MCP exposure, marker-based addressing) is deferred until it's proven necessary.

## What Changes

- Add `cgraph seam query --graph FUSED.json <op> [PARAMS_JSON]` (sibling to `seam gen` / `seam fuse`):
  load the fused seam graph and run one read op against it, printing the JSON response.
- Supported ops are the five read ops — `query`, `path`, `explain`, `impact`, `context` — dispatched
  through the existing `handle_daemon_request`. A non-read op (e.g. `update`, `remember`, `shutdown`)
  is rejected with a clear message; the seam graph is a read-only derived view.
- Each invocation loads the graph fresh (one-shot); the seam is a static snapshot of the fused
  service graphs, refreshed by re-running `seam fuse`.

## Capabilities

### Modified Capabilities

- `cross-service-seam`: a fused seam graph becomes queryable — `cgraph seam query` answers
  cross-service `query`/`path`/`explain`/`impact`/`context` over it, read-only.

## Non-Goals

- **A resident seam daemon / MCP exposure** — deferred (the staged stage 2). The spike showed the
  static-serve loop is feasible (a marker-selected read-only mode in `graphd`, client/MCP via
  `--root <seamdir>`), but it is a separate change pursued only if interactive latency or live agent
  querying turns out to matter.
- **Cross-daemon federation / liveness** (Scope C2) — still out; the seam is a static snapshot.
- **Write ops on a seam** — rejected by design; a seam is a derived view, not a source of truth.
- **New query semantics** — none; the read ops are unchanged and simply operate over the fused graph.

## Impact

- `src/cli/main.cpp`: the `seam query` subcommand (parse `--graph` + op + params; `load_graph_snapshot`;
  validate the op is a read op; `handle_daemon_request`; print). Usage line + `seam <gen|fuse|query>`
  dispatch. No engine module changes — it reuses the public `load_graph_snapshot`, `make_request`,
  and `handle_daemon_request`.
- `tests/smoke/seam_test.cpp`: an integration case asserting a fused seam snapshot answers
  cross-service `impact` / `path` through `handle_daemon_request` (the path the CLI drives), plus a
  read-only-rejection check.
- No daemon/parity surface touched. `context` snippet reads resolve against each service's absolute
  `source_file` paths (local-only, as with the fuse view).
- Verified by: the integration test + a live smoke (`gen` → `fuse` → `seam query impact/path/explain`
  returning cross-service results), and the full suite.
