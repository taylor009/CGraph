# Graph session memory: durable re-overlay

## Why

graph-session-memory v1 made `graph_remember`/`graph_recall` work, but checkpoints only
survive within a single running daemon. They vanish on any graph rebuild because they are
snapshot-only:

- The rebuild is **extraction-only**: `rebuild_graph` constructs a fresh snapshot from
  `index.files` alone (`incremental_update.cpp:25-50`), and the rescan/incremental paths
  `publish_graph_snapshot` it wholesale (`incremental_update.cpp:219-231`).
- Memory nodes **never enter `index.files`** — `remember` injects them straight into the live
  snapshot via `mutate_graph_snapshot` (`daemon_ops.cpp:1199`); `index.files` is populated only
  by file extraction (`incremental_update.cpp:118-123`). So every rebuild produces a graph that
  has never seen them.
- There is **no re-injection for memory**. The only post-rebuild preservation is
  `ingest_all_drops()` (`daemon_server.cpp:270-279`), which re-overlays *semantic* fragments
  from disk and does not touch checkpoints.

Three triggers drop memory: every incremental code edit (`daemon_server.cpp:514`), the first
edit after a fast-load restart (`daemon_server.cpp:517-521`, `index_hydrated=false`), and the
every-5th full-dedup reconcile (`incremental_update.cpp:225-231`). v1's incidental survival via
`graph.json` fast-load (`daemon_server.cpp:362-380`) is fragile: the first edit after restart
re-rescans and wipes it.

The codebase already solved the structurally identical problem for host-authored semantic
nodes: **filesystem source-of-truth + re-overlay** (`daemon_server.cpp:268-269`: *"Re-overlays
every present fragment. Run after a deterministic rescan… would otherwise drop merged semantic
nodes"*). This change applies the same proven pattern to memory.

## What Changes (Design A)

- **Write a sidecar fragment at `remember` time.** Alongside the markdown body under
  `cgraph-out/memory/`, write a fragment JSON (the checkpoint node + its `concerns` edges)
  using the existing `to_json(const Fragment&)` serializer (`fragment_json.hpp:20`). The `.md`
  body remains the node's `source_file` (so the body stays snippet-readable). The sidecar — not
  the live snapshot — is the durable record.
- **Re-overlay memory fragments after every rebuild.** Add an `ingest_all_memory()` hook,
  parallel to `ingest_all_drops()`, that re-reads every memory sidecar from `cgraph-out/memory/`
  and merges it via `merge_fragment`/`mutate_graph_snapshot`. Call it at the **same three sites**
  as `ingest_all_drops()`: after a full rescan (`daemon_server.cpp:339`), after fast-load
  (`:375`), and after an incremental rebuild (`:524`).
- **Idempotent overlay.** Re-merge reuses `merge_fragment`'s first-occurrence-wins dedup
  (`graph_builder.cpp:101-138`), so re-applying the same sidecar never duplicates nodes/edges.
- **`graph.json` is not the source of truth for memory.** Exclude `memory:` nodes/edges from the
  persisted snapshot (at the daemon persist boundary, `daemon_lifecycle.cpp:148`) so the sidecar
  is the sole source of truth and there is no two-source coherence question.
- **Defensive recall.** Recall already resolves `concerns` targets with skip-on-miss; keep that
  so an edge to a renamed/removed code node degrades to "no link," never an error.
- **Keep memory and semantic drops separate.** Memory sidecars live under `cgraph-out/memory/`,
  semantic fragments under `cgraph-out/semantic-drop/`; distinct dirs, distinct hooks, distinct
  trust labels — so they can be managed (and later GC'd) independently.

## Goals

- `graph_remember` checkpoints survive a daemon **restart**.
- Checkpoints survive **incremental code edits**.
- Checkpoints survive **full rescans**.
- Memory stays excluded from centrality / analysis / code retrieval (unchanged from v1).
- Re-overlay is **idempotent** (no duplicate nodes/edges).
- `graph.json` is **not** the source of truth for memory.

## Non-Goals

- No GC/TTL (checkpoints accumulate; pruning is a later change).
- No vector / embedding memory search.
- No change to graph extraction (`index.files` still extraction-only).
- No snapshot carry-forward (rejected design C — fights the rebuild model, no cold-restart
  survival).
- No new first-class memory database/layer (rejected design B — second persistence model).

## Capabilities

- **graph-session-memory** (MODIFIED): checkpoints now durably survive restart, incremental
  edits, and full rescans via on-disk sidecar fragments re-overlaid at the rebuild hooks; the
  sidecar (not `graph.json`) is the source of truth.

## Impact

- Engine: `remember` writes a sidecar (`daemon_ops.cpp`); new `ingest_all_memory()` +
  three call sites and memory-dir priming in `daemon_server.cpp`; memory exclusion at the
  persist boundary (`daemon_lifecycle.cpp:148`).
- Tests: `daemon_ops_test` (sidecar written; idempotent re-overlay; exclusion unchanged),
  plus a daemon-level test for restart / incremental / rescan survival.
- On-disk: `cgraph-out/memory/<ts>-<slug>.md` (body) + `<ts>-<slug>.json` (sidecar fragment).
- Parity: additive; code-node `graph.json` output changes only by *removing* memory nodes that
  v1 incidentally wrote — code nodes are unchanged.
