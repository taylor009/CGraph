## ADDED Requirements

### Requirement: Enrichment planning reuses unchanged-file hashes
Enrichment planning SHALL maintain a per-file stat index (path, size, modification time, content
hash) and, before hashing a doc/media file, classify it against that index. When a file's size
and modification time match the indexed entry (a stat hit), planning SHALL reuse the stored hash
without reading the file. Planning SHALL read and re-hash only files that are new or whose size
or modification time changed. The chunk plan produced — its chunks, cache-hit count, and stale
count — SHALL be identical whether each file's hash was reused or recomputed.

#### Scenario: Unchanged files are not re-hashed across plans
- **WHEN** a plan runs over a tree of N doc/media files with an empty stat index, then runs again
  reusing the resulting index with no file changes
- **THEN** the first plan reads and hashes all N files and the second plan reads and hashes none,
  reusing all N stored hashes, and both plans produce the same chunks, cache-hit count, and stale
  count

#### Scenario: A changed file is re-hashed, others reused
- **WHEN** one file's modification time or size changes between two plans
- **THEN** only that file is read and re-hashed and the remaining files reuse their stored hashes

#### Scenario: Plan output is independent of hash reuse
- **WHEN** the same tree and cache are planned once cold (all hashed) and once warm (all reused)
- **THEN** the two plans are equal in chunk membership, content hashes, cache-hit count, and
  stale count

### Requirement: Enrichment stat index persists across restarts
The stat index SHALL be persisted to the semantic drop directory and reloaded on daemon start, so
that a restart over an unchanged tree reuses stored hashes rather than re-hashing every doc/media
file. A missing or unreadable index SHALL be treated as empty (cold), causing a one-time re-hash
without error.

#### Scenario: Restart reuses persisted hashes
- **WHEN** the daemon has planned a tree, persisted the stat index, and restarted with the tree
  unchanged
- **THEN** the first plan after restart reuses the persisted hashes and reads no unchanged file

#### Scenario: Absent index is treated as cold
- **WHEN** no stat index file exists
- **THEN** planning proceeds as a cold plan that hashes every file, with no error

### Requirement: Enrichment re-plans only on relevant change
After the initial plan that populates the pending counts at startup, enrichment SHALL re-plan
only when doc/media files change. A code-only operation — a build or an `update .` rescan that
touches no doc/media file — SHALL NOT trigger an enrichment re-plan or a doc-tree walk.

#### Scenario: Code-only rescan does not re-plan
- **WHEN** an `update .` rescan runs and no doc/media file has changed
- **THEN** no enrichment re-plan is triggered and the doc tree is not walked

#### Scenario: Doc change triggers a re-plan
- **WHEN** a doc/media file is added, changed, or removed
- **THEN** an enrichment re-plan is triggered

#### Scenario: Initial plan runs once
- **WHEN** the daemon starts and completes its initial build or fast-load
- **THEN** enrichment is planned exactly once to populate the pending/stale counts
