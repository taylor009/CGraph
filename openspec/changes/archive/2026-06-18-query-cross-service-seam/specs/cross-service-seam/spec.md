## ADDED Requirements

### Requirement: A fused seam graph is queryable read-only
The engine SHALL expose `cgraph seam query --graph FUSED.json <op> [PARAMS_JSON]`, which loads a
fused seam graph and runs one read op against it, printing the op's JSON response. The supported ops
are `query`, `path`, `explain`, `impact`, and `context` — the daemon's read ops — dispatched over the
loaded snapshot so they answer cross-service questions (traversing `CONSUMES`, `SERVED_BY`,
`RESPONDS_WITH`, `CONSUMED_AT`, `MIRRORED_BY` alongside each service's own edges). A non-read op
SHALL be rejected with a clear message and a non-zero exit; the seam graph is a read-only derived
view. Each invocation loads the graph fresh (one-shot); the seam is a static snapshot refreshed by
re-running `seam fuse`.

#### Scenario: Cross-service impact over the seam
- **WHEN** `cgraph seam query --graph FUSED.json impact '{"id":"schema:<provider>:<v>:<Name>","direction":"dependents"}'`
  runs on a fused seam graph
- **THEN** the response lists the dependents reached across services (e.g. the endpoint that
  responds with that schema and the consuming service), drawn from the contract edges

#### Scenario: Cross-service path over the seam
- **WHEN** `cgraph seam query --graph FUSED.json path '{"source":"<consumer node id>","target":"endpoint:<provider>:<METHOD> <path>"}'`
  runs
- **THEN** the response returns a path connecting the consumer code node to the provider endpoint
  across the contract edge (or reports no path), never erroring on the cross-service boundary

#### Scenario: Write ops are rejected
- **WHEN** `cgraph seam query` is invoked with an op outside `query`/`path`/`explain`/`impact`/`context`
  (e.g. `remember` or `update`)
- **THEN** the command rejects it with a clear message and a non-zero exit, writing no changes
