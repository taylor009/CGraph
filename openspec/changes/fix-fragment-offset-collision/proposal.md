## Why

`plan_enrichment` names new fragment files by **counting** existing drops:

```cpp
const auto fragment_offset = discover_semantic_fragment_drops(drop_dir).size();  // semantic_orchestration.cpp:126
const auto fragment_index = fragment_offset + chunk.index;                        // :140
```

This is correct only when existing fragment numbers are contiguous (`chunk_00, chunk_01, ...`). When
they are **non-contiguous** — which happens whenever an earlier pass enriched a subset, or a chunk
was skipped — the count understates the next free number and the planner hands back names that
**collide with existing fragments**. A host that follows the plan and writes
`cgraph-out/semantic-drop/<chunk.fragment>` then **overwrites a prior pass's fragment**, silently
dropping its nodes and edges from the graph on the next ingest.

This was hit live: with `chunk_00, chunk_01, chunk_05, chunk_06` on disk (count 4), a fresh plan
numbered new fragments from `chunk_04`, clashing with the existing `chunk_05`/`chunk_06`
(CLAUDE.md and host-contract enrichment). Only a manual reroute avoided losing that work. The
skill's own contract promises the opposite — "offset fragment names past the fragments already
dropped so the drop directory **accumulates across passes** and a new pass never overwrites an
earlier one."

## What Changes

- Compute the fragment offset from the **maximum existing chunk index plus one**, not the count, so
  new fragment names are always strictly greater than every existing one and can never collide —
  regardless of gaps in the numbering. `SemanticFragmentDrop` already carries the parsed
  `chunk_index`, so this reads the value that is already available.
- With no existing fragments the offset is 0 (unchanged cold behavior).

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `semantic-fragment-ingest`: the chunk plan assigns each new fragment a filename strictly past the
  highest existing fragment number, so a new enrichment pass never overwrites a fragment from an
  earlier pass even when existing fragment numbers are non-contiguous.

## Non-Goals

- **No renumbering of existing fragments.** The fix only changes how *new* names are chosen; files
  already on disk are untouched.
- **No change to ingest, validation, caching, or source attribution.** Only the
  next-fragment-name computation changes.
- **No change to the cold path.** An empty drop dir still starts at `chunk_00`.

## Impact

- `src/engine/semantic_orchestration.cpp` (`plan_enrichment`): replace the count-based offset with
  `max(existing chunk_index) + 1`.
- `tests/smoke/semantic_orchestration_test.cpp`: a regression test that with non-contiguous
  existing fragments (e.g. `chunk_00`, `chunk_05`) a new plan assigns a fragment name greater than
  the highest existing index and does not collide with any existing file.
- No schema or output-format change; purely a correctness fix for incremental enrichment.
