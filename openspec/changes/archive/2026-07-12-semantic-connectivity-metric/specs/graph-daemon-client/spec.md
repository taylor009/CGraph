## ADDED Requirements

### Requirement: Status reports semantic connectivity
The `status` op SHALL report a `semantic` block describing how well the host-authored semantic
layer connects to the code graph, computed from the current snapshot so it reflects live
enrichment. The block SHALL include the number of document nodes, the number of concept nodes, how
many document nodes reach a code node (the connected set), the orphan document count, the orphan
concept count, the number of edges bridging semantic nodes directly into code, and a connectivity
rate (connected documents / document nodes). A node SHALL be treated as semantic when its id is
namespaced `doc:`, `concept:`, or `topic:`, and as a code node otherwise.

#### Scenario: Status carries the semantic block
- **WHEN** the snapshot contains a document node with an edge into a code node
- **THEN** `status.semantic` reports `doc_nodes` >= 1 and a `connectivity_rate` greater than 0

#### Scenario: Empty semantic layer reports zeros without dividing by zero
- **WHEN** the snapshot has no semantic nodes (a pure code graph)
- **THEN** `status.semantic` reports zero doc and concept nodes and a connectivity rate of 0

### Requirement: Document-to-code connectivity is transitive
A document node SHALL count as connected when it reaches a code node within a bounded number of
hops over the graph edges, so a document linked to code through a concept
(`doc -> concept -> code`) counts as connected, not orphan. A document that reaches no code node
within the bound SHALL count as an orphan document.

#### Scenario: Document connected through a concept counts as connected
- **WHEN** a document links to a concept that links to a code node, and the hop bound is at least 2
- **THEN** the document is counted in the connected set, not as an orphan

#### Scenario: Direct and transitive bounds differ
- **WHEN** the only path from a document to code is `doc -> concept -> code`
- **THEN** the document is connected at a hop bound of 2 and not connected at a hop bound of 1

#### Scenario: Document reaching no code is an orphan
- **WHEN** a document links only to a concept that itself has no edge to any code node
- **THEN** the document is counted as an orphan document and the concept as an orphan concept
