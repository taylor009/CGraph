## ADDED Requirements

### Requirement: Adaptive gather admits bounded same-file focal context
When `gather = "adaptive"` and a free-text query is present, the `context` op SHALL consider code nodes that share a non-empty source file with the primary resolved focal, even when no persisted graph edge connects those nodes. Same-file candidates SHALL be admitted directly as candidates, SHALL NOT expand the graph frontier, SHALL be deduplicated against normally reached nodes, and SHALL be bounded to five admitted nodes from the focal source file. Ordering SHALL be deterministic by lexical overlap descending, centrality descending, then node id ascending. Admitted entries SHALL report depth 2 and `via = "same_file"`. During knapsack packing, a same-file candidate SHALL receive lexical-overlap value only and SHALL NOT receive the structural hop-value term used by persisted graph neighbors. The implementation SHALL NOT add nodes or edges to `graph.json`.

#### Scenario: Adaptive gather includes a relevant same-file sibling
- **WHEN** a free-text `context` request resolves a primary focal whose source file contains an otherwise-unreachable sibling
- **THEN** adaptive gathering admits that sibling as a depth-2 candidate with `via = "same_file"`

#### Scenario: Fixed gather remains unchanged
- **WHEN** the same request sets `gather = "fixed"`
- **THEN** no same-file candidate expansion occurs and the response remains byte-for-byte identical to historical fixed gathering

#### Scenario: Existing graph reach wins deduplication
- **WHEN** an eligible same-file sibling was already reached through persisted graph edges at an equal or shallower depth
- **THEN** the existing reach record and relation remain unchanged and the sibling appears at most once

#### Scenario: Empty and non-code sources are excluded
- **WHEN** a seed or sibling has an empty source file or the sibling is a session-memory node
- **THEN** that node does not participate in same-file expansion

#### Scenario: Dense files are deterministically capped
- **WHEN** more than five eligible siblings share a focal source file
- **THEN** exactly the first five siblings under overlap-descending, centrality-descending, id-ascending order are admitted

#### Scenario: Query overlap controls inferred-candidate value
- **WHEN** a focal-file sibling has no lexical overlap with the free-text query but remains within the five-candidate cap
- **THEN** the sibling remains in the gathered candidate set but receives zero knapsack value and cannot displace a positive-value structural neighbor

### Requirement: Current-path recall gates same-file expansion
Same-file expansion SHALL be retained only when the committed query-only retrieval fixture demonstrates a strict mean grade-2 recall increase at one or more fixed budgets and no decrease at the remaining budgets, without fixture regeneration or tolerance weakening. The serialized graph parity tests SHALL remain unchanged, and median warmed `context` latency over at least 50 calls SHALL NOT regress by more than 10% on the same graph and query.

#### Scenario: Measured recall gain permits the change
- **WHEN** the committed end-to-end retrieval gate runs before and after same-file expansion
- **THEN** at least one of the 2k, 4k, or 8k recall measurements increases strictly and none decreases before baselines are updated

#### Scenario: No gain rejects the mechanism
- **WHEN** no committed retrieval budget improves or any budget decreases
- **THEN** the same-file expansion implementation is removed rather than shipped or hidden behind a fallback

#### Scenario: Graph parity remains byte-identical
- **WHEN** extractor golden and graph parity tests run after the change
- **THEN** their serialized `graph.json` outputs remain byte-identical to the pre-change outputs

#### Scenario: Context latency blocks a regression
- **WHEN** at least 50 warmed resident-daemon `context` calls are measured before and after on the same graph and query
- **THEN** the change is blocked if after-change median latency exceeds the before-change median by more than 10%

#### Scenario: Transfer topology blocks a regression
- **WHEN** the unchanged and changed engines score the same frozen TypeScript graph and query-only evaluation rows
- **THEN** no measured budget decreases before the same-file mechanism is retained
