## Why

A cold daemon start rebuilds the entire graph from source on every launch. Measured on a
1,251-file TS/TSX repo this is ~504s (21,376 nodes / 69,018 edges), and profiling of the
post-extraction phases shows they are O(N+E) with pre-built hash indexes and MinHash/LSH dedup
— the time is dominated by serial per-file tree-sitter extraction.

The daemon already writes `graph.json` and can load it back (`load_graph_snapshot`), but the
cold-start path ignores it and re-extracts every file. More fundamentally, the incremental
engine rebuilds from a per-file `IncrementalGraphIndex` (each file's `ExtractionResult`), and
that index is **never persisted** — so even a restart where nothing changed pays the full parse
cost. The common day-to-day case (restart the resident daemon with an unchanged or barely-changed
tree) should not require re-parsing 1,251 files.

## What Changes

- Persist the per-file extraction index (`IncrementalGraphIndex`: fragments, raw calls/relations,
  file cache entries, aliases) to disk under the project output dir, atomically.
- Stamp the persisted cache with a content-addressed **version key** derived from extractor
  logic, language config, and ID-normalization rules. A mismatch invalidates the entire cache
  and forces a full rebuild — guarding against silently serving a graph built by an older
  extractor when source files are byte-identical.
- Replace the unconditional full re-extract at daemon startup with a **tiered start**:
  - Tier 1 (nothing changed): stat/hash-diff the tree against the persisted cache; if zero files
    changed, load `graph.json` and serve. No extraction, no rebuild.
  - Tier 2 (some files changed): load the persisted index, re-extract only changed/added files,
    drop removed files, rebuild the merged graph, persist the refreshed index + `graph.json`.
  - Tier 3 (no cache / version mismatch / corrupt): full cold rebuild (unchanged behavior).
- Preserve byte-for-byte graph parity: a tiered start MUST produce the same merged graph as a
  cold build for the same source tree and version key.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: Refine daemon startup and disk-reload behavior into a version-stamped,
  tiered start that reuses a persisted extraction index and only re-extracts changed files.

## Non-Goals

- Parallelizing the extraction loop (tracked separately as the cold-build / Tier-3 optimization).
  This change makes most cold builds unnecessary; it does not speed up the ones that remain.
- Changing the wire protocol, daemon ops, or MCP tool surface.
- Changing extraction, ID normalization, dedup, or `graph.json` output shape — parity is held.

## Impact

- Adds a serializer/deserializer for `IncrementalGraphIndex` (`ExtractionResult` + `FileCacheEntry`)
  and a version-key computation; adds a tiered startup path in the daemon server.
- Adds an on-disk cache artifact under the project output dir (`cgraph-out/`); size and load time
  are validated against re-extraction cost before format is fixed (JSON vs. compact binary).
- Adds tests for: round-trip index (de)serialization, version-key invalidation, Tier-1 unchanged
  fast path, Tier-2 changed-subset rebuild producing a parity-identical graph, and corrupt-cache
  fallback to full rebuild. Adds a phase-timing benchmark spike confirming extraction dominance
  and measuring the standalone merge/resolve/dedup/analyze floor.
