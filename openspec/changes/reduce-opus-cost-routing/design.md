## Cost model

The bill is a product of three independently-tunable terms, measured from the session panel:

```
   cost  ≈   context_size   ×   num_turns   ×   model_rate
            ────────────     ────────────     ────────────
            /clear,/compact  decisiveness     model routing
            MCP/plugin trim  (mostly fixed)   (Opus -> Sonnet)
            lean CLAUDE.md                    ~5x lever
```

12.5M Opus cache read ÷ ~150k steady context ≈ ~83 turns. Each turn re-reads the whole window, so
shrinking `context_size` discounts *every remaining turn*, and dropping `model_rate` on a class of
turns discounts *all of them at once*. The two compose multiplicatively.

Ranked by leverage:

1. **model_rate (structural, decay-proof).** A `model:` line in command frontmatter pays out on
   every future invocation with zero ongoing discipline. Largest single lever because Opus≈5×
   Sonnet and the week is currently only 7% Sonnet.
2. **context_size floor (free, per-turn).** The always-on plugin/MCP surface + global CLAUDE.md set
   the minimum every turn pays. Trimming the loadout lowers the floor for all 83 turns.
3. **num_turns / session shape (behavioral).** `/clear` between tasks, `/compact` mid-task, fewer
   parallel Opus sessions. Real but requires ongoing discipline — lowest durability.

This change prioritizes (1) and (2) because they are one-time config edits; (3) is documented as
operating guidance, not encoded here.

## Routing decision matrix

Per-command model assignment, justified by whether the command does load-bearing reasoning on
this repo's parity-critical C++ or mechanical artifact manipulation:

| Command         | Model           | Rationale                                                        |
|-----------------|-----------------|------------------------------------------------------------------|
| `archive`       | sonnet          | Move completed change dir, update spec files — mechanical        |
| `bulk-archive`  | sonnet          | Batch of the above                                               |
| `sync`          | sonnet          | Reconcile artifacts — mechanical                                 |
| `verify`        | sonnet          | Read tasks vs specs, report gaps — mechanical reading            |
| `new`           | sonnet          | Scaffold change skeleton — templated                             |
| `propose`       | sonnet          | Draft proposal prose — drafting, low-risk                        |
| `onboard`       | sonnet          | 550-line scaffolding walkthrough — templated                    |
| `apply`         | session default | Implements code under the Graphify parity contract — needs Opus  |
| `explore`       | session default | Architectural reasoning / thinking partner — Opus judgment       |
| `continue`      | session default | Resumes apply-class work — inherits apply's risk profile         |
| `ff`            | session default | Fast-forward across steps incl. apply — keep Opus                |

"session default" means: do NOT pin; the command runs on whatever the session is, so the operator
chooses Opus when warranted. This avoids hard-coding Opus while keeping the safety margin on
parity-critical paths.

## Plugin / MCP loadout — DESCOPED (decision 2026-06-19)

Originally this change proposed trimming datadog/posthog/playwright to on-demand. Implementation
revealed the trade is bad and it was **dropped**:

- **Low leverage.** MCP tool *schemas* are deferred (loaded on demand via ToolSearch), so disabling
  these plugins only removes their instruction blocks + tool-name lists from the per-turn floor —
  ~1–3k of a ~150k window (~1–2%). The model-routing lever already captured the real savings.
- **Workflow conflict.** The project `CLAUDE.md` mandates Datadog for root-cause tracing and browser
  screenshots for UI verification — exactly the plugins the trim would disable.
- **Global blast radius.** `~/.claude.json` is machine-wide (all projects + parallel sessions),
  unlike the cgraph-only command edits.

Decision: keep all plugins always-on. The subagent-only plugins (coderabbit, pr-review-toolkit,
code-simplifier) stay; their cost is addressed by routing subagent invocations to Sonnet (below),
not by disabling the plugins.

## Subagent model default

The standing rule (global CLAUDE.md core-hard-lines #7) already says default Sonnet, Opus only for
load-bearing work. Config does not enforce it: no subagent definitions exist to pin, so the default
is "inherit session = Opus." The fix is operational — every `Agent` call sets `model` explicitly
(`"sonnet"` default, `"opus"` only when load-bearing). This is already mandated; the change records
it as a verification checkpoint rather than new config.

## Test strategy

Config has no unit-test harness, so red-green-refactor on code is not applicable. The repo's TDD
rule requires recording the validation approach when a behavior cannot be tested directly:

- **Observable behavior under test:** "after routing, a mechanical opsx command executes on
  Sonnet, not Opus."
- **Verification (the regression guard):** run a fixed, representative mechanical command (e.g.
  `/opsx:archive` on one completed change, or `/opsx:verify`) in a fresh `/clear`'d session, then
  read the session usage panel and confirm the model line shows Sonnet (and Opus cost ≈ 0) for that
  invocation. Capture before (no frontmatter) vs after (pinned) Opus spend for the same command.
- **Floor check:** with trimmed plugins, confirm the per-turn cached-context baseline drops
  (fewer MCP instruction blocks loaded) by inspecting a fresh session's loaded servers.
- **Pre-failing test impractical:** there is no executable assertion over frontmatter routing; the
  measurement above is the closest regression signal and is recorded as the acceptance evidence.

Numbers are only comparable within the same machine/account and command — record the exact command
and session for each before/after pair.
