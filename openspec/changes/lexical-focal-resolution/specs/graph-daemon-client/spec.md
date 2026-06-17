## ADDED Requirements

### Requirement: Lexical multi-seed focal resolution for free-text queries
When resolving a focal node for a `context` or `query` request, the engine SHALL first attempt
exact (id, label, bare symbol) and substring matching, and SHALL fall back to lexical term-overlap
matching only when those produce no match. The lexical fallback SHALL rank nodes by the overlap of
the query's lexical terms with the node label and resolve the focal from the top match,
deterministically (ties broken by centrality then id). For a `context` request, the gather SHALL be
seeded from the top-N lexical matches and union their neighborhoods. When the best lexical overlap
falls below a minimum confidence threshold, the focal SHALL remain unresolved — the response returns
suggestions and the call is recorded as a zero hit, unchanged from current behavior.

#### Scenario: Natural-language query resolves via lexical overlap
- **WHEN** a `context` request supplies a free-text query that is not an exact match or a substring
  of any node id or label, but shares lexical terms with one or more symbols
- **THEN** a focal node is resolved from the highest-overlap match and a non-empty context bundle is
  returned, instead of the empty `focus:null` response

#### Scenario: Exact lookups are unchanged
- **WHEN** a request supplies an exact node id, an exact label, or a bare symbol name that resolves
  by the existing exact/substring path
- **THEN** that node is resolved exactly as before and the lexical fallback does not run

#### Scenario: Off-topic query stays an honest zero hit
- **WHEN** a query's best lexical overlap with any node is below the confidence threshold
- **THEN** the focal stays unresolved, the response returns `suggestions`, and the call is recorded
  as a context/query zero hit

#### Scenario: Multi-seed gather unions several ego graphs
- **WHEN** a free-text `context` query overlaps several symbols and resolves via the lexical fallback
- **THEN** the gathered candidate set is the union of the neighborhoods of the top-N lexical seeds,
  deduplicated by shallowest reach, and a relevant node reachable only from a lower-ranked seed is
  included

#### Scenario: Resolution is deterministic
- **WHEN** the same free-text query is resolved twice against the same snapshot
- **THEN** the same focal node (and seed set) is selected, with equal-overlap ties broken by
  centrality then id
