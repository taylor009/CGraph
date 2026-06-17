## Test strategy

The exclusion logic lives in one module (`path_ignore.cpp`) consumed by both detection and the
watcher, so unit tests on that module cover both code paths' decision logic; an end-to-end build
confirms the wiring.

- **`path_ignore_test.cpp` (unit, red first):**
  - each new name (`.venv`, `venv`, `site-packages`, `__pycache__`, `.tox`, `.nox`,
    `.pytest_cache`, `.mypy_cache`, `.ruff_cache`, `.hypothesis`, `.eggs`) is skipped;
  - a directory containing a `pyvenv.cfg` is skipped even when its name is not in the list
    (e.g. `qa-env/`);
  - **negative / over-skip guard:** a real source directory whose name merely *contains* a skipped
    token (e.g. `my_env_utils/`, `environment/`) is NOT skipped, and a `site_packages` *file* or a
    dir without `pyvenv.cfg` is not structurally skipped.
- **End-to-end (the real proof):** build the cgraph repo graph and assert (a) node count drops to
  real-code scale (~1.2k, not ~39k) and (b) no node has a `source_file` under `.venv/` or
  `site-packages/`.
- **Watcher consistency:** the watcher shares the predicate, so a file created under a skipped tree
  after the initial build must not surface as an incremental update. Covered by reasoning (shared
  module) plus, if practical, a watcher test that touches a file under a `pyvenv.cfg` dir and
  asserts no event.
- **Parity regression:** run `extractor_goldens` and `pack_context_parity` — must be unchanged
  (fixtures contain no venv).

## Decisions

- **Static name list + structural `pyvenv.cfg`, not full gitignore (Phase 1).** The name list is
  immediate and zero-risk; `site-packages` by name catches venv *contents* whatever the venv is
  called; `pyvenv.cfg` catches the venv *root* even when oddly named. Together this is robust for
  the venv category without taking on gitignore-semantics complexity. Full `.gitignore` handling
  (nested files, globs, negation) is deferred to a separate Phase 2 change — see Non-Goals.

- **Path-aware predicate.** `is_skipped_directory(name)` sees only the leaf name and cannot probe
  for `pyvenv.cfg`. Introduce `is_dependency_directory(const std::filesystem::path& dir)` that
  returns true on a name match OR when `dir / "pyvenv.cfg"` exists, and call it from the two
  directory-skip sites (`detect.cpp`, `file_watcher.cpp`). Keep `is_skipped_directory(name)` as the
  fast name-only inner check. The `pyvenv.cfg` `exists()` probe runs once per directory entered
  during the walk (not per file) — negligible.

- **Conservative names only.** Include `site-packages`/`__pycache__`/`.venv`/`venv` and the cache
  dirs; exclude ambiguous names like bare `env`, `bin`, `obj`, `lib` that frequently hold real
  source. Oddly-named venvs are caught structurally instead, so the name list can stay safe.

- **No override knob.** Matches today's unconditional skip of `node_modules`/`vendor`/etc.; adding
  a force-include seam is a separate concern (Non-Goals).

## Validation that cannot be tested directly

- The *absolute* node-count target (~1.2k) depends on the current cgraph tree and will drift as the
  repo changes; the durable assertions are the within-build invariants (no `source_file` under a
  skipped tree; count strictly lower than a build that includes the venv), not a fixed number.
