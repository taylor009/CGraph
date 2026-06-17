## ADDED Requirements

### Requirement: Detection excludes dependency and virtual-environment trees
Project file detection SHALL skip dependency, build, tooling, and virtual-environment directory
trees rather than index their contents as project source. A directory SHALL be excluded when its
name is in the skip list (which includes the Python ecosystem: `.venv`, `venv`, `site-packages`,
`__pycache__`, `.tox`, `.nox`, `.pytest_cache`, `.mypy_cache`, `.ruff_cache`, `.hypothesis`,
`.eggs`) OR when it contains a `pyvenv.cfg` virtual-environment marker. The daemon file watcher
SHALL apply the identical exclusion, so a file created under a skipped tree never produces an
incremental update. Files that remain in scope are extracted unchanged (parity is held).

#### Scenario: Virtualenv contents are not indexed
- **WHEN** a project root contains a virtualenv (e.g. `research/.venv/lib/pythonX/site-packages/…`)
  and the graph is built
- **THEN** no node has a `source_file` under that `.venv` or any `site-packages` directory, and the
  graph contains only the project's own source

#### Scenario: Oddly-named virtualenv is detected by marker
- **WHEN** a directory not in the name skip list (e.g. `qa-env/`) contains a `pyvenv.cfg` file
- **THEN** the directory and its contents are skipped during detection

#### Scenario: Lookalike directory is not over-skipped
- **WHEN** a real source directory has a name that merely contains a skipped token (e.g.
  `my_env_utils/`, `environment/`) and has no `pyvenv.cfg`
- **THEN** it is walked normally and its source files are indexed

#### Scenario: Watcher and detector agree
- **WHEN** the daemon is running and a recognized source file is created under a skipped tree
- **THEN** the watcher emits no event and the resident graph is unchanged, matching what a cold
  detection pass would have produced

#### Scenario: Pathologically deep generated dependency cannot crash the build
- **WHEN** a repo contains a machine-generated dependency file deep in a skipped tree (e.g. a
  NumPy header under `site-packages`) that previously triggered an extraction stack overflow
- **THEN** detection never walks it, so it cannot contribute nodes or destabilize the build
