# Tasks — graph-session-memory-overlay

## 1. Sidecar fragment at remember time
- [x] 1.1 Test: after `remember`, a `*.json` sidecar exists beside the `*.md` under
  `cgraph-out/memory/`, and `parse_fragment` reads it into a fragment with the checkpoint node
  + its `concerns` edges. (daemon_ops_test return 100/101)
- [x] 1.2 Implement: in `remember`, serialize the checkpoint Fragment with
  `to_json(const Fragment&)` and write `cgraph-out/memory/<ts>-<slug>.json` (same stem as the
  body `.md`). In-memory `merge_fragment` kept for immediate recall. (daemon_ops.cpp)

## 2. Memory re-overlay hook
- [x] 2.1 Test: applying the same memory fragment twice yields exactly one checkpoint node +
  one `concerns` edge (idempotent; counts unchanged). (daemon_ops_test return 105)
- [x] 2.2 Test: after a rebuild drops the memory node, the overlay restores it and recall
  returns it. (daemon_ops_test return 103/104; live RESTART survived=True)
- [x] 2.3 Test: same against an incremental rebuild. (live AFTER EDIT survived=True; engine
  test simulates rebuild-drop-then-restore)
- [x] 2.4 Implement `ingest_all_memory()` in `daemon_server.cpp` (scan `cgraph-out/memory/*.json`,
  validate/parse, merge via `mutate_graph_snapshot`).
- [x] 2.5 Wired at the three rebuild sites that call `ingest_all_drops` (rescan, fast-load,
  incremental). `memory_dir` local + `state.memory_dir` point at it.

## 3. graph.json is not the source of truth for memory
- [x] 3.1 Test: after `remember`, the daemon-persisted node-link JSON contains no `memory:`
  node, yet recall still returns the checkpoint. (daemon_ops_test return 102; live "graph.json
  memory nodes: 0")
- [x] 3.2 Implement: on the daemon persist path (`daemon_lifecycle.cpp` persist_graph_snapshot),
  filter `memory:` nodes + incident edges from the snapshot copy before `to_node_link_json` —
  daemon persist only, not the shared serializer / one-shot exports.

## 4. Defensive recall (dangling concerns)
- [x] 4.1 Test: a checkpoint whose `concerns` target no longer exists still recalls (target
  skipped, no error). (daemon_ops_test return 106) Confirmed the existing skip-on-miss covers it.

## 5. Restart survival (end to end)
- [x] 5.1 Live (real IPC): remember -> stop daemon -> fresh daemon -> recall returns the
  checkpoint with body + links=1. Verified.

## 6. Verify
- [x] 6.1 Existing session-memory tests stay green (v1 daemon_ops/analysis/mcp blocks): memory
  still excluded from analysis/query/context.
- [x] 6.2 Full suite `ctest --preset default`: 59/59 passing.
- [x] 6.3 Live check: remember -> restart graphd -> recall (survived, links intact); remember ->
  edit watched file (incremental rebuild) -> recall (survived); graph.json holds 0 memory nodes.
