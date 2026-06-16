# Design — graph-session-memory-overlay

## Test strategy (before implementation)

The survival properties are the whole point, so they are pinned with deterministic tests at
the layer each is reachable:

1. **Sidecar written at remember time** (engine test): after `remember`, a `*.json` sidecar
   exists beside the `*.md` under `cgraph-out/memory/`, and it parses via the existing
   `parse_fragment` into a fragment containing the checkpoint node + its `concerns` edges.
2. **Idempotent re-overlay** (engine test): applying the same memory fragment twice (via the
   overlay function) leaves exactly one checkpoint node and one `concerns` edge — first-wins
   dedup. Node/edge counts identical after the second overlay.
3. **Survives a full rescan** (engine test): build a snapshot with a memory node, run
   `full_stat_index_rescan` (which rebuilds from `index.files`), then the memory re-overlay;
   the checkpoint is present again and recall returns it. (The rescan-drop without overlay is
   already what v1 exhibits; this asserts the overlay restores it.)
4. **Survives incremental rebuild** (engine test): same shape against
   `apply_incremental_code_updates`.
5. **Survives restart** (daemon-level test or scripted live check): remember → stop daemon →
   start fresh daemon → recall returns the checkpoint, regardless of the fast-load-vs-rebuild
   path taken, because the overlay re-reads the sidecar.
6. **graph.json excludes memory** (engine test): after `remember`, the persisted node-link JSON
   for the daemon snapshot contains no `memory:` node; recall still works (sourced from the
   sidecar overlay).
7. **Exclusion unchanged** (existing v1 tests): memory still absent from centrality, `query`,
   and `pack_context` — the v1 `daemon_ops_test`/`analysis_test` blocks must stay green.

## Mechanism

### Sidecar write (in `remember`)
`remember` already builds a `Fragment` { checkpoint node, `concerns` edges } and merges it
(`daemon_ops.cpp:1199`). Add: serialize that same fragment with `to_json(const Fragment&)`
(`fragment_json.hpp:20`) and write it to `cgraph-out/memory/<ts>-<slug>.json` (same stem as the
`.md`). The body `.md` stays the node's `source_file`. The in-memory merge stays so the
checkpoint is immediately recall-able in the live session; the sidecar guarantees durability.

### Re-overlay hook (`ingest_all_memory`)
Mirror `ingest_all_drops` (`daemon_server.cpp:270-279`): scan `cgraph-out/memory/` for sidecar
`*.json`, validate+parse each (`validate_semantic_fragment_file` / `parse_fragment`), and merge
via `mutate_graph_snapshot(state, merge_fragment)`. Call it at the three rebuild sites that
already call `ingest_all_drops`:
- after `full_stat_index_rescan` in the `rescan` lambda (`daemon_server.cpp:339`)
- after `load_graph_snapshot` in `try_load_persisted` (`daemon_server.cpp:375`)
- after the incremental/first-edit rebuild in the serve loop (`daemon_server.cpp:524`)

Ordering matches drops: overlay runs after the code-only graph is built and before
publish-to-clients / persist, so a rebuilt graph is never served without memory re-applied.

### Idempotency
No new logic: `merge_fragment` skips nodes/edges whose id/edge-key already exists
(`graph_builder.cpp:113-120`). Re-applying a sidecar already present is a no-op. This also makes
the overlay safe to run alongside any incidental `graph.json` content.

### graph.json exclusion (sidecar = sole source of truth)
The daemon persist path serializes `to_node_link_json(*read_graph_snapshot(state))`
(`daemon_lifecycle.cpp:148`). Filter `memory:` nodes (and edges incident to them) out of the
snapshot copy before serializing on the **daemon persist path only** — not in the shared
`to_node_link_json` (which one-shot exports also use; one-shot has no memory). This removes the
two-source ambiguity: after a restart, memory comes back via the sidecar overlay, never via
`graph.json`. Code-node output is unchanged.

### Defensive recall (already present)
Recall resolves each `concerns` target with `by_id.find(...)` and skips misses, so a
dangling edge (target code node renamed/removed across a rebuild) degrades to "no link," never
an error. Keep this; add a test asserting a checkpoint whose target no longer exists still
recalls with an empty/!-shrunken `concerns` list.

## Separation of layers
Memory sidecars: `cgraph-out/memory/`. Semantic fragments: `cgraph-out/semantic-drop/`. Distinct
directories, distinct overlay functions, distinct discovery — so trust labeling and future GC
differ per layer and neither can clobber the other.

## Why not the alternatives (recorded)
- **Carry-forward (C)**: copy `memory:` nodes from the prior snapshot at rebuild time. Smaller
  code, but the concern scatters across every rebuild/publish path, and it does not survive a
  cold restart (nothing on disk to carry from). Rejected.
- **First-class memory DB (B)**: a second persisted layer + lifecycle to keep coherent with
  `graph.json`. More surface, more risk. Rejected.

## Untestable-directly notes
The restart test needs a real daemon process boundary; it is a scripted/integration check
rather than a pure unit test. Everything else is deterministic at the engine layer (overlay
function + rescan + persist serializer).

## Deferred (non-goals, noted)
GC/TTL for accumulating sidecars; stale-edge repair beyond skip-on-miss; vector recall.
