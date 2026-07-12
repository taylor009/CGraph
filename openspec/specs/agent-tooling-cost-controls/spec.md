# agent-tooling-cost-controls Specification

## Purpose
TBD - created by archiving change reduce-opus-cost-routing. Update Purpose after archive.
## Requirements
### Requirement: Slash-command model routing
Each opsx slash command SHALL declare a model appropriate to its task class: commands whose work is
mechanical artifact manipulation (archive, bulk-archive, sync, verify, new, propose, onboard) SHALL
pin the cheaper model via `model:` frontmatter, and commands performing parity-critical
implementation or architectural reasoning (apply, explore, continue, ff) SHALL omit the pin so they
inherit the session model. No command SHALL silently run the most expensive model by default merely
because no model was specified.

#### Scenario: Mechanical command runs on the cheaper model
- **WHEN** a model-pinned mechanical command (e.g. `/opsx:verify`) is invoked in a fresh session
- **THEN** the session usage panel reports that invocation ran on the pinned cheaper model and the
  expensive-model cost attributable to it is approximately zero

#### Scenario: Load-bearing command keeps session model
- **WHEN** `/opsx:apply` is invoked in a session whose model is the expensive reasoning model
- **THEN** the command runs on that session model (no downgrade), preserving correctness on the
  Graphify parity contract

### Requirement: Explicit subagent model selection
Subagent invocations SHALL set the model explicitly rather than inheriting the session model,
defaulting to the cheaper model and reserving the expensive model for explicitly load-bearing
investigation or implementation.

#### Scenario: Default subagent uses the cheaper model
- **WHEN** a subagent is spawned for routine search or simplification work
- **THEN** it runs on the cheaper model because the invocation set the model explicitly, not by
  inheriting the session default

### Requirement: Before/after cost verification
Each cost-control lever SHALL be validated by a controlled before/after measurement against the
real session usage panel, recording the exact command and session, since config changes have no
executable test harness.

#### Scenario: Routing delta is measured, not assumed
- **WHEN** a command's model routing is changed
- **THEN** the same command is run before and after the change in fresh sessions and the
  expensive-model cost for that invocation is recorded for both, demonstrating the actual delta

