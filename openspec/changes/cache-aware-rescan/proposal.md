## Why

`full_stat_index_rescan` re-extracted every detected file on every call, even when the daemon
already held an up-to-date extraction for most of them. The daemon keeps a warm in-memory index
(`IncrementalGraphIndex`), so an `update .` op that touches a handful of files was re-parsing the
entire project.

This change also closes out the Tier-2 question from `persist-incremental-index`: should the
daemon persist the per-file extraction index to disk so a restart with a few changed files can
re-extract only the delta? Measured answer: no. See Non-Goals.

## What Changes

- Make `full_stat_index_rescan` cache-aware: classify each detected file against the existing
  index (`classify_cached_file`), reuse the held `ExtractionResult` for stat/hash hits, and
  re-extract only changed and new files. With an empty index (cold start) everything is new and
  all files are extracted, so the cold path is unchanged. A reused fragment is byte-identical to
  re-extracting it, so the merged graph is identical either way.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: a full rescan re-extracts only the files that changed since the index was
  built, reusing cached extractions for the rest, while producing the same graph as re-extracting
  everything.

## Non-Goals

- **Tier-2 persisted index (rejected, measured).** A prototype that serialized the full per-file
  index to `index-cache.json` and reloaded it on restart to skip re-extraction was built and
  benchmarked on a 39,114-node repo: cold rebuild (Tier 3) 18.8s vs. Tier-2 restart 22.0s — a net
  loss — because parsing the 144 MB index file costs more than re-extracting the source with the
  already-parallel tree-sitter pass. Reverted. Tier 1 (skip the rebuild entirely on an unchanged
  restart) remains the worthwhile persistence tier.

## Impact

- `src/engine/incremental_update.cpp` (`full_stat_index_rescan`), `tests/smoke/incremental_rescan_test.cpp`.
- A warm `update .` re-extracts only changed files (verified: 4 of 8,218 on a touched-4-files
  rescan) instead of all. Note: the end-to-end `update .` latency on a large repo is currently
  dominated by synchronous semantic-enrichment planning (`refresh_enrichment_state`), a separate
  pre-existing cost not addressed here.
- No change to graph output or the cold-build path. Full suite 55/55.
