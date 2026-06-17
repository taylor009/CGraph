## 1. Exclusion predicate (name list + structural marker)

- [x] 1.1 Add `path_ignore_test.cpp` cases (red first): each new name is skipped
      (`.venv`, `venv`, `site-packages`, `__pycache__`, `.tox`, `.nox`, `.pytest_cache`,
      `.mypy_cache`, `.ruff_cache`, `.hypothesis`, `.eggs`); a dir containing `pyvenv.cfg` is
      skipped under a non-listed name (`qa-env/`); over-skip negatives (`my_env_utils/`,
      `environment/`, `env`, a dir without `pyvenv.cfg`) are NOT skipped.
- [x] 1.2 Extend `is_skipped_directory` with the Python-ecosystem names.
- [x] 1.3 Add `is_dependency_directory(const std::filesystem::path&)` = name match OR
      `dir/"pyvenv.cfg"` exists; keep `is_skipped_directory(name)` as the inner name check.
- [x] 1.4 `ctest --preset default -R cgraph_path_ignore_test` (pass).

## 2. Wire into detection and the watcher

- [x] 2.1 `detect.cpp` directory-skip uses `is_dependency_directory(entry.path())`.
- [x] 2.2 `file_watcher.cpp` directory-skip uses the same predicate (the planner
      `semantic_chunk_plan.cpp` keeps `is_skipped_directory(name)` and gets the name-based
      additions for free).
- [x] 2.3 `cgraph_detect_test`, `cgraph_file_watcher_test`, and incremental tests pass (6/6).

## 3. Verify end-to-end

- [x] 3.1 Built the cgraph repo graph: node count dropped 39,142 -> 1,281 (edges 1,630), and
      0 nodes have a `source_file` under `.venv/` or `site-packages/`.
- [x] 3.2 SIGBUS trigger gone at the source: 0 `ast-depth-cap` warnings — the numpy header is
      never walked.
- [x] 3.3 Parity goldens unchanged: `extractor_goldens` + `pack_context_parity` pass (2/2).
- [x] 3.4 Full suite `ctest --preset default` (59/59 pass).
