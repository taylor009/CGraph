# graph-session-memory (delta: graph-session-memory-overlay)

## MODIFIED Requirements

### Requirement: Checkpoints persist on disk and recall within the daemon session
A checkpoint's markdown body SHALL be written under `cgraph-out/memory/` and the checkpoint
node SHALL be added to the live graph snapshot so that within a running daemon it is
recall-able and its body is snippet-readable. In addition, a checkpoint SHALL be persisted as
an on-disk sidecar fragment (see "Checkpoints persist as re-overlaid sidecar fragments") so it
durably survives daemon restarts, incremental code edits, and full rescans — not only the live
`/clear` case. The sidecar fragment, NOT `graph.json`, SHALL be the source of truth for memory.

#### Scenario: Checkpoint body persists on disk
- **WHEN** a checkpoint is written
- **THEN** its markdown body file exists under `cgraph-out/memory/` and the checkpoint node's
  `source_file` points at it

#### Scenario: Checkpoint is recall-able after /clear within the same daemon
- **WHEN** a checkpoint is written and the agent clears its context (the daemon keeps running)
- **THEN** the checkpoint is recall-able with its body snippet and linked code briefs

## ADDED Requirements

### Requirement: Checkpoints persist as re-overlaid sidecar fragments
At `remember` time the daemon SHALL write, alongside the markdown body, a sidecar fragment file
under `cgraph-out/memory/` containing the checkpoint node and its `concerns` edges, serialized
in the fragment schema so it can be parsed back. After every graph rebuild — a full rescan, a
fast-load of the persisted graph on startup, and an incremental code-edit rebuild — the daemon
SHALL re-overlay all memory sidecar fragments by re-reading them from disk and merging them into
the live snapshot, at the same points it re-overlays semantic fragments. The sidecar files SHALL
be the durable source of truth: a checkpoint SHALL be recall-able after a daemon restart and
after any rebuild, independent of whether the startup took the fast-load or the rebuild path.
Memory sidecars SHALL live in a directory distinct from the semantic-drop directory and be
overlaid by a distinct hook.

#### Scenario: Checkpoint survives a daemon restart
- **WHEN** a checkpoint is written, the daemon stops, and a fresh daemon starts on the same
  project
- **THEN** recall returns the checkpoint with its body snippet and linked code briefs

#### Scenario: Checkpoint survives an incremental code edit
- **WHEN** a checkpoint exists and a watched code file changes, triggering an incremental rebuild
- **THEN** after the rebuild the checkpoint is still recall-able

#### Scenario: Checkpoint survives a full rescan
- **WHEN** a checkpoint exists and a full rescan rebuilds the graph from extraction
- **THEN** after the rescan the checkpoint is still recall-able

### Requirement: Memory re-overlay is idempotent
Re-applying a memory sidecar fragment that is already present in the graph SHALL NOT duplicate
its node or edges. The overlay SHALL rely on first-occurrence-wins merge semantics so that
repeated rebuilds and overlays converge to exactly one node and one `concerns` edge per
checkpoint relationship.

#### Scenario: Re-overlay does not duplicate
- **WHEN** the same memory fragment is overlaid more than once (e.g. across successive rebuilds)
- **THEN** the graph contains exactly one checkpoint node and one `concerns` edge per
  relationship, with node/edge counts unchanged by the repeat

### Requirement: graph.json excludes memory nodes
The daemon's persisted graph snapshot (`graph.json`) SHALL NOT contain `memory:` nodes or edges
incident to them, so that the sidecar fragments are the sole source of truth for memory and
there is no second, divergent copy. This exclusion SHALL apply only to the daemon persist path;
recall SHALL continue to return checkpoints by re-overlaying the sidecars. Code-node output in
`graph.json` SHALL be otherwise unchanged.

#### Scenario: Persisted graph omits memory but recall still works
- **WHEN** a checkpoint is written and the daemon persists `graph.json`
- **THEN** `graph.json` contains no `memory:` node, and recall still returns the checkpoint
  after a restart via the sidecar overlay

### Requirement: Recall tolerates dangling concerns targets
Recall SHALL skip any `concerns` target that no longer resolves to a node (e.g. a code node
renamed or removed by a rebuild after the checkpoint was written) rather than erroring,
returning the checkpoint with only its resolvable links.

#### Scenario: A removed concerns target is skipped on recall
- **WHEN** a checkpoint concerns a code node that is later removed and recall runs
- **THEN** the checkpoint is returned without the missing link and without error
