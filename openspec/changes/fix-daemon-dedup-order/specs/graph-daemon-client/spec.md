## MODIFIED Requirements

### Requirement: Immutable snapshot concurrency
The daemon SHALL serve reads from immutable graph snapshots and apply graph mutations through a
single writer before publishing a new complete snapshot. A graph the daemon rebuilds from source
(full rescan or incremental update) SHALL be identical to the graph the canonical one-shot pipeline
produces for the same files: the same deduplication result and the same node and edge counts, with
community and centrality computed on the deduplicated node set.

#### Scenario: Read during update is consistent
- **WHEN** a read request overlaps a watcher-driven update or semantic fragment merge
- **THEN** the read observes either the previous complete snapshot or the next complete snapshot,
  never a partially mutated graph

#### Scenario: Rebuilt graph matches the canonical pipeline
- **WHEN** the daemon rebuilds the graph for a project
- **THEN** its node and edge counts and its deduplication result match a one-shot build of the same
  project, and every node carries the centrality computed after deduplication
