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
node SHALL be added to the live graph snapshot and persisted into `graph.json`, so that within
a running daemon it is recall-able and its body is snippet-readable. This is the primary use
case: `/clear` is a Claude Code context operation that does not stop the daemon, so a
checkpoint written before `/clear` is recall-able after it from the same long-running daemon.

Surviving a daemon **restart** or a mid-session **full rescan** is NOT guaranteed in v1: both
paths rebuild the graph from extraction results, which do not contain memory nodes, so the
live snapshot loses checkpoint nodes (their markdown bodies remain on disk under
`cgraph-out/memory/`). Re-overlaying memory after a rebuild — which makes checkpoints survive
restart and rescan — requires the re-overlay mechanism deferred to a follow-up change.

#### Scenario: Checkpoint body persists on disk
- **WHEN** a checkpoint is written
- **THEN** its markdown body file exists under `cgraph-out/memory/` and the checkpoint node's
  `source_file` points at it

#### Scenario: Checkpoint is recall-able after /clear within the same daemon
- **WHEN** a checkpoint is written and the agent clears its context (the daemon keeps running)
- **THEN** the checkpoint is recall-able with its body snippet and linked code briefs

