## Test strategy

The exclusion lives in one shared module, so unit tests on `is_skipped_directory` cover detection,
the watcher, and the enrichment planner; an `enrich-plan` re-run confirms the planner benefit.

- **`path_ignore_test.cpp` (red first):** each new name (`.claude`, `.codex`, `.gemini`, `.cursor`,
  `.factory`, `.opencode`, `.windsurf`, `.aider`, `.specify`) is skipped; **over-skip negatives** —
  the non-dotted lookalikes (`claude`, `factory`, `specify`, `cursor` as ordinary source dir names)
  are NOT skipped (exact-match on the dotted name, so a real `factory/` package is safe).
- **Enrichment-planner check:** `cgraph enrich-plan` on a repo containing `.claude`/`.factory`/
  `.specify` no longer lists docs under those trees (the planner shares `is_skipped_directory`).
- **Parity regression:** `extractor_goldens` + `pack_context_parity` unchanged (no such dirs in
  fixtures).

## Decisions

- **Name-based exact match, not a marker.** Unlike virtualenvs (arbitrary names → `pyvenv.cfg`
  marker), agent-CLI config dirs are a small set of fixed, dotted, distinctive names. Exact-name
  skip is sufficient and cannot over-match a legitimately-named source directory.
- **Which names.** The current AI-agent CLIs and the spec-kit tool: `.claude`, `.codex`, `.gemini`,
  `.cursor`, `.factory`, `.opencode`, `.windsurf`, `.aider`, `.specify`. (`.agents`, `.idea`,
  `.vscode` are already skipped.) The set is dotted-only, matching how these tools name their dirs.
- **`.github` deliberately excluded.** It is VCS/CI config but routinely carries real project
  content (CONTRIBUTING, docs, templates); skipping it would drop legitimate docs. Left in scope.
- **Reuse, don't restructure.** This extends the same skip-list the venv change used; no new
  predicate, no call-site changes (detection/watcher/planner already call `is_skipped_directory`).

## Validation that cannot be tested directly

- The cross-project enrichment-noise reduction (the 38% figure) is specific to backend's current
  tree; the durable assertions are the per-name unit skips and the planner no longer emitting docs
  under these dirs, not a fixed percentage.
