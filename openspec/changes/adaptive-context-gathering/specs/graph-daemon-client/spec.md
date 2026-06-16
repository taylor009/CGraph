## ADDED Requirements

### Requirement: Adaptive relevance-gated context gathering
The `pack_context` path SHALL support a flag-gated adaptive gather mode, selected by a `gather`
parameter (`"fixed"` default, `"adaptive"`), that changes only which candidates the BFS collects,
not the response shape. Under `gather = "fixed"` the gather behavior SHALL be byte-for-byte
unchanged from the current fixed k-hop BFS. Under `gather = "adaptive"` the gather SHALL expand all
nodes at depth 0 and 1 unconditionally (preserving the full 2-hop core), and SHALL expand a node at
depth ≥ 2 only when its query lexical-overlap is ≥ `gather_theta` (default 0.05), to a maximum depth
of 3, so that the third hop is taken only along query-relevant paths. Adaptive gather SHALL use the
existing knapsack fill and the existing deterministic relevance signal; it SHALL introduce no model
or LLM. `gather = "adaptive"` SHALL NOT be the default in this change.

#### Scenario: Default gather is unchanged
- **WHEN** a `context` request omits `gather` (or sets `gather = "fixed"`)
- **THEN** the gathered candidate set and the response are identical to the current fixed k-hop
  behavior for the same focal, budget, and packing

#### Scenario: Adaptive keeps the 2-hop core and gates the third hop
- **WHEN** a `context` request sets `gather = "adaptive"` with a `gather_theta`
- **THEN** every node within 2 hops of the focal is still gathered, and a node at depth 2 expands
  its depth-3 neighbors only if its query lexical-overlap is ≥ `gather_theta`; a depth-2 node below
  the threshold contributes no depth-3 neighbors

#### Scenario: Adaptive reaches beyond two hops for less than full three-hop cost
- **WHEN** adaptive gather runs on the evaluation set versus a fixed 2-hop and fixed 3-hop gather
- **THEN** its candidate set reaches relevant symbols a 2-hop gather misses, at a candidate-token
  cost far below the full 3-hop gather (the recall/cost win is the change's reason to exist)

### Requirement: In-engine revalidation gates adaptive gather
The adaptive gather mode SHALL remain non-default until its grade-2 recall improvement is
reproduced through the engine's own token accounting (capped source-slice char/4 cost and the
response's real packing), not only the offline Python harness. A parity test SHALL drive the
`context` op with `gather = "adaptive"` over the evaluation rows and assert the in-engine recall and
candidate-cost deltas against recorded targets; when the evaluation artifacts are absent the test
SHALL skip rather than fail.

#### Scenario: Parity gate reproduces the recall gain in-engine
- **WHEN** the parity test runs `gather = "adaptive"` against the evaluation rows with artifacts present
- **THEN** mean grade-2 recall@budget is at least the fixed-2-hop baseline plus the change's target
  margin, measured under the engine's real cost model, and the test fails if it is not

#### Scenario: Parity gate is CI-safe when artifacts are absent
- **WHEN** the evaluation graph/queries artifacts are not present (clean checkout / CI)
- **THEN** the parity test skips with a success exit rather than failing
