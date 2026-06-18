## Why

A generalization run against real target repos (`research/generalization/results.md`) measured that
**3,268 of project-anvil's 4,868 graph nodes (67%) are duplicate copies inside
`.anvil/worktrees/<uuid>/`** — agentic git worktrees, each a full checkout of the same repo. They are
not project source; they are transient duplicate checkouts that:

- **inflate the graph ~3x** (4,868 nodes where the real source is ~1,600),
- **pollute retrieval** — duplicate symbols compete for focal resolution and gather slots (a warm-up
  query in the run resolved its focal *into* a worktree copy, `services/.../agentpool`), depressing
  recall on an otherwise-clean repo,
- **would pollute enrichment** — every worktree doc is a candidate chunk-plan entry.

`is_dependency_directory` (`path_ignore.cpp`) already skips dependency/venv/tooling trees by name and
detects oddly-named virtualenvs structurally via a `pyvenv.cfg` marker. Linked git worktrees are the
direct analog and a missing category — but they cannot be caught by name (the checkout dirs are
uuid-named, and the parent dir is tool-specific: `.anvil`, `.agents`, `.cybertron`, …). They have an
caught by two **structural** markers instead (name-independent), because tools leave both live and
stale checkouts behind:
1. A LIVE linked worktree's `.git` is a *regular file* (`gitdir: …/.git/worktrees/<id>`), whereas a
   real repo root's `.git` is a *directory* — catches worktrees parked anywhere, never the root.
2. Tools park checkouts under the `<.tool>/worktrees/<id>/` convention (`.anvil/worktrees/…`,
   `.agents/worktrees/…`). A `worktrees` directory under a *dotted* parent is that convention, and
   skipping it also drops STALE checkouts whose `.git` has been pruned (the majority in anvil — 4 of
   6 worktrees). The dotted-parent guard leaves a legitimate `src/worktrees/` module untouched.

## What Changes

- Add the two structural worktree markers above to `is_dependency_directory`, beside the existing
  `pyvenv.cfg` probe — name-independent, probed once per directory entered.
- Apply the same exclusion in the **enrichment chunk planner** (`semantic_chunk_plan.cpp`), which
  today gates only on the name-based `is_skipped_directory` and so would otherwise still plan
  worktree docs. It now gates on `is_dependency_directory(path)` (which subsumes the name list).

## Capabilities

### Modified Capabilities

- `deterministic-graph-pipeline`: the detection-exclusion requirement is broadened so detection, the
  daemon watcher, and the enrichment planner also skip git-worktree checkout trees, detected by a
  worktree `.git`-regular-file marker or a `worktrees` directory under a dotted parent (never the
  project root's `.git` directory, never a non-dotted `worktrees/` source module).

## Non-Goals

- **Unguarded name-based worktree skipping** (skip *any* dir literally named `worktrees`, anywhere)
  — rejected; it would over-skip a legitimate `src/worktrees/` source module. The `worktrees` rule
  used here is guarded by a *dotted parent*, so it matches only the `<.tool>/worktrees/` tool
  convention.
- **Skipping `.anvil` / other tool-state dirs wholesale by name** — unnecessary; the structural
  worktree marker catches their worktree trees, and blanket-skipping arbitrary dotted dirs risks
  dropping real content (the same reasoning that kept `.github` in scope).
- **Removing already-indexed worktree nodes from existing graphs** — a rebuild drops them naturally;
  no migration step.

## Impact

- `src/engine/path_ignore.cpp` (two structural worktree markers in `is_dependency_directory`),
  `src/engine/semantic_chunk_plan.cpp` (gate recursion on `is_dependency_directory`),
  `tests/smoke/path_ignore_test.cpp`.
- Detection (`detect.cpp:122`) and the watcher (`file_watcher.cpp:79`) already call
  `is_dependency_directory(path)`, so both are cleaned by the addition.
- **Measured effect (project-anvil):** leaked worktree nodes 3,268 → **0**; total nodes 4,868 →
  1,633; files scanned 883 → 287. Full suite 59/59, parity goldens 2/2.
- **Parity:** detection scope is not a parity surface; fixtures contain no git worktrees (goldens
  unaffected, to be confirmed by running them).
- Verified by: structural-marker unit tests (worktree `.git`-file skipped; real-root `.git`-dir NOT
  skipped; name list unaffected); a rebuilt anvil graph confirming the node-count drop; full suite.
