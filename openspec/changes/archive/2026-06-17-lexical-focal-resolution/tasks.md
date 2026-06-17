## 1. Lexical focal fallback (shared resolver)

- [x] 1.1 `daemon_ops_test.cpp` (red first): a NL query with no substring match resolves to a
      lexically-overlapping focal (`"compute the gamma value"` → "d"); an exact id/label/bare-symbol
      still resolves to that exact node (`query:"Alpha"` → "a", pre-existing); an off-topic query
      (`"xylophone zebra quux"`) stays unresolved (focus:null).
- [x] 1.2 In the `context`/`query` focal block, after exact + `matching_nodes` come up empty, rank
      nodes by `query_term_overlap(lexical_terms(query), label)` (`lexical_matches`) and resolve the
      top match; zero shared terms → no match → stay unresolved (the confidence floor).
- [x] 1.3 Determinism: `lexical_matches` ties break (overlap desc, centrality desc, label).
- [x] 1.4 `ctest --preset default -R cgraph_daemon_ops_test` (pass).

## 2. Multi-seed gather (context)

- [x] 2.1 Test (red): `"alpha gamma"` resolves via fallback and the disconnected "d" (gamma) seed —
      reachable ONLY as its own seed — appears in the bundle, proving the union of ego graphs.
- [x] 2.2 Seed the undirected BFS from the top-N (`kFocalSeedCount = 5`) lexical matches; union by
      shallowest depth; pack once. Exact/substring/id hits stay single-seed (`seeds = {focal}`).
- [x] 2.3 `cgraph_daemon_ops_test` passes.

## 3. Zero-hit semantics preserved

- [x] 3.1 A lexically-resolved focus is NOT a zero hit; a below-threshold (no-overlap) query IS —
      automatic, since context zero-hit keys off `focus.is_null()` (daemon_ops.cpp:~1408) which the
      fallback drives. Off-topic test confirms null focus.
- [x] 3.2 The `query` op shares the fallback (`lexical_matches` when substring empty), overlap-ranked
      (centrality re-sort skipped for lexical results); `total==0` only when no term overlaps.

## 4. Verify end-to-end

- [x] 4.1 Live daemon: `context {"q":"report semantic connectivity of the enrichment layer"}` →
      focus `SemanticConnectivity` (correct symbol), 21-node bundle, 3999/4000 tokens — was
      `focus:null` in production.
- [x] 4.2 Offline direction re-confirmed: `research/focal_resolution.py` → grade-2 recall@8k
      0.000 → 0.317 (lexical_top5), stable.
- [x] 4.3 Full suite `ctest --preset default` 59/59, parity goldens unchanged.
