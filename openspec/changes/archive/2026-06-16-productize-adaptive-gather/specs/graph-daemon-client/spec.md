## MODIFIED Requirements

### Requirement: Adaptive relevance-gated context gathering
The `pack_context` path SHALL support a flag-gated adaptive gather mode, selected by a `gather`
parameter (`"fixed"` default, `"adaptive"`), that changes which candidates the BFS collects and adds
an additive gather-reach summary to the response (see "Adaptive responses report gather reach"); the
per-entry focus/included/omitted shape SHALL be otherwise identical between `fixed` and `adaptive`.
Under `gather = "fixed"` the gathered candidate set and the existing response fields SHALL be
byte-for-byte unchanged from the current fixed k-hop BFS. Under `gather = "adaptive"` the gather SHALL
expand all nodes at depth 0 and 1 unconditionally (preserving the full 2-hop core), and SHALL expand a
node at depth ≥ 2 only when its query lexical-overlap is ≥ `gather_theta` (default 0.05), to a maximum
depth of 3, so that the third hop is taken only along query-relevant paths. Adaptive gather SHALL use
the existing knapsack fill and the existing deterministic relevance signal; it SHALL introduce no
model or LLM. `gather = "adaptive"` SHALL NOT be the default in this change.

#### Scenario: Default gather is unchanged
- **WHEN** a `context` request omits `gather` (or sets `gather = "fixed"`)
- **THEN** the gathered candidate set and the pre-existing response fields are identical to the
  current fixed k-hop behavior for the same focal, budget, and packing

#### Scenario: Adaptive keeps the 2-hop core and gates the third hop
- **WHEN** a `context` request sets `gather = "adaptive"` with a `gather_theta`
- **THEN** every node within 2 hops of the focal is still gathered, and a node at depth 2 expands
  its depth-3 neighbors only if its query lexical-overlap is ≥ `gather_theta`; a depth-2 node below
  the threshold contributes no depth-3 neighbors

#### Scenario: Adaptive reaches beyond two hops for less than full three-hop cost
- **WHEN** adaptive gather runs on the evaluation set versus a fixed 2-hop and fixed 3-hop gather
- **THEN** its candidate set reaches relevant symbols a 2-hop gather misses, at a candidate-token
  cost far below the full 3-hop gather

## ADDED Requirements

### Requirement: Context responses self-describe their gather and packing mode
Every `context` op response SHALL include a `gather` field (`"fixed"` or `"adaptive"`) and a
`packing` field (`"greedy"` or `"knapsack"`) in every code path, so a caller can determine which
retrieval and packing strategy produced the bundle without inferring it from other fields. The fields
SHALL reflect the strategy actually used after defaults and the `adaptive`-implies-`knapsack` coupling
are applied.

#### Scenario: Greedy response names its mode
- **WHEN** a `context` request runs with the default packing and gather (greedy, fixed)
- **THEN** the response includes `gather: "fixed"` and `packing: "greedy"`

#### Scenario: Adaptive response names its mode
- **WHEN** a `context` request sets `gather = "adaptive"`
- **THEN** the response includes `gather: "adaptive"` and `packing: "knapsack"`

### Requirement: Adaptive responses report gather reach
When `gather = "adaptive"`, the `context` response SHALL include a `reach` summary reporting the total
candidate count gathered, the number of candidates admitted beyond the 2-hop core by the relevance
gate, and the number of depth-2 frontier nodes the gate rejected, so a caller and the telemetry can
observe whether the gate actually expanded the third hop. When `gather = "fixed"` the response SHALL
NOT include the `reach` summary.

#### Scenario: Gate admits a relevant third hop
- **WHEN** adaptive gather runs with a query whose terms match a depth-2 node, and that node has
  depth-3 neighbors
- **THEN** the `reach` summary reports at least one candidate admitted beyond the 2-hop core

#### Scenario: Gate collapses to the core when nothing is relevant
- **WHEN** adaptive gather runs with a query that matches no depth-2 frontier node above the threshold
- **THEN** the `reach` summary reports zero candidates admitted beyond the 2-hop core

### Requirement: Context op contributes a zero-result signal to op-stats
The op-stats recording for the `context` op SHALL set the zero-hit flag when the focal node does not
resolve — the id or query matched nothing — mirroring the query op's zero-hit semantics, so the
durable ledger distinguishes context calls that found a symbol from those that found nothing. A
`context` call that returns a resolved focus SHALL NOT be recorded as a zero hit, even when a tight
budget left no room for neighbors (the focal snippet is still usable context). The context zero-hit
count SHALL be persisted on the ledger line and summed in the cross-session rollup so the rate is
durable, not only live.

#### Scenario: Unresolved focus is a zero hit
- **WHEN** a `context` request resolves no focal node
- **THEN** the op-stats record for that call has the zero-hit flag set, and the persisted context
  zero-hit count for that lifetime increments

#### Scenario: A resolved focus is not a zero hit
- **WHEN** a `context` request returns a resolved focus (with or without included neighbors)
- **THEN** the op-stats record for that call does not have the zero-hit flag set

### Requirement: Op-stats distinguish adaptive gather usage
The durable op-stats ledger SHALL track `context` calls served with `gather = "adaptive"` separately
from fixed-gather calls, as an additive count that is persisted and summed across sessions, so a
rollup can report adaptive adoption. The adaptive count SHALL be readable from ledger files written
before this field existed by defaulting it to zero, so older ledgers continue to roll up without
migration.

#### Scenario: Adaptive context calls are counted distinctly
- **WHEN** N `context` calls run with `gather = "adaptive"` and M run with `gather = "fixed"`, then
  the stats are persisted and reloaded
- **THEN** the cross-session rollup reports the adaptive `context` count as N, independent of the
  total `context` count

#### Scenario: Older ledgers without the field still roll up
- **WHEN** a persisted ledger line predates the adaptive-usage field
- **THEN** the rollup reads its adaptive count as zero and sums it without error

### Requirement: graph_context advertises adaptive gather and forwards its parameters
The `graph_context` MCP tool description SHALL document the adaptive gather mode, when to use it (a
query is present and relevant reach beyond two hops is wanted at bounded token cost), and the
requirement that a `query`/`q` be supplied for the gate to take effect. The `gather` and
`gather_theta` arguments SHALL forward verbatim to the daemon `context` op.

#### Scenario: Tool description names adaptive
- **WHEN** an MCP client lists tools
- **THEN** the `graph_context` description mentions the adaptive gather mode

#### Scenario: Adaptive parameters forward to the context op
- **WHEN** an MCP client calls `graph_context` with `gather = "adaptive"` and a `gather_theta`
- **THEN** the daemon request targets the `context` op with `gather` and `gather_theta` carried
  through unchanged
