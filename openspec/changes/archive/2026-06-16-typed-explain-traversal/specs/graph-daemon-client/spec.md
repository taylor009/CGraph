# graph-daemon-client (delta: typed-explain-traversal)

## ADDED Requirements

### Requirement: Explain supports optional relation filtering
The `explain` op SHALL accept an optional `relation` parameter. When `relation` is a
non-empty string, the op SHALL return only adjacent edges whose stored relation token
equals it, using the same exact, case-sensitive comparison as `impact`'s relation filter;
no case-folding or alias mapping SHALL be applied. The relation filter SHALL be applied
before centrality ordering and before the `limit` truncation, so that edges matching the
requested relation are never displaced by higher-centrality edges of other relations. The
relation filter SHALL compose with the existing `direction` filter: when both are given,
only edges satisfying both SHALL be returned. When `relation` is absent or empty, the op's
returned neighbor set, ordering, and counts SHALL be byte-for-byte identical to current
behavior. A `relation` value that matches no adjacent edge SHALL yield an empty neighbor
list and SHALL NOT be treated as an error or a missing-node case.

#### Scenario: Relation filter returns only matching edges
- **WHEN** `explain` is called on a node with mixed adjacent relations and `relation: CALLS`
- **THEN** every returned neighbor carries `relation == "CALLS"`, and an adjacent non-`CALLS`
  neighbor that appears in the unfiltered result is absent

#### Scenario: Absent relation preserves current behavior
- **WHEN** `explain` is called without a `relation` parameter
- **THEN** the returned neighbor set, centrality ordering, and counts are identical to the
  behavior before this change

#### Scenario: Relation and direction compose
- **WHEN** `explain` is called with `direction: in` and `relation: CALLS`
- **THEN** only incoming `CALLS` edges are returned (the intersection of both filters)

#### Scenario: No-match relation yields an empty neighbor list
- **WHEN** `explain` is called with a `relation` that no adjacent edge carries
- **THEN** the response is a found node with an empty neighbor list, not an error or a
  not-found result

### Requirement: graph_explain advertises typed traversal and forwards the relation parameter
The `graph_explain` MCP tool description SHALL document the `relation` parameter and the
named single-hop traversal patterns an agent uses, mapped to the project's stored relation
tokens: find callers (`direction: in`, `relation: CALLS`), find callees (`direction: out`,
`relation: CALLS`), find references (`relation: references`), trace imports
(`relation: imports`), and inspect inheritance/implementation edges (`relation: inherits`).
The tool SHALL forward the `relation` argument verbatim to the `explain` op so the param is
never silently dropped.

#### Scenario: Tool description names the typed patterns
- **WHEN** an MCP client lists tools
- **THEN** the `graph_explain` description names the `relation` parameter and the
  callers/callees/references usage patterns

#### Scenario: relation forwards to the explain op
- **WHEN** an MCP client calls `graph_explain` with `relation: CALLS`
- **THEN** the daemon receives `op == "explain"` with `params.relation == "CALLS"`
