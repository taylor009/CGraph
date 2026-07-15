# Design: host-enrichment-driver

## Shape

Both halves of the enrichment system already exist; the work is distribution + activation +
proof:

```
canonical source (this repo)                    host machine
  integrations/skills/cgraph            ──►   ~/.claude/skills/  + ~/.agents shared skills
  integrations/skills/cgraph-enrich     ──►   (symlinks, via `cgraph skills install`)

triggers
  Layer A (interactive, free):   installed skill self-triggers when
                                 graph_status shows enrichment_pending > 0
  Layer B (autonomous, bounded): drainer LaunchAgent (daily StartInterval)
                                   for each tracked repo (supervisor discovery):
                                     status.enrichment_pending == 0 ? exit (free)
                                     : headless host CLI run of the enrich loop
                                       (Sonnet, per-run chunk cap)

the loop itself (already written, integrations/skills/cgraph-enrich/SKILL.md):
  cgraph enrich-plan → author one fragment per chunk (candidate_links as evidence)
  → atomic tmp+rename drop → daemon watcher or enrich-ingest merges
  → re-plan until 0 inputs; failed chunks re-dropped under the same index
```

## Decisions

- **`cgraph skills install` verb, not manual copy or a script.** The repo keeps its ops
  surface on the binary (`cgraph daemon install/sync/status/uninstall`,
  `src/cli/main.cpp:523-587`); the verb is discoverable via `--help` and smoke-testable.
  The manual README copy is the status quo that produced "installed nowhere" — and it
  documents only one of the two skills (`README.md:261-263`).
- **Symlink, never copy.** Copies drift from the committed source the moment a SKILL.md is
  edited; symlinks make the repo canonical everywhere at once.
- **Drainer reuses the supervisor, not a new subsystem.** Tracked-repo discovery
  (`.mcp.json` registers cgraph), launchd rendering (`launch_agent.cpp`), and per-root
  hashing exist and are tested — so the drainer set equals the resident-daemon set by
  construction. The LaunchAgent's ProgramArguments invoke the HOST CLI headless; the binary
  keeps zero model logic (`docs/host-skill-contract.md:3`).
