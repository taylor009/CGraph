# Design — graph-session-memory

## Test strategy (before implementation)

Every behavior is a deterministic function of request params + the in-memory graph + a
sandboxed directory, so all of it is smoke-testable without a model or network.

1. **Write sandbox** — `graph_remember` writes only under `cgraph-out/memory/`. Test: a body
   writes a file inside that dir; a `title`/slug that would escape (`../`, absolute path,
   separators) is sanitized or rejected, never writing outside the memory dir. Assert no file
   appears outside `cgraph-out/memory/`.
2. **Oversize reject** — a body over the cap returns an error, writes no file, and leaves the
   graph node count unchanged (no partial node).
3. **Node + edges shape** — a successful remember creates exactly one
   `memory:checkpoint:<ts>` node with `kind:"checkpoint"`, `source_file` = the md path,
   `confidence: Inferred`, `properties.created_at` set; and one `concerns` edge per resolved
   `touches`. An unresolved `touches` entry yields NO edge and is reported in the response.
4. **Snippet-readable body** — after remember, `graph_context`/`explain` on the checkpoint id
   returns the body text as the focal snippet (proves the `source_file`→markdown round-trip).
5. **Recall ordering** — write three checkpoints with increasing timestamps; `graph_recall`
   returns them newest-first, each carrying its body snippet and briefs of its `concerns`
   targets; `limit` caps the count.
6. **Analysis exclusion** — build a graph, add a `memory:` node + edges via remember, run the
   analysis path; assert the memory node has no `degree_centrality`/`god_node` property and is
   absent from any community assignment, and that code-node centrality is identical to the
   no-memory baseline (memory does not shift the distribution).
7. **Retrieval exclusion** — `pack_context` / `query` over code never returns a `memory:` node
   as a candidate, even when a `concerns` edge makes it adjacent to the focal code node.
8. **MCP forwarding** — `graph_remember` and `graph_recall` tools forward their args verbatim
   to ops `remember` / `recall`.

## Mechanism

### New ops
Extend `DaemonOp` with `Remember` and `Recall` before `Count` (`operation_stats.hpp:65`); add
their names to `daemon_op_name` (`operation_stats.cpp:44`) and `daemon_op_from_string` (`:49`);
add two cases to the dispatch switch (`daemon_ops.cpp:1107`). They are recorded in the live
`lifetime` op-stats (the loop iterates all op indices) but are **not** added to
`kSubstantiveOps` / the durable ledger (`operation_stats.cpp:170`), so the versioned ledger
schema is unchanged in v1.

### `remember` write
`handle_daemon_request` operates only on a `GraphSnapshot`; `DaemonState` carries no project
root or output dir (confirmed: no such field). So `remember` cannot reach `cgraph-out/memory/`
on its own. It uses the same injected-handler pattern as `update` — `update_handler` is
"injected by the running daemon (which owns the file index and project root)"
(`daemon_ops.hpp:54-57`). v1 adds a `remember_handler` set by `daemon_server`; when unset
(in-process/tests), a test injects one pointing at a temp dir. `recall`, by contrast, is a
pure-snapshot op (it scans `memory:` nodes and `with_source` reads each node's stored
`source_file`), so it needs no handler.

The handler routes through `mutate_graph_snapshot` (`daemon_ops.cpp:1079`), the existing
single-writer path that copies → mutates → atomically publishes under `writer_mutex`. Steps:
- Resolve `touches` ids/names against the current snapshot (reuse `resolve_node`).
- Sanitize the title into a slug; compose `cgraph-out/memory/<iso-ts>-<slug>.md`; verify the
  resolved real path is inside the memory dir (reject otherwise); write the body.
- Build a `Fragment` { the checkpoint node, `concerns` edges to resolved targets } and
  `merge_fragment` it (`graph_builder.cpp:101`).
- Also persist the fragment as a file under `cgraph-out/memory/` so it can be re-overlaid.

### Persistence / survival (the load-bearing detail)
Memory nodes added live would be **dropped by a full rescan**, which rebuilds the graph from
`index.files` (extraction results that never contain memory nodes). Semantic enrichment solves
the identical problem by re-overlaying drop fragments after load via `ingest_all_drops`
(`daemon_server.cpp`). v1 reuses that pattern: the memory dir is an overlay source, and its
fragments are re-applied after every full rescan and on restart, exactly like
`semantic-drop/`. This is the only way memory survives the rescan/restart paths.

### `recall` read
Scan the snapshot for ids beginning `memory:checkpoint:`, sort by `properties.created_at`
descending (the id embeds the ISO timestamp as a tiebreaker), optionally filter by `query`
over title/tags, take `limit`, and for each emit `with_source(node_brief)` (body snippet) plus
`node_brief` of each `concerns` target. Bounded by `limit` + existing snippet caps.

### Exclusion
A single predicate `is_memory_id(id) := id.starts_with("memory:")` (mirroring
`is_doc`/`is_concept`, `semantic_connectivity.cpp:12-14`) gates three sites:
- `analyze_graph` (`analysis.cpp:152`) — skip memory nodes when computing centrality/god_node.
- `detect_communities` (`analysis.cpp:96`) — exclude memory nodes/edges from clustering.
- `pack_context` candidate gathering (`daemon_ops.cpp:564`) — never gather a `memory:` node as
  a code-context candidate.

## Case / ID notes
The id is `memory:checkpoint:<iso-ts>`. Per `make_id` casefolding/normalization
(`normalize.cpp:111`) the stored id will be normalized; recall and exclusion match on the
normalized `memory_` prefix consistently. Timestamps are supplied by the daemon at write time
(the engine reads wall-clock only at the write boundary, like the ledger flush).

## Untestable-directly notes
None. The wall-clock timestamp is the only non-deterministic input; tests inject or tolerate
it (assert ordering and prefix, not exact value).

## Deliberately deferred (v1 non-goals, noted for the follow-up)
- GC/TTL and accumulation bounds (memory grows unbounded in `graph.json` until pruned).
- Stale-edge repair when `concerns` targets are renamed/removed.
- Staleness signaling on recall beyond surfacing `created_at` and `Inferred` confidence.
