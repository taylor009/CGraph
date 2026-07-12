## ADDED Requirements

### Requirement: Persisted extraction index
The daemon SHALL persist the per-file extraction index (each file's extraction fragment, raw
calls, raw relations, file cache entry, and resolved path aliases) to disk under the project
output directory, written atomically, so that a subsequent start can rebuild the graph without
re-extracting unchanged files.

#### Scenario: Index round-trips without loss
- **WHEN** the daemon persists its extraction index and a later start loads it
- **THEN** rebuilding the graph from the loaded index produces a graph equal to rebuilding from
  the in-memory index that was persisted

#### Scenario: Corrupt cache is ignored, never half-loaded
- **WHEN** the persisted index file is truncated, partially written, or otherwise unparseable
- **THEN** the daemon treats it as no usable cache, falls back to a full rebuild, and serves a
  valid graph without crashing

### Requirement: Version-stamped cache invalidation
The persisted extraction index SHALL carry a content-addressed version key derived from extractor
identity, language configuration, and ID-normalization rules. The daemon SHALL discard the entire
cache and perform a full rebuild when the loaded key does not match the running binary's key,
even when every source file is byte-identical on disk.

#### Scenario: Extractor change invalidates a byte-identical tree
- **WHEN** the source tree is unchanged but the running binary's version key differs from the
  persisted cache's key
- **THEN** the daemon discards the cache and rebuilds the graph from a full extraction rather than
  serving a graph produced by the previous extractor

#### Scenario: Matching key permits reuse
- **WHEN** the persisted version key matches the running binary's key
- **THEN** the daemon is permitted to reuse cached per-file extraction results for unchanged files

## MODIFIED Requirements

### Requirement: Daemon lifecycle and fallback
The daemon SHALL support idle shutdown, clean socket or pipe cleanup, a tiered version-stamped
startup that reuses a persisted extraction index, and one-shot CLI fallback for environments that
cannot run resident processes. Startup SHALL escalate by cost: serve directly from the persisted
graph when no source file has changed; re-extract only changed, added, or removed files when some
have; and perform a full rebuild only when no usable cache exists. A tiered startup SHALL produce
a graph byte-identical to a full cold rebuild for the same source tree and version key.

#### Scenario: Idle daemon exits
- **WHEN** the daemon has no activity for the configured idle timeout
- **THEN** it flushes authoritative outputs as needed, releases the endpoint, and exits

#### Scenario: Restricted environment uses one-shot
- **WHEN** the target environment disallows background resident processes
- **THEN** the same engine can run in one-shot mode without daemon IPC

#### Scenario: Unchanged tree serves from disk without re-extracting
- **WHEN** the daemon starts, a valid version-matched cache exists, and a stat/hash diff finds no
  changed, added, or removed source files
- **THEN** the daemon loads the persisted graph and serves queries without calling the extractor

#### Scenario: Changed subset re-extracts only what changed
- **WHEN** the daemon starts with a valid cache and a stat/hash diff finds a subset of files
  changed, added, or removed
- **THEN** the daemon re-extracts only those files, reuses cached results for the rest, rebuilds
  the merged graph, and the result equals a full cold rebuild on the same tree

#### Scenario: Branch switch does not force a full re-extract
- **WHEN** a checkout rewrites file modification times without changing file contents
- **THEN** the stat miss falls back to a content-hash comparison and unchanged files are reused
  rather than re-extracted
