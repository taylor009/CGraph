# Tasks

## 1. Locate canonical config sources

- [x] 1.1 Canonical source = repo copy `/.claude/commands/opsx/*.md` (plain committed files, NOT
  symlinks, NOT a marketplace plugin; full-turing's copy has a different md5 — per-project copies).
  Edits affect only cgraph sessions. No global blast radius for command routing.
- [x] 1.2 Confirmed via docs (code.claude.com/docs/en/slash-commands, line 236): custom commands
  are now skills; `model:` frontmatter is supported, accepts `/model` values or `inherit`. Nuance:
  the override applies for the rest of the invoking turn only, then reverts to session model —
  ideal for single-turn mechanical commands, and why apply/explore stay on session default.

## 2. Establish the baseline measurement (before)

- [ ] 2.1 (USER-OBSERVED) In a fresh `/clear`'d session, run one representative mechanical command
  (`/opsx:verify` or `/opsx:archive`) and record from the usage panel: model used, Opus cost,
  total cost. "Before" datapoint. NOTE: the routing edit is now already in place — capture this
  on a command that is NOT yet routed, or treat the current panel reading as post-change.
- [x] 2.2 Context floor recorded: direct MCP servers = `cgraph` (project, ~/.claude.json) +
  `chrome-devtools` (settings.json); 4 more MCP servers (datadog, linear, posthog, playwright)
  arrive via enabled plugins. Global `CLAUDE.md` = 11.6KB / 164 lines. MCP tool *schemas* are
  deferred (ToolSearch) so only instruction blocks + tool-name lists load eagerly per turn.

## 3. Route opsx commands by model (largest lever)

- [x] 3.1 Added `model: sonnet` to `archive`, `bulk-archive`, `sync`, `verify`, `new`, `propose`,
  `onboard`. All 7 verified to parse via `yaml.safe_load`.
- [x] 3.2 `apply`, `explore`, `continue`, `ff` set to `model: inherit` (documented value = keep
  session model) with a rationale comment on each, instead of a bare omission — makes the
  deliberate choice auditable. 4 verified to parse.
- [ ] 3.3 (USER-OBSERVED) In a fresh session, run a routed command (e.g. `/opsx:verify`) and
  confirm the usage panel reports Sonnet and near-zero Opus cost for that invocation.

## 4. Trim the always-on plugin / MCP loadout (context floor) — DESCOPED

- [x] 4.1 DESCOPED (user decision 2026-06-19): not done by design. MCP schemas are deferred, so the
  trim saves ~1–2% of the per-turn floor while breaking the documented Datadog/browser workflow.
  All plugins kept always-on. See proposal Non-Goals + design.md.
- [x] 4.2 DESCOPED with 4.1 (no trim to verify).

## 5. Enforce subagent model default

- [x] 5.1 Spot-checked opsx command bodies for subagent spawns: exactly one — `archive.md:63`
  spawned a `general-purpose` subagent for sync with no model. Fixed at the source by adding
  `model: "sonnet"` to that Task invocation (mechanical sync work, matches archive's routing).

## 6. After-measurement and acceptance

- [ ] 6.1 Re-run the baseline command set from task 2 post-change and record the "after" Opus cost
  per command. Compute the delta.
- [ ] 6.2 Record results: before vs after Opus cost per command, context-floor change, and which
  lever produced which delta. Confirm no cgraph engine/spec/`graph.json` change occurred.
- [ ] 6.3 If the measured saving on routed commands is not materially lower (Sonnet line replacing
  Opus line), investigate whether the `model:` frontmatter actually took effect before declaring
  done.
