## Why

The committed end-to-end retrieval gate currently measures mean grade-2 recall of 0.224, 0.315, and 0.382 at 2k, 4k, and 8k token budgets, while an earlier ceiling diagnostic found that 34 of 197 beyond-two-hop misses were relevant symbols in the focal symbol's own file. The default adaptive gather can only traverse explicit graph edges, so query-relevant same-file symbols remain unnecessarily distant when no call or reference edge connects them.

## What Changes

- Extend the default `gather="adaptive"` context walk with deterministic, query-ranked same-file candidate expansion from the primary resolved focal.
- Bound expansion to five depth-2 candidates from the primary focal's source file so large files and ambiguous secondary seeds cannot flood the candidate set or tie direct graph neighbors.
- Value same-file candidates by lexical overlap only; unlike persisted graph neighbors, inferred same-file proximity does not receive a structural hop bonus in knapsack packing.
- Preserve `gather="fixed"` byte-for-byte and leave `graph.json`, extraction, node IDs, and edge parity unchanged.
- Gate the change on the committed query-only retrieval fixture: the mechanism ships only if it raises at least one recall budget without lowering any measured budget and without a material daemon-query latency regression.
- Add focused smoke coverage for adaptive inclusion, fixed-mode exclusion, deduplication, deterministic ordering, empty source paths, and large-file caps.

Non-goals:

- No persisted same-file edges or new graph relation types.
- No embeddings, models, or changes to focal ranking.
- No changes to knapsack weights or algorithm, token accounting, or the MCP request schema.
- No fixture regeneration to manufacture a gain.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `graph-daemon-client`: Adaptive context gathering admits bounded, query-relevant same-file candidates while fixed gathering and the serialized graph remain unchanged; the existing end-to-end gate measures and protects the resulting default-path recall.

## Impact

- Runtime behavior: `src/engine/daemon_ops.cpp` (`context` candidate gathering only).
- Verification: `tests/smoke/daemon_ops_test.cpp`, `tests/smoke/retrieval_quality_test.cpp`, existing pack-context and graph-parity tests, warmed resident-daemon `context` calls, and a frozen TypeScript-repository transfer run.
- Contract: delta requirements under `openspec/specs/graph-daemon-client/spec.md`.
- Dependencies and persisted formats: unchanged.
