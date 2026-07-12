## 1. Fix and verify

- [x] 1.1 Strengthen `graph_builder_test` merge case: assert first-occurrence-wins label and
      cross-fragment hyperedge dedup (guards the changed dedup path against a last-wins regression).
- [x] 1.2 Rewrite `merge_fragments` to maintain node/edge/hyperedge dedup sets once across all
      fragments; keep `merge_fragment` unchanged for the single-merge `semantic_ingest` caller.
- [x] 1.3 Run `ctest --preset default -R cgraph_graph_builder_test` (pass).
- [x] 1.4 Run the full suite `export VCPKG_ROOT="$PWD/.vcpkg" && ctest --preset default` (53/53 pass).
- [x] 1.5 Verify parity on the reference repo: `graph.json` node/link counts identical
      before and after (26,914 nodes / 77,330 links); cold build ~452s -> ~28.7s.
