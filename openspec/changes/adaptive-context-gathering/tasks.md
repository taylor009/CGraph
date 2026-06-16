## 1. Adaptive gather behind a flag (default off)

- [x] 1.1 `daemon_ops_test.cpp` (red): on a fixture graph, `gather="adaptive"` keeps all depth-0/1
      nodes; a depth-2 node with query overlap < θ contributes no depth-3 neighbors, one with ≥ θ
      does; `gather="fixed"` (and omitted) is byte-for-byte the current behavior.
- [x] 1.2 In `pack_context` (`daemon_ops.cpp:564`): read `gather` (default `"fixed"`) and
      `gather_theta` (default 0.05, clamped) beside the `packing` read (`:569`). When
      `gather="adaptive"`: set depth to `kKnapsackContextDepth` and add the depth≥2 relevance gate
      in the BFS loop (before the neighbor expansion at `:618`), reusing `lexical_terms` (`:167`) /
      `query_term_overlap` (`:207`). Greedy/fixed path untouched.

## 2. In-engine parity gate (blocks any default flip)

- [x] 2.1 Extend `pack_context_parity_test.cpp` (`:1-15`): add a `gather=adaptive, packing=knapsack`
      pass over the eval rows; assert mean grade-2 recall ≥ baseline + target margin and candidate
      cost << k=3, under the engine cost model; record the targets from the in-engine run (NOT the
      Python numbers). Skip when artifacts absent.
- [x] 2.2 Confirm `gather` default remains `"fixed"`; the parity test is the gate that must pass
      before any future default-flip change is proposed.

## 3. Benchmark + docs

- [x] 3.1 Record mean candidate count + gather wall-time for fixed-k2 vs adaptive vs k3 on a real
      query (the +13% vs +96% candidate-cost claim, re-measured in-engine).
- [x] 3.2 (Doc-only) note `gather`/`gather_theta` in the `graph_context` tool description
      (`src/mcp/mcp_server.cpp`); params already pass through, no logic change.

## 4. Verification

- [x] 4.1 `ctest --preset default` green; report pass/fail counts.
- [x] 4.2 Confirm `gather="fixed"` output and `graph.json` are unchanged (additive-only): diff a
      `context` response and a build against pre-change output for the same input.
- [x] 4.3 Record, per CLAUDE.md Research/Eval Discipline: exact command, baseline/candidate/delta,
      files changed, eval-unchanged, the layer (retrieval / candidate gathering), and that the
      Python harness result was revalidated in-engine before the mode is considered for default.
