## ADDED Requirements

### Requirement: A fused seam is served by a resident read-only daemon
`cgraph seam fuse` SHALL write a `.cgraph-seam` marker into its output directory alongside
`graph.json`. When `graphd` is started on a directory containing that marker, it SHALL run a static
seam serve loop rather than the build-and-watch server: it SHALL load the directory's `graph.json`,
publish it as the snapshot, and serve the read ops (`query`, `path`, `explain`, `impact`, `context`,
`status`) via the existing request handler, WITHOUT building a graph, watching files, persisting, or
running enrichment. Write ops SHALL be rejected (the seam is a read-only derived view); `update`
SHALL reload `graph.json` from disk; `shutdown` SHALL stop the daemon. The seam daemon SHALL be
addressed by the same per-root identity as any project, so the existing client and MCP tools reach it
by pointing their root / `project_root` at the seam directory, with no client or MCP change.

#### Scenario: A seam directory is served statically and queried cross-service
- **WHEN** a client (or MCP tool) addresses a seam directory (one produced by `seam fuse`, carrying
  the marker) as its root and issues `impact` / `explain` / `path` / `context`
- **THEN** `graphd` serves it from the loaded fused graph without building or watching, and returns
  cross-service answers (traversing the contract edges)

#### Scenario: Repeated queries are served by the resident daemon
- **WHEN** a second read op is issued against the same seam directory while the daemon is up
- **THEN** it is served by the resident daemon without reloading the graph (low-latency repeat),
  and `status` reports the loaded node/edge counts

#### Scenario: Writes are rejected; update reloads
- **WHEN** a write op (e.g. `remember` or fragment ingest) is sent to a seam daemon
- **THEN** it is rejected; and an `update` reloads `graph.json` from disk so a re-`fuse` is picked up
  without a rebuild

#### Scenario: A normal project is unaffected
- **WHEN** `graphd` starts on a directory without the `.cgraph-seam` marker
- **THEN** it runs the normal build-and-watch server unchanged
