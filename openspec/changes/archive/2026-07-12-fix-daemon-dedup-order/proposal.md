## Why

Profiling the daemon cold build (after the merge fix and parallel extraction) showed
`semantic_dedup` taking 62.7s of a 71s build, while the canonical `run_one_shot` pipeline dedups
the same project in ~2s. The cause is an ordering divergence: `run_one_shot` runs
merge -> resolve -> dedup -> communities -> analyze, but the daemon's `rebuild_graph` ran
communities + analyze first and dedup afterward. `semantic_dedup` buckets nodes by community when
the property is present and does O(k^2) fuzzy comparisons within each bucket, so running it after
community detection is both quadratic-slow and over-merges: on a 5,702-file repo the daemon
produced 21,376 nodes where the canonical pipeline produces 26,914 — ~5,500 distinct symbols
wrongly collapsed. It also computed centrality on the pre-dedup node set, leaving it stale.

## What Changes

- Move community detection + centrality (`detect_communities` + `analyze_graph`) out of
  `rebuild_graph` into a `finalize_graph` step the daemon runs AFTER dedup, matching the canonical
  `run_one_shot` order. Both the full rescan and the incremental update path now finalize after
  their dedup pass.
- Persist `graph.json` + manifest immediately after the graph is final (post semantic overlay) and
  before enrichment planning, and log the Tier-1 fast-path load before planning — so neither the
  cache write nor the load confirmation sits behind a slow whole-project enrichment walk.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: the resident daemon's rebuilt graph is specified to be identical to the
  canonical one-shot graph (same dedup result and node/edge counts), with community and centrality
  computed on the deduped node set.

## Impact

- `src/engine/incremental_update.cpp` (`rebuild_graph` / new `finalize_graph` / both update paths),
  `src/engine/daemon_server.cpp` (persist + log ordering), `tests/smoke/incremental_update_test.cpp`
  (parity assertion strengthened to node/edge counts + centrality presence).
- Measured on the reference repo: daemon cold build ~71s -> ~11s; daemon graph now 26,914 nodes /
  77,330 edges, identical to the canonical CLI build (was a wrong 21,376). Full suite 55/55.
- No protocol, op, or output-format change. This brings the daemon's output into agreement with the
  Graphify-parity contract the CLI already upholds.
