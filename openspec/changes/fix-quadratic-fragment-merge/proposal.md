## Why

A phase-timing spike on a 5,702-file reference repo found that `merge_fragments` consumed 424s
of a 452s cold build (84%). `merge_fragments` calls `merge_fragment` once per file, and
`merge_fragment` rebuilds its node/edge dedup indexes from the entire accumulated graph on every
call — making a bulk merge O(fragments x nodes), i.e. quadratic. This is the single dominant cost
of every cold build and every daemon full rescan.

## What Changes

- Fix `merge_fragments` to maintain the node/edge/hyperedge dedup indexes once across all
  fragments instead of rebuilding them per file. Output is unchanged (first-occurrence-wins
  ordering and all dedup semantics preserved); only the time complexity changes, O(N^2) -> O(N).
- Leave `merge_fragment` (single-fragment merge into an existing graph) unchanged — its
  index-from-graph rebuild is correct for its one caller (`semantic_ingest.cpp`).
- Strengthen the merge unit test to assert first-occurrence-wins ordering and cross-fragment
  hyperedge dedup, which the prior count-only assertions did not cover.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `deterministic-graph-pipeline`: the graph-build merge step is specified to complete in time
  linear in total fragment size, not quadratic in file count.

## Impact

- `src/engine/graph_builder.cpp` (`merge_fragments`), `tests/smoke/graph_builder_test.cpp`.
- Measured: cold build on the reference repo dropped from ~452s to ~28.7s (15.7x), with
  byte-identical output (26,914 nodes / 77,330 links before and after). Full suite (53 tests) green.
- No API, protocol, output-shape, or parity-contract change.
