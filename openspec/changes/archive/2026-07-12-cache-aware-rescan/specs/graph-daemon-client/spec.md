## MODIFIED Requirements

### Requirement: Daemon lifecycle and fallback
The daemon SHALL support idle shutdown, clean socket or pipe cleanup, a tiered version-stamped
startup that reuses a persisted extraction index, and one-shot CLI fallback for environments that
cannot run resident processes. Startup SHALL escalate by cost: serve directly from the persisted
graph when no source file has changed; re-extract only changed, added, or removed files when some
have; and perform a full rebuild only when no usable cache exists. A tiered startup SHALL produce
a graph byte-identical to a full cold rebuild for the same source tree and version key. A full
rescan SHALL re-extract only the files that changed since the in-memory index was built, reusing
the held extraction for unchanged files, and SHALL produce the same graph as re-extracting every
file.

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

#### Scenario: Full rescan re-extracts only changed files
- **WHEN** a full rescan runs while the in-memory index already holds extractions and only a subset
  of files changed
- **THEN** only the changed and new files are re-extracted, unchanged files reuse their held
  extraction, and the resulting graph equals a rescan that re-extracted every file

#### Scenario: Branch switch does not force a full re-extract
- **WHEN** a checkout rewrites file modification times without changing file contents
- **THEN** the stat miss falls back to a content-hash comparison and unchanged files are reused
  rather than re-extracted
