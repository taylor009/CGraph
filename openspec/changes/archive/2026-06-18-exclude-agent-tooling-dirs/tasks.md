## 1. Skip agent-tooling config dirs

- [x] 1.1 `path_ignore_test.cpp`: each of `.claude`, `.codex`, `.gemini`, `.cursor`, `.factory`,
      `.opencode`, `.windsurf`, `.aider`, `.specify` is skipped; non-dotted lookalikes (`claude`,
      `factory`, `specify`) are NOT skipped.
- [x] 1.2 Added the agent-tooling names to `is_skipped_directory` (`path_ignore.cpp`).
- [x] 1.3 `ctest --preset default -R cgraph_path_ignore_test` (pass).

## 2. Verify detection + enrichment planner benefit

- [x] 2.1 `detect.cpp` / `file_watcher.cpp` skip the dirs (shared predicate; covered by 1.x).
- [x] 2.2 `cgraph enrich-plan` on backend: plannable docs 174 → 102, agent-tooling docs in plan
      66 → 0 (`.claude`/`.factory`/`.specify` no longer planned).

## 3. Verify

- [x] 3.1 Parity goldens unchanged: `extractor_goldens` + `pack_context_parity` pass (2/2).
- [x] 3.2 Full suite `ctest --preset default` 59/59.
