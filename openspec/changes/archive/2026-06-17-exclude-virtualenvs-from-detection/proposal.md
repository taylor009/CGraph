## Why

`detect_project_files` walks the whole tree and indexes every recognized source file except those
under a hardcoded skip list (`path_ignore.cpp::is_skipped_directory`: `node_modules`, `vendor`,
`target`, `build`, `dist`, …) or matched by the root `.gitignore`. The skip list encodes a clear
principle — **index the project's own source, skip dependency/build/tooling trees** — but it has
no entry for the Python ecosystem. Any repo containing a virtualenv gets its entire `site-packages`
indexed as if it were project code.

Concretely, on this repo: a `research/.venv` inflates the cgraph graph to **39,142 nodes, ~97% of
which are NumPy/site-packages noise** — only ~1,181 are real cgraph code. This:

- drowns real-code retrieval in dependency noise (signal-to-noise collapse), and
- was the **root cause of the recent SIGBUS crash**: a machine-generated NumPy C header
  (`numpy/_core/include/numpy/__multiarray_api.h`) nested past 500 levels overflowed the
  extraction worker stack. The depth guard (commit `dea8a4a`) is the backstop; *this* is the root
  fix — that file should never have been walked.

Both the one-shot detector (`detect.cpp:123`) and the daemon watcher (`file_watcher.cpp:80`) route
through the same `path_ignore.cpp`, so a single module change fixes cold build and incremental
updates together.

## What Changes

- Extend `is_skipped_directory` with the Python-ecosystem dependency/tooling category:
  `.venv`, `venv`, `site-packages`, `__pycache__`, `.tox`, `.nox`, `.pytest_cache`, `.mypy_cache`,
  `.ruff_cache`, `.hypothesis`, `.eggs`. (`site-packages` catches venv *contents* regardless of the
  venv directory's name.)
- Add a structural check: a directory containing a `pyvenv.cfg` marker is a virtualenv root and is
  skipped, catching venvs with non-standard names (`qa-env`, `.direnv/python-3.13`, …) that a
  name-only list would miss. This requires a path-aware predicate (the existing
  `is_skipped_directory` sees only the leaf name), shared by both `detect.cpp` and
  `file_watcher.cpp`.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `deterministic-graph-pipeline`: project file detection SHALL exclude dependency and
  virtual-environment trees (by name and by `pyvenv.cfg` marker), and the daemon watcher SHALL
  apply the identical exclusion so incremental updates never re-add a skipped file.

## Non-Goals

- **Full `.gitignore` semantics (Phase 2, separate change).** Today only the *root* `.gitignore` is
  read and `matches_simple_gitignore` has no glob support — so the existing `*-source-graph/`
  pattern in this repo's own `.gitignore` matches nothing. Honoring nested `.gitignore` files
  (Python writes one containing `*` into every venv, so venvs would self-exclude for free) plus
  globs/negation is the principled general fix, but its matching semantics are subtle and out of
  scope here.
- **An override to force-include a skipped tree** (e.g. to trace into a vendored dependency).
  Current behavior skips dependency trees unconditionally; this change preserves that. A future
  `--include` / config seam is noted but not built.
- **Changing extraction, ID normalization, or `graph.json` shape** — parity is held for every file
  that is still indexed.

## Impact

- `src/engine/path_ignore.cpp` / `path_ignore.hpp` (name list + path-aware `pyvenv.cfg` predicate),
  `src/engine/detect.cpp` and `src/engine/file_watcher.cpp` (call the path-aware predicate),
  `tests/smoke/path_ignore_test.cpp`.
- **Behavior shift:** any repo with a venv produces a smaller, cleaner graph. The cgraph repo graph
  drops from ~39,142 to ~1,200 nodes.
- **Parity:** detection scope is not a parity surface (the contract covers fragment shape, ID
  normalization, and node-link output). The parity fixtures (`extractor_goldens`,
  `pack_context_parity`) contain no venv, so goldens are unaffected — to be confirmed by running
  them.
- Verified by: per-category skip unit tests + a `pyvenv.cfg` structural case + a lookalike
  negative case; end-to-end the cgraph graph drops to real-code size with no `source_file` under
  `.venv`/`site-packages`; full suite; parity goldens unchanged.
