## ADDED Requirements

### Requirement: Fragment merge
The graph build SHALL merge per-file extraction fragments into a single graph, deduplicating
nodes by normalized id, edges by (source, relation, target), and hyperedges by id, with the
first occurrence of any duplicate retained. The merge SHALL complete in time linear in the total
number of fragment nodes and edges, and SHALL NOT rebuild its deduplication index from the
accumulated graph on a per-fragment basis.

#### Scenario: Duplicates are removed, first occurrence wins
- **WHEN** fragments contain nodes, edges, or hyperedges whose dedup key already appeared in an
  earlier fragment or earlier in the same fragment
- **THEN** the merged graph keeps only the first occurrence of each key and discards the rest

#### Scenario: Bulk merge stays linear
- **WHEN** a large number of fragments are merged in one build
- **THEN** total merge time grows linearly with total fragment size, not with file count squared
