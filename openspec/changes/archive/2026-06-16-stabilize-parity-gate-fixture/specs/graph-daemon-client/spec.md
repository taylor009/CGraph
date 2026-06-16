# graph-daemon-client (delta: stabilize-parity-gate-fixture)

## MODIFIED Requirements

### Requirement: In-engine revalidation gates adaptive gather
The adaptive gather mode SHALL remain non-default until its grade-2 recall improvement is
reproduced through the engine's own token accounting (capped source-slice char/4 cost and the
response's real packing), not only the offline Python harness. A parity test SHALL drive the
`context` op with `gather = "adaptive"` over the evaluation rows and assert the in-engine recall and
candidate-cost deltas against recorded targets. The parity test SHALL measure against a committed,
version-controlled fixture pair (a deterministic code-only graph and a verbatim eval snapshot), NOT
the mutable working-tree artifacts (`cgraph-out/graph.json`, `research/eval/queries.jsonl`), so the
gate is reproducible and immune to daemon-state or working-tree drift. Because the fixture is always
present, the gate SHALL run on every checkout including CI; the artifact-absent skip SHALL remain
only as a defensive fallback for the case where the fixture is missing. The recorded targets and
tolerance SHALL be unchanged by this stabilization.

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

## ADDED Requirements

### Requirement: Parity gate uses a committed deterministic fixture graph
The pack_context parity gate's graph fixture SHALL be a deterministic, code-only build of the
project (excluding disposable research and generated build artifacts), committed under version
control alongside a verbatim snapshot of the evaluation set. The fixture graph SHALL be reproducible:
rebuilding it from the same sources SHALL produce a byte-identical `graph.json`. The fixture graph
and the fixture eval set SHALL be internally consistent — every grade-2 evaluation `node_id` SHALL
resolve to a node in the fixture graph — so the gate is independent of checkout path and machine. The
fixture eval set SHALL be a verbatim copy: labels, grades, queries, and recorded targets SHALL NOT be
altered when producing or regenerating the fixture.

#### Scenario: Fixture graph is deterministic
- **WHEN** the fixture graph is rebuilt from the same sources
- **THEN** the resulting `graph.json` is byte-identical to the committed fixture

#### Scenario: Fixture graph and eval set are internally consistent
- **WHEN** the parity test resolves each grade-2 evaluation `node_id` against the fixture graph
- **THEN** every id resolves to a node present in the fixture graph

#### Scenario: Fixture excludes disposable and generated content
- **WHEN** the fixture graph is generated
- **THEN** it contains only the project's code (e.g. src/tests/scripts) and no `research/` or
  `build/` nodes, matching the graph the recorded targets were calibrated on
