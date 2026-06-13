## 1. Fix the offset + regression test

- [x] 1.1 `semantic_orchestration_test` (red): seed the drop dir with non-contiguous fragments
      (`chunk_00.json`, `chunk_05.json`), run `plan_enrichment` over a tree with an uncached doc,
      and assert the first new manifest `fragment` is `chunk_06.json` or higher and equals no
      existing filename. (Pre-fix this is `chunk_02.json` — a collision-in-waiting.)
- [x] 1.2 In `plan_enrichment` (`semantic_orchestration.cpp`): replace
      `fragment_offset = discover_semantic_fragment_drops(drop_dir).size()` with one past the
      maximum existing `chunk_index` (0 when none). Reuse the `chunk_index` already on
      `SemanticFragmentDrop`.

## 2. Verify

- [x] 2.1 Full suite `ctest --preset default` (report pass/total); confirm the existing
      contiguous-case orchestration assertions still pass.
- [x] 2.2 End-to-end: in a scratch drop dir seeded with `chunk_00.json` + `chunk_05.json`, run
      `cgraph enrich-plan` and confirm `plan.json` assigns `chunk_06.json`+ (no collision). Record
      the snippet.
