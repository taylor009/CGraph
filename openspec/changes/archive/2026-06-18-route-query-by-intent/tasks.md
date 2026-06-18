## 1. Deterministic intent classifier

- [x] 1.1 `daemon_ops_test.cpp` (via `handle_daemon_request("query")`, the real dispatch): STRUCTURAL
      phrases (`who calls X`, `what does X call`) route correctly; ENTITY for a unique exact symbol;
      LEXICAL for an NL phrase, a substring-ambiguous name (`alpha` stays a 2-result search), and a
      structural phrase whose operand does NOT resolve (fall-through to search).
- [x] 1.2 Implemented `parse_structural_phrase` + the routing in `query_graph` (`daemon_ops.cpp`),
      priority order: structural-phrase-with-resolving-operand → unique-exact-entity → lexical.
      Relation tokens reuse the existing edge-type strings (`CALLS`, `references`, `inherits`,
      `imports`). [Entity rule refined to *unique exact* match — see design.md — to stay additive.]

## 2. Query dispatch + response shaping

- [x] 2.1 `daemon_ops_test.cpp`: exact symbol → `route:"entity"` + `neighbors` summary
      (callers/callees/references counts + top ids); `who calls X` → `route:"callers"` with the
      incoming-CALLS neighbors and `of:<X id>`; `what does X call` → `route:"callees"`; NL phrase →
      `route:"search"`. `kind`/`file`/`limit` narrowing applies via the shared `narrow`/`as_nodes`
      helpers.
- [x] 2.2 `query_graph` dispatches on the classifier and emits `route`; the structural/entity paths
      reuse one `typed_neighbors` walk (no duplicate traversal logic).

## 3. MCP surface + telemetry

- [x] 3.1 Updated the `graph_query` MCP description (`mcp_server.cpp`) to advertise direct structural
      answers (callers/callees/references/implementations/importers) and the `route` field, while
      remaining the name-search entry.
- [ ] 3.2 (Optional — deferred) op-stats per-route counters. Skipped to keep scope tight; the `route`
      field already lets a future ledger bucket calls. Pick up if route-adoption telemetry is wanted.

## 4. Verify

- [x] 4.1 Full suite `ctest --preset default` → 59/59 passed.
- [x] 4.2 Parity goldens unchanged (`extractor_goldens` + `pack_context_parity` pass) — `query` is
      not a parity surface; no incidental regression.
- [x] 4.3 Live smoke via `graphd --benchmark-query` on the real cgraph graph (1,323 nodes):
      `query_graph` → `route:"entity"`; `who calls query_graph` → `route:"callers"` with the operand
      resolved to its full node id; `route the query by intent` → `route:"search"` (10 hits).

## 5. Measurement (research, parked — not a merge blocker)

- [ ] 5.1 `research/query-routing/`: classification-accuracy probe (entity/structural/NL set) and the
      typed-vs-untyped neighbor precision/token comparison (the synthesis "testable nugget");
      document in `results.md` per the Research/Eval Discipline.
