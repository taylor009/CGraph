## MODIFIED Requirements

### Requirement: Adaptive relevance-gated context gathering
The `pack_context` path SHALL support a `gather` parameter (`"adaptive"` **default**, `"fixed"`) that
changes which candidates the BFS collects and adds an additive gather-reach summary to the response
(see "Adaptive responses report gather reach"); the per-entry focus/included/omitted shape SHALL be
otherwise identical between `fixed` and `adaptive`. Under `gather = "fixed"` the gathered candidate
set and the existing response fields SHALL be byte-for-byte unchanged from the historical fixed
k-hop BFS (the pre-flip default), so callers can opt back into it exactly. Under `gather =
"adaptive"` (the default) the gather SHALL expand all nodes at depth 0 and 1 unconditionally
(preserving the full 2-hop core), and SHALL expand a node at depth ≥ 2 only when its query
lexical-overlap is ≥ `gather_theta` (default 0.05), to a maximum depth of 3, so that the third hop is
taken only along query-relevant paths. Adaptive gather SHALL use the existing knapsack fill and the
existing deterministic relevance signal; it SHALL introduce no model or LLM.

#### Scenario: Default gather is adaptive
- **WHEN** a `context` request omits `gather`
- **THEN** the gather is `"adaptive"` (knapsack packing, depth 3, θ=0.05 gate) and the response
  reports `gather: "adaptive"`

#### Scenario: Explicit fixed gather is unchanged
- **WHEN** a `context` request sets `gather = "fixed"`
- **THEN** the gathered candidate set and the pre-existing response fields are byte-for-byte
  identical to the historical fixed k-hop behavior for the same focal, budget, and packing

#### Scenario: Adaptive keeps the 2-hop core and gates the third hop
- **WHEN** a `context` request runs adaptive gather with a `gather_theta`
- **THEN** every node within 2 hops of the focal is still gathered, and a node at depth 2 expands
  its depth-3 neighbors only if its query lexical-overlap is ≥ `gather_theta`; a depth-2 node below
  the threshold contributes no depth-3 neighbors

#### Scenario: Adaptive reaches beyond two hops for less than full three-hop cost
- **WHEN** adaptive gather runs on the evaluation set versus a fixed 2-hop and fixed 3-hop gather
- **THEN** its candidate set reaches relevant symbols a 2-hop gather misses, at a candidate-token
  cost far below the full 3-hop gather

### Requirement: In-engine revalidation gates adaptive gather
The `context` default gather SHALL be `"adaptive"`, and a parity test SHALL guard that default by
reproducing its grade-2 recall improvement through the engine's own token accounting (capped
source-slice cost and the response's real packing), not only the offline Python harness. The parity
test SHALL drive the `context` op with `gather = "adaptive"` over the evaluation rows and assert the
in-engine recall and candidate-cost deltas against recorded targets. It SHALL measure against a
committed, version-controlled fixture pair (a deterministic code-only graph and a verbatim eval
snapshot), NOT the mutable working-tree artifacts (`cgraph-out/graph.json`,
`research/eval/queries.jsonl`), so the gate is reproducible and immune to daemon-state or
working-tree drift. Because the fixture is always present, the gate SHALL run on every checkout
including CI; the artifact-absent skip SHALL remain only as a defensive fallback for the case where
the fixture is missing. The recorded targets and tolerance SHALL be unchanged by the default flip.

#### Scenario: Parity gate reproduces the recall gain in-engine
- **WHEN** the parity test runs `gather = "adaptive"` against the committed fixture rows
- **THEN** mean grade-2 recall@budget is at least the fixed-2-hop baseline plus the change's target
  margin, measured under the engine's real cost model, and the test fails if it is not

#### Scenario: Parity gate runs against the committed fixture, not the working tree
- **WHEN** the parity test runs on any checkout, regardless of the contents of
  `cgraph-out/graph.json` or whether a daemon has accumulated unrelated nodes
- **THEN** it reads the committed fixture pair, reaches the measurement (does not skip), and its
  result depends only on the fixture and the engine, not on working-tree state

#### Scenario: Skip is a fallback only when the fixture is missing
- **WHEN** the committed fixture pair is absent (e.g. a deliberately stripped tree)
- **THEN** the test skips with a success exit rather than failing, exactly as the prior
  artifact-absent behavior

### Requirement: Context responses self-describe their gather and packing mode
Every `context` op response SHALL include a `gather` field (`"fixed"` or `"adaptive"`) and a
`packing` field (`"greedy"` or `"knapsack"`) in every code path, so a caller can determine which
retrieval and packing strategy produced the bundle without inferring it from other fields. The fields
SHALL reflect the strategy actually used after defaults and the `adaptive`-implies-`knapsack` coupling
are applied.

#### Scenario: Default response names its mode
- **WHEN** a `context` request runs with the defaults (adaptive gather)
- **THEN** the response includes `gather: "adaptive"` and `packing: "knapsack"`

#### Scenario: Explicit fixed/greedy response names its mode
- **WHEN** a `context` request sets `gather = "fixed"` with default packing
- **THEN** the response includes `gather: "fixed"` and `packing: "greedy"`
