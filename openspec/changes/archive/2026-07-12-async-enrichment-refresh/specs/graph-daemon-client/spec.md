## MODIFIED Requirements

### Requirement: Daemon status
The daemon SHALL expose status including process id, uptime, node count, edge count, build state,
cache hit rate, and resident memory where available. Enrichment status (pending, stale, running,
failed) SHALL be refreshed asynchronously: a build, update, or fragment ingestion SHALL NOT block
its response on the whole-project enrichment scan, and the enrichment counts SHALL converge after
the asynchronous re-plan completes.

#### Scenario: Status reports enrichment state
- **WHEN** a client requests status
- **THEN** the response includes the current enrichment state and pending/stale/failed counts

#### Scenario: Update does not block on enrichment planning
- **WHEN** an `update` op rebuilds the graph on a project with many enrichable documents
- **THEN** the op responds once the graph is rebuilt and persisted, without waiting for the
  enrichment scan, and the enrichment counts are refreshed shortly afterward
