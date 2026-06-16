# Tasks — graph-session-memory

## 1. Op plumbing
- [ ] 1.1 Add `Remember` and `Recall` to `DaemonOp` (before `Count`) in
  `operation_stats.hpp`; add their names in `daemon_op_name` and parsing in
  `daemon_op_from_string` (`operation_stats.cpp`). Do NOT add them to `kSubstantiveOps`
  (keep the durable ledger schema v1). Add a failing test that `daemon_op_from_string`
  resolves "remember"/"recall".

## 2. graph_remember (write)
- [x] 2.1 Test: remember writes the body to a file under `cgraph-out/memory/` and
  creates a `memory:checkpoint:<ts>` node (`kind:"checkpoint"`, `source_file`=md,
  `confidence: Inferred`, `properties.created_at` set). (daemon_ops_test return 91/93/94)
- [x] 2.2 Test: slugify strips path separators/dots so a title can never write outside the
  memory dir; body file lands inside memory_dir. (return 93)
- [x] 2.3 Test: an oversized body is rejected — error returned, no file written, node count
  unchanged. (return 95)
- [x] 2.4 Test: each resolvable `touches` entry creates exactly one `concerns` edge to the
  existing node; an unresolved entry creates NO edge and is reported. (return 92/94)
- [x] 2.5 Implement `remember`: resolve touches, sanitize+sandbox the md path, write body,
  set source_location spanning the body, build the checkpoint Fragment, `merge_fragment` via
  `mutate_graph_snapshot`. Wired in daemon_server (`memory_dir` + mark-dirty on remember).
- [ ] 2.6 DEFERRED to a follow-up change (user-scoped: "engine v1 now, defer overlay").
  Re-apply memory fragments after a full rescan so memory survives a rebuild from
  `index.files`. v1 relies on graph.json persist + restart fast-load for survival; a
  mid-session full rescan can still drop checkpoint nodes from the live snapshot (the md
  bodies remain on disk). Tracked separately.

## 3. graph_recall (read)
- [x] 3.1 Test: with two checkpoints at increasing timestamps, recall returns them
  newest-first; each entry carries its body snippet and briefs of its `concerns` targets;
  `query` filters by title/tags. (daemon_ops_test return 96/97/98)
- [x] 3.2 Test: `recall` body comes through `with_source`/source_file; live check confirmed
  `graph_context`-style snippet round-trip. (return 97 + live)
- [x] 3.3 Implement `recall`: pure-snapshot scan of `memory:checkpoint:*`, sort by
  `created_at` desc, filter, limit, emit `with_source` body + linked code briefs.

## 4. Analysis / retrieval exclusion
- [x] 4.1 Test: adding a memory node + `concerns` edge leaves the memory node with no
  `degree_centrality`/`god_node`, and code-node centrality equals the no-memory baseline.
  (analysis_test return 2/3) Community-detection exclusion deferred with the overlay (memory
  nodes never reach detect_communities in v1 — added post-pipeline).
- [x] 4.2 Test: `pack_context`/`query` over code never returns a `memory:` node even when a
  `concerns` edge makes it adjacent. (daemon_ops_test return 99)
- [x] 4.3 Implement `is_memory_node_id` (types.hpp) and gate `analyze_graph` (centrality +
  memory-incident edges), `matching_nodes` (query), and `pack_context` candidate gathering.

## 5. MCP surface
- [x] 5.1 mcp_server test: `graph_remember` forwards to op `remember`; `graph_recall` to
  `recall`. (mcp_server_test)
- [x] 5.2 Implement the two MCP tools + descriptions naming the workflow.

## 6. Docs + verify
- [x] 6.1 Added `docs/session-memory.md` (workflow: remember before /compact-or-/clear; recall
  after /clear; graph_context on linked nodes; never persist raw DOMs / chain-of-thought).
- [x] 6.2 Full suite `ctest --preset default`: 59/59 passing.
- [x] 6.3 Live check (real client→daemon IPC): remember (concerns=1 on ready graph), recall
  same-session returns the checkpoint with body snippet + linked code brief, memory inert to
  query/context. NOTE: a daemon RESTART rebuilds and drops the checkpoint (graph.json + body
  persist, but recall=0 after restart) — this is the deferred-overlay limitation (2.6); the
  primary /clear case (daemon stays up) is fully verified.
