## ADDED Requirements

### Requirement: Daemon operation stats in status
The daemon SHALL accumulate per-op counts and total latency for each request type, measured at the
request-dispatch boundary, and expose them through the `status` op as since-boot lifetime totals
together with a rolling recent window (last N operations). For queries it SHALL additionally report
a zero-hit rate (queries returning no results / total queries). The `status` payload SHALL also
include a modeled cache-saving estimate derived from the most recent (re)build's measured per-file
timings, labeled as an estimate and omitted when no per-file mean is available.

#### Scenario: Status reports per-op counts and latency
- **WHEN** a client has issued several `query` ops against the daemon
- **THEN** `status` reports a `query` count equal to the number issued and a non-zero total/mean
  latency for that op

#### Scenario: Zero-hit queries are tracked
- **WHEN** some issued queries return zero results and others return matches
- **THEN** `status` reports a query zero-hit rate equal to (zero-result queries / total queries)

#### Scenario: Lifetime totals and recent window coexist
- **WHEN** more operations have been issued than the rolling window capacity
- **THEN** `status` reports lifetime totals covering all operations and a recent window reflecting
  only the most recent N, and the recent window never exceeds its capacity

## MODIFIED Requirements

### Requirement: Daemon status reporting
The `status` op SHALL report live daemon and graph state with no field carrying a permanent
placeholder value. Specifically `uptime_seconds` SHALL reflect real elapsed time since daemon
start, `cache_hit_rate` SHALL reflect the measured fraction of files reused from cache on the most
recent (re)build, and `enrichment_running` SHALL reflect the count of in-flight enrichment ingests,
with `enrichment_state` reporting `running` while an ingest is active. The status payload SHALL
continue to report `pid`, `node_count`, `edge_count`, `build_state`, the enrichment pending/stale/
failed counts, `watching`, and `incremental_updates`.

#### Scenario: Uptime advances with daemon lifetime
- **WHEN** `status` is queried after the daemon has been running for a measurable interval
- **THEN** `uptime_seconds` is greater than zero and increases on a later query

#### Scenario: Cache hit rate reflects real reuse
- **WHEN** a warm rescan reuses a subset of files and re-extracts the rest
- **THEN** `cache_hit_rate` reported by `status` equals reused files / total files and lies in
  the interval (0, 1]

#### Scenario: Enrichment running is observable
- **WHEN** an enrichment ingest is in flight
- **THEN** `status` reports `enrichment_running` >= 1 and `enrichment_state` equal to `running`,
  and both clear once the ingest completes
