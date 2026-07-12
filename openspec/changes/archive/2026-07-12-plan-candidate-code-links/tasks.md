## 1. Matcher module (deterministic, pure)

- [x] 1.1 `semantic_code_links_test` (red): `build_symbol_index` over a small graph; then
      `compute_candidate_links` —
      - a doc mentioning `classify_cached_file` returns that node id;
      - a doc whose only match is the bare word `cache` returns no candidate from it;
      - a capitalized type name (`GraphSnapshot`, kind struct) is kept;
      - a unique name outranks a name shared by many nodes;
      - results are capped at `max_links`;
      - empty/media text yields no candidates.
- [x] 1.2 Add `src/engine/semantic_code_links.{hpp,cpp}`: `CandidateLink`, `SymbolIndex`,
      `build_symbol_index(const GraphSnapshot&)`, `compute_candidate_links(doc_text, index,
      max_links = 10)`. Shape filter (compound / capitalized-type), specificity ranking, cap.
- [x] 1.3 Register the new source in `src/engine/CMakeLists.txt` and the test in
      `tests/smoke/CMakeLists.txt`.

## 2. Wire candidates into the plan

- [x] 2.1 Test (red): `plan_enrichment` over a fixture with `<out>/graph.json` present and a doc
      that names a known code symbol writes `plan.json` whose input carries the matching
      `candidate_links`; with no `<out>/graph.json`, `candidate_links` is empty and the rest of the
      plan is unchanged.
- [x] 2.2 In `plan_enrichment` (`semantic_orchestration.cpp`): load `<out>/graph.json` via the
      existing graph loader, `build_symbol_index` once, and attach `candidate_links` to each
      document input when emitting the manifest. Skip media inputs. Empty when no graph.

## 3. Skill update

- [x] 3.1 Update the `cgraph-enrich` skill doc to author from `candidate_links` (emit
      `doc -> <id>` edges using the handed-over ids), replacing the "discover ids via graph_query"
      guidance. Keep the relation choice with the host.

## 4. Verify

- [x] 4.1 Full suite `ctest --preset default` (report pass/total).
- [x] 4.2 End-to-end: run `enrich-plan` over this repo (with a built `graph.json`), confirm a
      design doc's input lists real code-node ids as `candidate_links` (e.g. `design.md` ->
      `SemanticChunkPlan`, `classify_cached_file`). Record the before/after plan.json snippet.
