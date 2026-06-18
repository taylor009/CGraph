## Why

Enriching the backend project surfaced the venv lesson again, one layer up: of its 174 plannable
docs, **66 (38%) are agent-tooling config** — `.claude/commands/`, `.factory/skills/`,
`.specify/templates/` — not project source or documentation. They become isolated, low-value doc
nodes that dilute the semantic layer, and any code they contained would pollute the code graph too.

`is_skipped_directory` (`path_ignore.cpp`) already skips editor/tooling config (`.idea`, `.vscode`,
`.agents`) and the Python ecosystem — agent-CLI config dirs are the direct analog and a missing
category. Because the same predicate is shared by detection (`detect.cpp`), the daemon watcher
(`file_watcher.cpp`), **and the enrichment chunk planner** (`semantic_chunk_plan.cpp`), one skip-list
addition cleans the code graph, incremental updates, and every enrichment plan across all projects —
before we enrich the remaining ones (cybertron, turing-agents, frontend).

## What Changes

- Add the well-known agent-CLI / spec-tool config directories to `is_skipped_directory`:
  `.claude`, `.codex`, `.gemini`, `.cursor`, `.factory`, `.opencode`, `.windsurf`, `.aider`,
  `.specify`. Name-based exact match (these are fixed, dotted, distinctive names — no structural
  marker needed, unlike venvs).

## Capabilities

### Modified Capabilities

- `deterministic-graph-pipeline`: the detection-exclusion requirement is broadened so project file
  detection (and the shared enrichment planner) also skip agent-tooling config directory trees.

## Non-Goals

- **`.github`** — left in scope. It is genuine VCS/CI config that often holds real content
  (`CONTRIBUTING.md`, docs, issue templates); excluding it risks dropping legitimate project docs.
- **Removing already-enriched tooling doc nodes** from existing graphs — a re-plan / rebuild drops
  them naturally; no migration step.
- **Structural detection** (a marker file) — unnecessary; these are fixed, well-known names, so an
  exact-name skip is sufficient and avoids over-skipping.

## Impact

- `src/engine/path_ignore.cpp` (skip-list addition), `tests/smoke/path_ignore_test.cpp`.
- Cleans `detect.cpp`, `file_watcher.cpp`, and `semantic_chunk_plan.cpp` (all share the predicate) at
  once. The backend enrichment plan would drop from 174 to ~108 docs (the 66 tooling docs removed).
- **Parity:** detection scope is not a parity surface; fixtures contain no such dirs (goldens
  unaffected, to be confirmed by running them).
- Verified by: per-name skip unit tests + non-dotted-lookalike negatives; a re-run `enrich-plan` on a
  repo with `.claude`/`.factory`/`.specify` confirms those docs are no longer planned; full suite.
