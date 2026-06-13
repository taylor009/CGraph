## ADDED Requirements

### Requirement: Plan offers candidate code links per document
The enrichment chunk plan SHALL compute, for each document input, candidate links to real code
nodes that the document mentions, derived deterministically from the code graph's node labels. Each
candidate SHALL carry the real code-node id and its label. The plan SHALL surface these as a
`candidate_links` array on each document input in `plan.json`. Candidates SHALL be node ids and
labels only; the plan SHALL NOT assign an edge relation (the authoring host chooses the relation).

#### Scenario: A document naming a code symbol gets that node as a candidate
- **WHEN** a planned document's text mentions a compound code symbol that exists as a graph node
  (e.g. `classify_cached_file`)
- **THEN** that document's `candidate_links` includes the symbol's real code-node id and label

#### Scenario: Candidates are ids and labels, not relations
- **WHEN** the plan emits candidate links for a document
- **THEN** each candidate contains a code-node id and label and no edge relation

### Requirement: Candidate matching is high-precision
Candidate matching SHALL accept compound identifiers (snake_case or internal CamelCase) and
capitalized type names, and SHALL reject bare lowercase words that merely coincide with a symbol
name. When a symbol name is shared by multiple nodes, rarer names SHALL rank ahead of common ones,
and the number of candidates per document SHALL be bounded.

#### Scenario: Bare-word collisions are excluded
- **WHEN** a document contains a bare lowercase word (e.g. `cache`) that happens to match a code
  symbol name, and no compound form
- **THEN** that word does not by itself produce a candidate link

#### Scenario: Candidate count is bounded
- **WHEN** a document mentions more matching symbols than the per-document cap
- **THEN** only the top-ranked candidates up to the cap are emitted

#### Scenario: Non-text inputs yield no candidates
- **WHEN** an input is media (no readable text)
- **THEN** it has no candidate links

### Requirement: Candidate links degrade gracefully without a code graph
Candidate computation SHALL require a code graph to match against. When no persisted code graph is
available to the planner, the plan SHALL emit empty `candidate_links` and otherwise produce the
same chunk plan it does today. The feature SHALL be additive: a fragment that ignores
`candidate_links` SHALL remain valid and merge unchanged.

#### Scenario: No code graph present
- **WHEN** the planner has no persisted code graph to load
- **THEN** every input's `candidate_links` is empty and the rest of the plan is unchanged

#### Scenario: Candidate links are advisory
- **WHEN** a host authors a fragment that does not use the offered candidate links
- **THEN** the fragment still validates and merges exactly as before
