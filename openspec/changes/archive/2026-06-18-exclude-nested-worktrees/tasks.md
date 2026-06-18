## 1. Structural git-worktree exclusion

- [x] 1.1 `path_ignore_test.cpp`: (a) a dir whose `.git` is a regular file (live linked-worktree
      marker) is skipped by `is_dependency_directory`; (b) a `worktrees` dir under a DOTTED parent is
      skipped (covers stale checkouts whose `.git` is pruned); (c) a real repo root (`.git`
      directory) is NOT skipped; (d) a dir with no `.git` is NOT skipped; (e) a legitimate
      `src/worktrees/` module (non-dotted parent) is NOT skipped; the existing name skip-list and
      `pyvenv.cfg` behavior are unchanged.
- [x] 1.2 Added two structural worktree markers to `is_dependency_directory` (`path_ignore.cpp`),
      beside the existing `pyvenv.cfg` probe: `is_regular_file(dir / ".git")` (live worktree) OR
      `dir.filename() == "worktrees"` with a dotted parent (the `<.tool>/worktrees/` convention,
      catching stale checkouts). The dotted-parent guard protects a real `worktrees/` source module.
      [Refined from the proposal's `.git`-file-only marker after the rebuild showed 4 of 6 anvil
      worktrees were stale (no `.git`) and leaked 161 nodes.]
- [x] 1.3 `ctest --preset default -R cgraph_path_ignore_test` → passed.

## 2. Enrichment planner consistency

- [x] 2.1 `semantic_chunk_plan.cpp` now gates directory recursion on
      `is_dependency_directory(entry.path())` (subsumes the former name-only `is_skipped_directory`),
      so worktree trees are not planned for enrichment.

## 3. Verify end-to-end

- [x] 3.1 Detection + watcher benefit automatically (both call `is_dependency_directory(path)` —
      `detect.cpp:122`, `file_watcher.cpp:79`); no change needed there.
- [x] 3.2 Rebuilt project-anvil: total nodes 4,868 → 1,633, files 883 → 287, leaked worktree nodes
      3,268 → **0** (no node has a `source_file` under any `.*/worktrees/`).
- [x] 3.3 Parity goldens unchanged: `extractor_goldens` + `pack_context_parity` pass (2/2).
- [x] 3.4 Full suite `ctest --preset default` → 59/59 passed.
