## ADDED Requirements

### Requirement: Not-ready reads are excluded from op-stats quality accounting
Reads served while the daemon is still building (the empty build snapshot, `build_state` Empty) SHALL
NOT be counted as operations or zero-hits in op-stats. Such a read SHALL instead increment a separate
`not_ready` counter and SHALL NOT affect per-op counts, latency, the zero-hit counters, or the recent
window. As a result, `query_zero_hit_rate` (and the other zero-hit/latency figures) reflect only
reads against a ready graph. The `not_ready` count SHALL be surfaced in `status` and the durable
ledger so build-window traffic remains visible without polluting the quality metrics.

#### Scenario: A query during build is not a miss
- **WHEN** a `query` is served while the graph is still building (empty snapshot, returning no
  results because nothing is loaded)
- **THEN** `query_zero_hits` and `query` count are unchanged, the `not_ready` counter increments by
  one, and `query_zero_hit_rate` is unaffected

#### Scenario: Ready-state accounting is unchanged
- **WHEN** a `query` is served against a ready graph
- **THEN** it is counted normally (op count, latency, and zero-hit if it returned nothing), exactly
  as before

### Requirement: Op-stats record the query route distribution
Op-stats SHALL record how query-op responses were routed, in three buckets — `entity` (a unique exact
symbol), `structural` (a typed-traversal intent such as callers/callees/references), and `search`
(lexical/name search or unrouted) — so routing adoption is observable. These counts SHALL be surfaced
in `status` and persisted in the durable ledger and its cross-session rollup, alongside the existing
adaptive-context counter.

#### Scenario: Route counts reflect how queries resolved
- **WHEN** queries are served and routed (some to entity, some to structural traversal, some to
  lexical search)
- **THEN** `status` and the ledger report the per-bucket counts, so the fraction of queries answered
  by typed routing vs search is visible

### Requirement: The stats command reports all-time by default
`cgraph stats` SHALL default to the full durable history (no lower time bound); `--since` SHALL still
narrow the window. Checking stats without arguments SHALL surface all recorded lifetimes rather than
only the current day's.

#### Scenario: Default stats shows full history
- **WHEN** `cgraph stats --root PATH` is run with no `--since`
- **THEN** it rolls up every recorded lifetime in the ledger, not only those since the start of the
  current day
