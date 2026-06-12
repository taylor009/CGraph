## ADDED Requirements

### Requirement: One-shot operation stats
A one-shot build SHALL record per-phase wall-clock timings (extract, merge, resolve, dedup,
community detection, analysis) and counters (files extracted, files reused from cache, node count,
edge count) measured at the pipeline orchestration boundary. It SHALL write these to a sidecar
`stats.json` in the output directory and emit a single human-readable summary line to stderr. The
build SHALL NOT embed stats in `graph.json`; the node-link output SHALL remain byte-identical to a
build with stats disabled, preserving the Graphify parity contract.

#### Scenario: Build records phase timings and counts
- **WHEN** a one-shot build completes over a non-empty source tree
- **THEN** every recorded phase timing is greater than zero, the node and edge counters equal the
  resulting snapshot's `nodes.size()` and `edges.size()`, and `cgraph-out/stats.json` exists and
  parses as JSON

#### Scenario: graph.json parity is preserved
- **WHEN** a build is run with operation stats enabled
- **THEN** the produced `graph.json` is byte-identical to the parity golden for the same source tree

#### Scenario: Stderr summary is human-readable
- **WHEN** a one-shot build completes
- **THEN** stderr contains one summary line reporting file count, node count, edge count, and total
  build time in human units

### Requirement: Modeled cache-saving estimate
When a build or rescan reuses cached extractions, the stats output SHALL include a modeled
cache-saving estimate derived as `files_reused × mean(per-file extract time)` from measured
timings, presented under a key that identifies it as an estimate. The estimate SHALL be omitted —
never fabricated, hardcoded, or computed from a zero mean — when no extraction ran in the session
to establish a per-file mean.

#### Scenario: Estimate present when reuse and timings exist
- **WHEN** a rescan reuses at least one cached file and at least one file was actually extracted
- **THEN** the stats output includes a cache-saving estimate equal to
  `files_reused × mean(extract_ms)`, labeled as an estimate

#### Scenario: Estimate omitted on full cache hit
- **WHEN** a rescan reuses files but extracts none (no per-file mean available this session)
- **THEN** no cache-saving estimate is emitted, rather than a fabricated or zero value
