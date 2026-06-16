# graph-session-memory Specification

## Purpose
TBD - created by archiving change graph-session-memory. Update Purpose after archive.
## Requirements
### Requirement: Checkpoint write via the remember op
The daemon SHALL expose a `remember` op (MCP tool `graph_remember`) that accepts `title`,
`body`, optional `touches` (node ids or names), and optional `tags`. On success it SHALL write
the `body` to a markdown file located only under `cgraph-out/memory/`, and SHALL create exactly
one node with an id in the `memory:checkpoint:` namespace whose `kind` is `"checkpoint"`,
`label` is the title, `source_file` is the written markdown file, `confidence` is `Inferred`,
and whose `properties` include a `created_at` timestamp (and `tags` when supplied). The write
SHALL go through the single-writer graph-mutation path. The op SHALL NOT alter, annotate, or
overwrite any existing node.

#### Scenario: Checkpoint creates a node backed by a markdown body
- **WHEN** `remember` is called with a title and body
- **THEN** a markdown file is written under `cgraph-out/memory/`, a single
  `memory:checkpoint:<timestamp>` node is created with `source_file` pointing at that file and
  `properties.created_at` set, and no existing node is modified

#### Scenario: Body is sandboxed to the memory directory
- **WHEN** `remember` is called with a title or slug that would resolve outside
  `cgraph-out/memory/` (e.g. contains `..`, path separators, or an absolute path)
- **THEN** the file is written inside `cgraph-out/memory/` (sanitized) or the call is rejected;
  in no case is a file written outside `cgraph-out/memory/`

#### Scenario: Oversized body is rejected without side effects
- **WHEN** `remember` is called with a body exceeding the size cap
- **THEN** the op returns an error, writes no file, and creates no node (the graph is unchanged)

### Requirement: Touches create concerns edges only to resolved nodes
For each `touches` entry, the `remember` op SHALL add a `concerns` edge from the checkpoint
node to the entry's node only when that entry resolves to an existing node in the current
graph. An entry that does not resolve SHALL NOT produce an edge and SHALL be reported in the
response as unresolved. The op SHALL NOT create placeholder or dangling target nodes.

#### Scenario: Resolved touch becomes a concerns edge
- **WHEN** `remember` is called with a `touches` entry that resolves to an existing node
- **THEN** exactly one `concerns` edge is added from the checkpoint to that node

#### Scenario: Unresolved touch creates no edge
- **WHEN** a `touches` entry resolves to no node
- **THEN** no edge is created for it and the response reports it as unresolved

### Requirement: Checkpoint recall via the recall op
The daemon SHALL expose a `recall` op (MCP tool `graph_recall`) that accepts optional `query`
and optional `limit` and returns checkpoint nodes ordered newest-first by `created_at`. Each
returned entry SHALL include the checkpoint's body as a source snippet (read from its
`source_file`, subject to the existing snippet caps) and a brief of each code node the
checkpoint `concerns`. When `query` is supplied, results SHALL be filtered by lexical match
over title/tags. The response size SHALL be bounded by `limit` and the existing snippet caps.

#### Scenario: Recall returns newest checkpoints first with bodies and links
- **WHEN** multiple checkpoints exist and `recall` is called
- **THEN** they are returned newest-first, each carrying its body snippet and briefs of its
  `concerns` targets, capped by `limit`

#### Scenario: Checkpoint body is snippet-readable through context
- **WHEN** `graph_context` or `explain` is called on a checkpoint node id
- **THEN** the checkpoint body text is returned as the focal source snippet

### Requirement: Memory nodes are inert to code analysis and retrieval
Nodes whose id is in the `memory:` namespace SHALL be excluded from degree-centrality
computation, god-node ranking, and community detection, and SHALL never be gathered as
candidates by `pack_context` or returned as code matches by `query`. Adding, removing, or
linking memory nodes SHALL NOT change the centrality, community assignments, or retrieval
rankings of any code node.

#### Scenario: Memory nodes receive no centrality or community
- **WHEN** the analysis path runs over a graph containing `memory:` nodes
- **THEN** those nodes carry no `degree_centrality` or `god_node` property and no community
  assignment, and every code node's centrality is identical to the same graph without the
  memory nodes

#### Scenario: Memory nodes never enter code retrieval
- **WHEN** `pack_context` or `query` runs for a code focal/term and a `memory:` node is
  adjacent (via a `concerns` edge) or name-matches
- **THEN** no `memory:` node appears among the gathered candidates or returned matches

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

### Requirement: Status reports a memory inventory
The `status` op SHALL include a `memory` object describing the session-memory layer:
`checkpoint_count` (the number of `memory:` checkpoint nodes in the live snapshot),
`sidecar_count` (the number of checkpoint sidecar files on disk), `recall_count` (lifetime
`recall` op count), `recall_zero_hits` (recalls that returned no checkpoints), `last_remember_at`
and `last_recall_at` (recency of the most recent respective op, empty when never called), and
`last_overlay_count` (the number of checkpoints re-applied by the most recent memory re-overlay).
The block SHALL be present whenever the daemon is serving, with zeroed/empty values before any
memory activity.

#### Scenario: Inventory reflects written checkpoints
- **WHEN** one checkpoint has been written and `status` is requested
- **THEN** `memory.checkpoint_count` is 1 and `memory.sidecar_count` is 1; after a second
  checkpoint, `memory.checkpoint_count` is 2

#### Scenario: Inventory is restored after re-overlay
- **WHEN** the graph is rebuilt and memory checkpoints are re-overlaid from their sidecars
- **THEN** `memory.checkpoint_count` reflects the re-applied checkpoints (not duplicated) and
  `memory.last_overlay_count` is the number re-applied

### Requirement: Recall zero-hits are counted
The daemon SHALL count `recall` operations that return no checkpoints as recall zero-hits and
expose the count in `status.ops` and the `memory` block. Counting recall zero-hits SHALL NOT
change the existing `query` or `context` zero-hit counters or behavior.

#### Scenario: A recall that returns nothing is counted
- **WHEN** a `recall` is issued whose query matches no checkpoint
- **THEN** `recall_zero_hits` increments, while `query_zero_hits` and `context_zero_hits` are
  unchanged

#### Scenario: A recall that returns results is not a zero-hit
- **WHEN** a `recall` returns at least one checkpoint
- **THEN** `recall_zero_hits` does not increment

### Requirement: remember and recall are recorded in the durable op-stats ledger
The durable op-stats ledger SHALL record `remember` and `recall` operations — their per-op
counts and latency histograms in each ledger line and in the cross-session rollup — and SHALL
record a top-level `recall_zero_hits`. This SHALL be additive and backward compatible: the
histogram bucket layout and `schema_version` SHALL be unchanged, and ledger lines written before
this change (which lack `remember`/`recall` op entries and `recall_zero_hits`) SHALL still parse
and roll up, contributing zero to the new fields.

#### Scenario: A memory-active lifetime records remember/recall durably
- **WHEN** a daemon serves `remember`/`recall` ops and then shuts down
- **THEN** its ledger line's `ops` object contains `remember` and `recall` entries with counts,
  and a top-level `recall_zero_hits`, and the rollup sums them across lifetimes

#### Scenario: Pre-existing ledger lines remain parseable
- **WHEN** the rollup processes a ledger that contains lines written before this change
- **THEN** those lines parse without error, their missing `remember`/`recall`/`recall_zero_hits`
  read as zero, and same-`schema_version` histograms still merge

