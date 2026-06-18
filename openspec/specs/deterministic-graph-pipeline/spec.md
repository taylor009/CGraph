# deterministic-graph-pipeline Specification

## Purpose
TBD - created by archiving change improve-graph-html-view. Update Purpose after archive.
## Requirements
### Requirement: Interactive HTML view reveals community structure
The interactive `graph.html` export SHALL position nodes so that computed community assignments are visible as spatially distinct regions, rather than using community only for color while leaving the layout a uniform frame-filling cloud. The layout SHALL remain deterministic for a given graph (no use of `Math.random`).

#### Scenario: Communities render as separated regions
- **WHEN** the pipeline exports `graph.html` for a graph with multiple detected communities and the view settles
- **THEN** nodes of the same community are drawn closer to one another than to nodes of other communities, so distinct communities read as separate regions

#### Scenario: Layout is not clamped to a viewport box
- **WHEN** the layout settles for a graph larger than the viewport
- **THEN** nodes relax in open space without piling against fixed canvas edges, and the view auto-fits the whole graph so it is visible on load

#### Scenario: Layout is deterministic
- **WHEN** `graph.html` is generated twice for the same graph
- **THEN** the generated layout logic uses only seeded placement (no `Math.random`), so the same graph produces the same layout each load

### Requirement: Interactive HTML view bounds on-screen labels
The interactive `graph.html` export SHALL limit always-on node labels to a bounded set (the highest-degree nodes) and SHALL reveal additional labels progressively on hover, selection, active highlight, search match, and zoom-in, so an overview of a large graph is not an unreadable wall of overlapping text.

#### Scenario: Overview labels are bounded
- **WHEN** `graph.html` is exported for a graph with hundreds of nodes and viewed at the default zoom with no selection or search
- **THEN** only a bounded top-by-degree subset of nodes is labelled, not every node above a fixed radius

#### Scenario: Hidden labels are reachable
- **WHEN** the user hovers, selects, searches, or zooms into a node whose label is hidden in the overview
- **THEN** that node's label becomes visible, so no label is permanently inaccessible

### Requirement: Interactive HTML view selection is reversible
The interactive `graph.html` export SHALL let the user return to the unfocused full-graph view without reloading the page. Clicking empty canvas and pressing `Escape` SHALL clear the current selection and highlight, and the view SHALL provide controls to reset the view and to fit the whole graph to the viewport.

#### Scenario: Escape clears selection
- **WHEN** a node is selected (everything else dimmed) and the user presses `Escape`
- **THEN** the selection and highlight clear and the full graph is shown undimmed

#### Scenario: Clicking empty canvas clears selection
- **WHEN** a node is selected and the user clicks an empty area of the canvas
- **THEN** the selection clears before any pan begins, restoring the unfocused view

#### Scenario: Fit to screen recenters the graph
- **WHEN** the user has panned or zoomed away and activates the fit-to-screen control
- **THEN** the view recenters and scales so the whole graph's bounding box is visible within the viewport

### Requirement: Interactive HTML view supports light and dark themes
The interactive `graph.html` export SHALL render in both a light and a dark theme, defaulting to the operating system's color-scheme preference and providing a control to switch themes. The canvas (background, node strokes, edges, and labels) SHALL follow the active theme, not only the surrounding DOM chrome.

#### Scenario: Theme follows OS preference by default
- **WHEN** `graph.html` is opened with no explicit theme chosen and the OS prefers a dark color scheme
- **THEN** the view renders in dark theme, including the graph canvas

#### Scenario: User can switch themes
- **WHEN** the user activates the theme toggle
- **THEN** the page and the graph canvas switch between light and dark, with node, edge, and label colors updating to remain legible

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

### Requirement: SQL files are indexed as file-level nodes
Project file detection SHALL recognize the `.sql` extension as a known language
(`DetectedLanguage::Sql`), so `.sql` files are classified as code (detected, extracted, and watched
for incremental updates) rather than as enrichment-only documents. For each detected `.sql` file the
deterministic extractor SHALL emit exactly one file-level node with `kind = "sql_file"`, a label
derived from the file name, and the file's `source_file` set — with no symbol nodes and no edges
(file-level only; SQL contents are not parsed). The node SHALL be queryable, enrichable, and
seam-anchorable like any other graph node.

#### Scenario: A SQL file produces one file-level node
- **WHEN** the graph is built over a project containing `.sql` files (e.g. Prisma migrations)
- **THEN** each `.sql` file contributes exactly one node of kind `sql_file` whose `source_file` is
  that file, and no symbol nodes or edges are emitted for it

#### Scenario: SQL files are discoverable
- **WHEN** an agent queries the graph for SQL files (e.g. by kind `sql_file` or a `file:` path match)
- **THEN** the `sql_file` nodes are returned, so the project's data layer is visible in the graph

#### Scenario: Extraction parity is preserved
- **WHEN** the extractor goldens (which contain no `.sql` fixtures) are regenerated
- **THEN** they are unchanged — indexing `.sql` is additive and does not alter extraction of any
  existing language

### Requirement: SQL DDL is extracted into a schema graph
Beyond the file-level `sql_file` node, the deterministic extractor SHALL parse the DDL in `.sql`
files into schema nodes and relationships:

- `CREATE TABLE "<name>"` SHALL emit a `sql_table` node, and `CREATE TYPE "<name>" AS ENUM` a
  `sql_enum` node. Their ids SHALL be keyed on the entity **name** (independent of the source file),
  so the same table or enum appearing across multiple migration files merges — via the graph
  builder's id dedup — into a single node representing the current schema. The node SHALL record the
  `source_file` and location of its defining `CREATE` statement.
- A foreign key `ALTER TABLE "<X>" … FOREIGN KEY (…) REFERENCES "<Y>"` SHALL emit a `references`
  edge from the `sql_table` node for `X` to the `sql_table` node for `Y` (reusing the existing
  `references` relation, so `impact` / typed `explain` / query routing operate over it). Duplicate
  foreign keys across migrations SHALL collapse via edge dedup.

Extraction is regex-based over the (Prisma-style) DDL; SQL is not fully parsed. Forms not matched
(e.g. inline column-level references, non-Prisma dialects) SHALL simply yield no edge rather than an
error.

#### Scenario: Tables, enums, and foreign keys become a schema graph
- **WHEN** the graph is built over `.sql` migrations declaring `CREATE TABLE`, `CREATE TYPE … ENUM`,
  and `ALTER TABLE … FOREIGN KEY … REFERENCES` statements
- **THEN** the graph contains a `sql_table` node per table, a `sql_enum` node per enum, and a
  `references` edge between the owning and referenced tables for each foreign key

#### Scenario: A table merges across migrations into one node
- **WHEN** the same table is created in one migration file and altered in others
- **THEN** the graph contains exactly one `sql_table` node for it (its id keyed on name, not file),
  with the foreign keys added by later migrations attached as `references` edges

#### Scenario: Schema is queryable via existing relation-aware ops
- **WHEN** an agent runs `impact` or `explain --relation references` on a `sql_table` node
- **THEN** the response returns the tables related by foreign keys, because the foreign keys are
  `references` edges the existing ops already traverse

