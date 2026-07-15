## ADDED Requirements

### Requirement: Fragment edges must resolve to known nodes
Semantic ingest SHALL reject a fragment atomically — with the graph unchanged and the rejection
counted as a failed fragment — when any edge endpoint resolves against neither the fragment's
own nodes (keyed exactly as merge keys them) nor the current graph snapshot. This check wraps
the semantic-ingest path only; the deterministic pipeline's merge semantics are unchanged.

#### Scenario: Dangling edge endpoint
- **WHEN** a schema-valid fragment carries an edge whose target id exists in neither the
  fragment nor the graph
- **THEN** the fragment is rejected with an error naming the unknown endpoint, no node or edge
  from the fragment enters the graph, and `enrichment_failed` is incremented

#### Scenario: Edges into the existing graph still merge
- **WHEN** a fragment's edge references a node already present in the graph snapshot
- **THEN** the fragment merges normally