- **Spend bounds, four layers.** Pre-spawn `status` gate (steady-state cost is zero);
  Sonnet routing for authoring ("knowledge extracted from the files you read, not
  invention" — mechanical work, per the reduce-opus-cost-routing precedent); per-run chunk
  cap (plans accumulate across passes by design); content-hash cache idempotency.
- **Verification is the drain itself.** Two real repos with recorded before/after
  `semantic` blocks; the native half is already integration-tested
  (`tests/smoke/host_surface_integration_test.cpp:75`), so no synthetic fixture is needed.

## Verification consensus (blinded swarm, different providers)

- **codex (blind priority ranking):** independently ranked "automate the host enrichment
  loop" #1 of all candidates, and corrected the survey premise — "a host integration does
  exist at integrations/skills/cgraph-enrich/SKILL.md ... What is missing is automatic
  execution of that documented loop." Its scope sketch matches: reuse the native pipeline,
  keep concurrency/retries/spend caps host-side, gate on the real
  pending→drain→cache-hit→stale→reject cycle.
- **droid (blind install/trigger design):** independently recommended the
  `cgraph skills install` verb mirroring `cgraph daemon install`, symlinks into both host
  skill paths, the two-layer trigger, supervisor-discovery reuse for the drainer, and the
  four spend bounds. Rejected: manual README copy (status quo), standalone script
  (ops surface belongs on the binary), MCP-served skills (MCP adds no model logic),
  session-start auto-run (unbounded spend), scheduler inside the daemon (contract
  violation).
- **Divergence reconciled:** my first draft proposed writing a NEW skill — wrong, the
  skill exists but is installed nowhere (both verifiers caught the reshape). My second
  draft proposed symlink-install-by-hand with no CLI verb; droid's convention argument
  (ops on the binary, testable, idempotent status/uninstall lifecycle) won. Codex proposed
  a generic "host dispatcher"; droid's supervisor-reuse makes the drainer set equal the
  daemon set by construction — adopted.

## Open questions (settle during implementation)

- Drainer cadence and per-run chunk cap defaults (proposal: daily, ~10 chunks/repo/run).
- Whether `skills` verb logic lives in `src/cli/main.cpp` only or gets an engine helper
  (then 1:1 test obligation applies; `launch_agent.cpp` currently lacks one — don't repeat
  that).
- Exact headless invocation for Layer B (`claude -p` vs `agents run`) — host detail, not a
  contract.

## Measured results (2026-07-12, this machine)

- Install: `cgraph skills install` created 4 symlinks (`~/.claude/skills/{cgraph,cgraph-enrich}`,
  `~/.agents/skills/{cgraph,cgraph-enrich}`); fresh headless sessions resolved the skills in both
  this repo and `full-turing/backend` (Skill tool loaded `cgraph-enrich` by name).
- This repo drained: `enrichment_pending` 72 -> 0, `enrichment_failed` 0. Semantic block
  before -> after: doc_nodes 122 -> 194, concept_nodes 18 -> 58, doc_code_edges 525 -> 836,
  connectivity_rate 0.811 -> 0.866. 10 chunks (76 inputs), all fragments accepted first try.
- Backend drained: `enrichment_pending` 128 -> 0, `enrichment_failed` 0. Semantic layer from
  empty: doc_nodes 0 -> 120, concept_nodes 0 -> 53, doc_code_edges 0 -> 469,
  connectivity_rate 0.858. 16 chunks, all accepted. `explain` on
  `doc:change-collaboration-service-v1-spec-collaboration-access` returns `DOCUMENTS ->
  sql_table_collab_access` (src/db/migrations/0037_collaboration_schema.sql:37).
- Idempotency: re-plan after each drain reports `0 chunk(s) ... 184 cache hit(s)` (this repo)
  and `128 cache hit(s)` (backend).
- Stale re-entry: a real edit to this change's tasks.md re-entered the next plan as exactly
  1 stale input and drained through drop + `enrich-ingest`.
- Sharp edge (documented, by test): re-running `enrich-plan` between drop and merge offsets the
  expected fragment name, orphaning the earlier drop — the skill's "always read the fresh plan
  and use the names it gives" rule is load-bearing. Merge (watcher or `enrich-ingest`) before
  re-planning.
- launchd gap found and fixed: under launchd's minimal PATH the host CLI did not resolve and the
  sweep exited 0 silently. Fixes: login-shell PATH resolution in the drain script + a
  `log_path` (StandardOut/ErrorPath) on the drainer LaunchAgent spec.

## Measured results (2026-07-14, this machine — tasks 5-7)

- Staleness on the production repo (task 5): one visible-text edit to
  `backend/v2-collaboration-service-design.md` re-entered the next plan as exactly
  `cache_hits: 127, stale_inputs: 1, chunks: 1` (fragment `chunk_16.json`, offset past the 16
  existing fragments). Drained: `17 fragment(s) merged, 0 rejected`, nodes 3413 -> 3591.
  Reverting the edit restored a clean plan (`128 cache hit(s), 0 stale`) — the cache retains
  the prior hash, so revert costs nothing.
- Invocation sharp edge (operator error, documented): `--out` is cwd-relative; running
  `enrich-plan --root <repo>` from another directory writes `plan.json`/stat-index into the
  *caller's* `cgraph-out` and reads the wrong cache, reporting everything as a cache hit. Run
  from the repo root exactly as the skill says (`--root . --out cgraph-out`).
- Second launchd gap found and fixed (task 6/7): the login-shell fallback (`zsh -lc`) cannot
  see `.zshrc`-managed PATH entries (the agents shim dir), so the first live firing exited 1
  with `host CLI 'claude' not found`. Fix at the right depth: `cgraph drain install` resolves
  the host CLI in the *install* environment (`resolve_host_cli`: `$CGRAPH_HOST_CLI`, else
  $PATH scan) and bakes it into the plist as an `EnvironmentVariables` entry
  (`LaunchAgentSpec.env`, rendered + tested); the script's runtime resolution remains the
  fallback for hand-run invocations.
- Drainer live proof (task 7, this machine): install with baked
  `CGRAPH_HOST_CLI=~/.agents/.cache/shims/claude` -> RunAtLoad firing gate-checked all 4
  supervisor-tracked repos: backend (`enrichment_pending: 0`) produced no drain line and no
  model spawn (status-gate skip), while collaboration-service (2 pending), turing-agents
  (125), frontend (1778) each dispatched a capped headless Sonnet run
  (`draining (cap 10 chunks)` per log). Sweep 1 exited 0 having drained
  collaboration-service 2 -> 0, turing-agents 125 -> 44, frontend 1778 -> 1698, all with
  `enrichment_failed: 0`.
- Second firing (`launchctl kickstart gui/<uid>/com.cgraph.enrich-drain`): its first log line
  is turing-agents (44 pending) — backend AND the now-drained collaboration-service produced
  no drain line and no model spawn. The status gate exits silently for drained repos; a repo
  re-enters the sweep only when `enrichment_pending` rises again.
