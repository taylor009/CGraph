# Tasks

## 1. Self-describing context responses
- [x] 1.1 In `daemon_ops_test.cpp`, add a failing assertion that the greedy `context` response carries `gather:"fixed"` and `packing:"greedy"` (and that adaptive carries `gather:"adaptive"`).
- [x] 1.2 In `daemon_ops.cpp`, add `gather` and `packing` to the greedy response branch (`:795-803`); confirm the knapsack branch already emits both (`:745-756`). Make 1.1 pass.

## 2. Adaptive reach summary
- [x] 2.1 In `daemon_ops_test.cpp`, extend the 3-hop gate fixture: with a query matching the relevant depth-2 node assert `reach.expanded_past_core >= 1`; with a non-matching query assert `reach.expanded_past_core == 0`. Failing first.
- [x] 2.2 In `daemon_ops.cpp`, count gate rejections at the depth≥2 `continue` site (`:624-630`) and depth≥3 admissions when building candidates (`:646-655`); attach the `reach` object to the response when `gather="adaptive"`. Make 2.1 pass.

## 3. Context zero-result signal
- [x] 3.1 In `daemon_ops_test.cpp`, assert a `context` request with an unresolvable focus records a zero-hit, and one returning focus + ≥1 included does not. Failing first.
- [x] 3.2 In `daemon_ops.cpp`, compute the `context` zero-result condition (focus unresolved or all candidates omitted) and pass it as the zero-hit at the record boundary (`:1096-1098`, `1114`). Make 3.1 pass.

## 4. Per-mode op-stats
- [x] 4.1 In `operation_stats_test.cpp`, record N adaptive + M fixed `context` calls, persist + reload, and assert the rollup reports the adaptive `context` count distinctly and additively. Failing first.
- [x] 4.2 In `operation_stats.{hpp,cpp}` (+ `include/cgraph/operation_stats.hpp`), thread an `adaptive_context` flag through `record`, increment a dedicated counter, persist it as one additive JSONL field, and sum it in the cross-session rollup (read with 0 default for old files). Wire the call site in `daemon_ops.cpp:1114`. Make 4.1 pass.

## 5. MCP discoverability + forwarding
- [x] 5.1 In `mcp_server_test.cpp`, add a `graph_context` `tools/call` asserting `gather`/`gather_theta` forward to `op=context`, and a `tools/list` assertion that the `graph_context` description contains "adaptive". Failing first.
- [x] 5.2 In `mcp_server.cpp`, rewrite the `graph_context` tool description (`:78-90`) to name both gather modes, say when adaptive helps, and note the `query` requirement. Make 5.1 pass.
- [x] 5.3 Expand the `graph_context` entry in `README.md` (`:204`) with a concrete adaptive call and the recall/cost framing. (Doc-only; reviewed, not unit-tested.)

## 6. Verify end-to-end
- [x] 6.1 Build and run the targeted tests: `ctest --preset default -R 'cgraph_daemon_ops_test|cgraph_mcp_server_test|cgraph_operation_stats_test'`.
- [x] 6.2 Re-run the parity gate to prove no recall regression: `CGRAPH_REPO_ROOT=$PWD ctest --preset default -R cgraph_pack_context_parity_test`.
- [x] 6.3 Run the full suite (`ctest --preset default`) and record pass/fail counts.
- [x] 6.4 Live MCP check: spawn the stdio server on a fixture repo, call `graph_context` with `gather:"adaptive"`, and confirm the response carries `gather:"adaptive"` + a `reach` summary with `expanded_past_core > 0`.
