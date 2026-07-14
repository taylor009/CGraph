# Proposal: host-enrichment-driver

## Why

The semantic-enrichment system is complete on BOTH sides — and has still never run to
completion anywhere, because the host half is distributed nowhere and triggered by nothing.

Engine side (built, tested): chunk plans with `candidate_links` (`semantic_chunk_plan.cpp`),
drop watcher (`semantic_drop.cpp`), fragment validation, single-writer ingest, content-hash
cache, async plan refresh, connectivity metrics. The native flow plan → fragment → validate →
ingest → cache-hit is integration-tested (`tests/smoke/host_surface_integration_test.cpp:75`).

Host side (built, orphaned): a complete driver skill exists at
`integrations/skills/cgraph-enrich/SKILL.md` ("This skill is the loop that produces them",
:11) alongside the query-routing skill `integrations/skills/cgraph/SKILL.md`. Neither is
installed in any host skill path (`~/.claude/skills/`, shared `~/.agents` skills); the only
install story is one manual README sentence that names only the `cgraph` skill
(`README.md:261-263`). No hook or schedule invokes the loop.

Live consequence (observed 2026-07-12): this repo's daemon reports
`enrichment_state: "pending"` with `enrichment_pending: 72`; the supervisor-tracked
production repo `full-turing/backend` reports `enrichment_pending: 128` with an empty
semantic layer. The only fragments ever produced were hand-authored 2026-06-10..17
(file mtimes in `cgraph-out/semantic-drop/`).

Two blinded verifiers on different providers converged on this diagnosis and priority
(see design.md, "Verification consensus").

## What changes

1. **`cgraph skills <install|status|uninstall>` CLI verb**, mirroring the existing
   `cgraph daemon <install|sync|status|uninstall>` surface (`src/cli/main.cpp:523,631`).
   `install` symlinks the canonical `integrations/skills/cgraph` and
   `integrations/skills/cgraph-enrich` into `~/.claude/skills/` and `~/.agents` shared
   skills; `status` reports per-path link presence/target; idempotent; symlink (never copy)
   so the committed source stays canonical.
2. **Two-layer trigger.**
   - Layer A (interactive, free): installation alone — the enrich skill's frontmatter
     already self-triggers "when graph_status shows enrichment_pending > 0"
     (`integrations/skills/cgraph-enrich/SKILL.md:3`).
   - Layer B (autonomous, bounded): a scheduled headless drainer LaunchAgent reusing the
     supervisor's tracked-repo discovery (`daemon_supervisor.cpp` — repos whose `.mcp.json`
     registers cgraph), which per repo: checks `status.enrichment_pending`, exits free if 0,
     otherwise runs the enrich loop headless on Sonnet with a per-run chunk cap.
3. **Drain as verification.** Run the loop for real on this repo (72 pending) and on
   `full-turing/backend` (128 pending), recording before/after `semantic` status blocks,
   then prove idempotency (re-plan reports 0 inputs) and stale re-entry (edit one doc).
4. **Docs**: `docs/host-skill-contract.md` names the reference driver;
   `README.md` install section replaces "copy it" with the verb.

## Impact

- `semantic-fragment-ingest` is exercised end to end on real repos for the first time.
- Native surface grows by one host-tooling verb: no graph-output changes, no Graphify-parity
  risk. New engine code gets 1:1 smoke tests per convention.
- Model spend stays host-owned and bounded: status-gated (zero steady-state cost), Sonnet
  routing (per the reduce-opus-cost-routing precedent), chunk-capped, cache-idempotent.

## Non-goals

- No daemon-side candidate-link computation (stays deferred per
  `archive/2026-07-12-plan-candidate-code-links/design.md:111-112`).
- No scheduler or model dispatch inside the daemon/binary — the drainer LaunchAgent shells
  out to the host CLI; the binary keeps zero model logic (contract line 3).
- No Linux/systemd drainer yet — macOS first, same staging as the daemon supervisor.
- No media transcription (contract scopes media out of required host work).
