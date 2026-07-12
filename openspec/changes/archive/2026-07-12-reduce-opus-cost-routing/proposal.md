## Why

A single 15h-wall-clock session cost **$10.29**, of which **$9.59 (93%)** was Opus 4.8. By token
volume that Opus spend is almost entirely cache reads:

```
input        30.8k    output 79.7k    cache write 191.6k    cache read 12.5M
                                                             ^^^^^^^^^^^^^^^^^ 98% of tokens
```

A cache read is not free reuse — it re-pays (at the reduced cache rate) to re-read the **entire**
conversation context on **every** turn. So 12.5M cache read ÷ ~150k steady context ≈ **~83 turns
each re-reading a 150k+ window.** The cost is therefore a product, not a sum:

```
cost  ≈  context_size  ×  num_turns  ×  model_rate
        └ 88% >150k ┘    └ 15h sess ┘   └ all Opus ┘
```

The three terms are multiplicative — fixing two compounds. The session diagnostic flagged the
symptoms (88% of spend at >150k context, 50% subagent-heavy, 18% from `/opsx:apply`, 25% from the
opsx plugin) but not the lever that ties them together: **everything runs on Opus by default, and
the default is never overridden anywhere in config.**

Verified facts (this machine, 2026-06-19):

- **11/11 opsx commands** (`.claude/commands/opsx/*.md`) carry **no `model:` frontmatter** → each
  inherits the session model (Opus). `/opsx:apply` alone is 18% of spend.
- **15 plugins are enabled** (`~/.claude.json` `enabledPlugins`), dragging in 4 always-on MCP
  servers (datadog, linear, posthog, playwright) plus their subagents — a permanent per-turn
  context tax and the source of the "50% subagent-heavy" and per-MCP usage lines.
- **No user subagent model overrides** exist (`~/.claude/agents` / `~/.agents/agents` empty), so a
  spawned subagent runs on Opus unless its caller sets `model` explicitly — contradicting the
  user's own standing rule ("default Sonnet for subagents").
- Global `CLAUDE.md` is **11.6KB / 164 lines**, loaded every session as part of the floor.

## What Changes

- **Pin a model on each opsx command via frontmatter** — the structural, decay-proof fix. Route
  mechanical commands (archive, bulk-archive, sync, verify, new, propose, onboard) to Sonnet; keep
  reasoning/load-bearing commands (apply, explore, continue, ff) on the session default so
  parity-critical C++ work in this repo still gets Opus judgment.
- **Make subagent model routing explicit** so spawned subagents default to Sonnet, with Opus
  reserved for explicitly load-bearing investigation — closing the gap between the standing rule
  and actual config.
- **Establish a before/after cost measurement** so each lever is proven against the real usage
  panel, not assumed.

## Capabilities

### New Capabilities

- `agent-tooling-cost-controls`: the user's Claude Code tooling config (slash-command model
  routing and subagent model defaults) is configured so the default execution path uses the
  cheapest model that fits the task, with parity-critical and reasoning commands explicitly kept
  on the session model.

### Modified Capabilities

- None (this is workflow/tooling config for the repo's agent environment, not cgraph product code;
  `graph.json` parity and the engine are untouched).

## Non-Goals

- **No change to cgraph engine code, exports, or the Graphify parity contract.** This touches only
  `~/.claude` / `~/.agents` / repo `.claude/` config.
- **No blanket "route everything to Sonnet."** Parity-critical implementation (apply on this repo)
  and genuine architectural reasoning (explore) stay on Opus by design — correctness over cost.
- **No new tooling or scripts to manage config.** Edits are to existing frontmatter and settings
  files.
- **No plugin/MCP loadout trim** (descoped 2026-06-19). MCP schemas are already deferred, so
  disabling datadog/posthog/playwright saves only ~1–2% of the per-turn floor while breaking the
  documented Datadog/browser workflow — a bad trade. All plugins stay always-on.
- **No claim of a specific % saving until measured.** The expected saving is modeled from the
  multiplicative cost equation; the actual delta is verified in the measurement task.

## Impact

- `.claude/commands/opsx/*.md` (add `model:` frontmatter; identify and edit the canonical source —
  plugin install vs. repo copy — not a stale copy).
- `~/.claude.json` `enabledPlugins` — left unchanged (trim descoped, see Non-Goals).
- Subagent invocation defaults (standing rule already exists in global `CLAUDE.md`; make it
  enforced in config where subagents are spawned).
- No code, no tests in the cgraph suite. Verification is a controlled before/after usage
  measurement (see `design.md` test strategy), since config has no unit-test harness.
