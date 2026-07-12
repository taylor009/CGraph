## 1. Connectivity helper (pure, deterministic)

- [x] 1.1 `semantic_connectivity_test` (red): build small graphs and assert —
      - `doc -> code` direct: doc connected at hop_bound 1;
      - `doc -> concept -> code`: connected at hop_bound 2, NOT at 1 (transitive + bound);
      - `doc -> concept` with a code-less concept: orphan doc + orphan concept;
      - a concept WITH a code edge is not orphan;
      - `doc_code_edges` counts only semantic->code edges;
      - pure-code graph: all semantic counts 0 and `connectivity_rate == 0` (no divide by zero).
- [x] 1.2 Add `src/engine/semantic_connectivity.{hpp,cpp}`: `SemanticConnectivity` struct and
      `compute_semantic_connectivity(const GraphSnapshot&, hop_bound = 2)`. Semantic node = id
      prefixed `doc:`/`concept:`/`topic:`. Build undirected adjacency once; bounded BFS per doc.
- [x] 1.3 Register the source in `src/engine/CMakeLists.txt` and the test in
      `tests/smoke/CMakeLists.txt`.

## 2. Surface in status

- [x] 2.1 `daemon_ops_test` (red): after publishing a snapshot with a `doc -> code` edge,
      `status.result.semantic` reports `doc_nodes >= 1` and `connectivity_rate > 0`; a pure-code
      snapshot reports zero doc/concept nodes.
- [x] 2.2 In `status()` (`daemon_ops.cpp`): call the helper on the snapshot and emit the `semantic`
      block (doc_nodes, concept_nodes, connected_docs, orphan_docs, orphan_concepts, doc_code_edges,
      connectivity_rate).

## 3. Verify

- [x] 3.1 Full suite `ctest --preset default` (report pass/total).
- [x] 3.2 End-to-end: against the live daemon on this repo (already enriched), confirm
      `graph_status.semantic` reports the real numbers (≈14 docs, ≈50% connectivity, 2 orphan
      concepts). Record the status snippet.
