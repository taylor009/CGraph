## MODIFIED Requirements

### Requirement: Detection excludes dependency and virtual-environment trees
Project file detection SHALL skip dependency, build, tooling, virtual-environment,
agent-tooling-config, and linked git-worktree directory trees rather than index their contents as
project source. A directory SHALL be excluded when its name is in the skip list — which includes the
Python ecosystem (`.venv`, `venv`, `site-packages`, `__pycache__`, `.tox`, `.nox`, `.pytest_cache`,
`.mypy_cache`, `.ruff_cache`, `.hypothesis`, `.eggs`) and the agent-CLI / spec-tool config
directories (`.claude`, `.codex`, `.gemini`, `.cursor`, `.factory`, `.opencode`, `.windsurf`,
`.aider`, `.specify`) — OR when it contains a `pyvenv.cfg` virtual-environment marker — OR when it is
a git-worktree checkout tree. A worktree checkout SHALL be detected by either of two structural
markers: (a) a `.git` entry that is a regular file (the live worktree `gitdir:` marker) rather than a
directory, or (b) a directory named `worktrees` whose parent directory name begins with a dot (the
`<.tool>/worktrees/` convention used by agent tools, which also covers stale checkouts whose `.git`
has been pruned). The project root's own `.git` is a directory and SHALL NOT trigger this exclusion,
and a `worktrees` directory under a non-dotted parent (a legitimate source module) SHALL NOT be
excluded. The daemon file watcher SHALL apply the identical exclusion, so a file created under a
skipped tree never produces an incremental update. The same exclusion governs the enrichment chunk
planner, so neither agent-tooling docs nor worktree-duplicated docs are planned for semantic
enrichment. Files that remain in scope are extracted unchanged (parity is held).

#### Scenario: Virtualenv contents are not indexed
- **WHEN** a project root contains a virtualenv (e.g. `research/.venv/lib/pythonX/site-packages/…`)
  and the graph is built
- **THEN** no node has a `source_file` under that `.venv` or any `site-packages` directory, and the
  graph contains only the project's own source

#### Scenario: Oddly-named virtualenv is detected by marker
- **WHEN** a directory not in the name skip list (e.g. `qa-env/`) contains a `pyvenv.cfg` file
- **THEN** the directory and its contents are skipped during detection

#### Scenario: Agent-tooling config directories are not indexed or enriched
- **WHEN** a project root contains agent-CLI or spec-tool config trees (e.g. `.claude/commands/`,
  `.factory/skills/`, `.specify/templates/`)
- **THEN** detection skips them and the enrichment planner does not plan their documents, so they
  contribute neither code nodes nor doc nodes

#### Scenario: Live and stale git-worktree trees are not indexed
- **WHEN** a project root contains agentic git worktrees under the `<.tool>/worktrees/<id>/`
  convention (e.g. `.anvil/worktrees/<uuid>/…`, `.agents/worktrees/<slug>/…`) — some live (`.git` is
  a regular file) and some stale (the `.git` pointer already pruned, only the duplicated source tree
  remaining) — and the graph is built
- **THEN** no node has a `source_file` under any worktree checkout tree (live or stale), and the
  graph contains only the project's own (single-copy) source

#### Scenario: Project root is never skipped as a worktree
- **WHEN** detection enters the project root, whose `.git` is a directory (a normal repository)
- **THEN** the root is walked normally and its source files are indexed; the `.git` marker only
  matches a regular file, and the convention marker only matches a `worktrees` dir under a dotted
  parent

#### Scenario: A legitimate source module named `worktrees` is not skipped
- **WHEN** a project has a real source directory literally named `worktrees` under a non-dotted
  parent (e.g. `packages/core/src/worktrees/`) with no worktree `.git` marker
- **THEN** it is walked normally and its source files are indexed

#### Scenario: Lookalike directory is not over-skipped
- **WHEN** a real source directory has a name that merely contains a skipped token, or matches a
  skipped name without its leading dot (e.g. `my_env_utils/`, `environment/`, `factory/`), and has
  neither a `pyvenv.cfg` nor a `.git` worktree-marker file
- **THEN** it is walked normally and its source files are indexed

#### Scenario: Watcher and detector agree
- **WHEN** the daemon is running and a recognized source file is created under a skipped tree
- **THEN** the watcher emits no event and the resident graph is unchanged, matching what a cold
  detection pass would have produced

#### Scenario: Pathologically deep generated dependency cannot crash the build
- **WHEN** a repo contains a machine-generated dependency file deep in a skipped tree (e.g. a
  NumPy header under `site-packages`) that previously triggered an extraction stack overflow
- **THEN** detection never walks it, so it cannot contribute nodes or destabilize the build
