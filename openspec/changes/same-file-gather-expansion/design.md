## Context

The default `context` path resolves one or more focal seeds, builds an undirected adjacency map from persisted graph edges, walks that map to depth three under the adaptive relevance gate, and packs reached nodes with the existing knapsack. The committed query-only retrieval gate records 0.224/0.315/0.382 recall at 2k/4k/8k. An earlier perfect-focal diagnostic found 34 beyond-two-hop misses in the focal symbol's own source file, but that diagnostic predates the current adaptive default; the implementation therefore begins as a measured, reversible experiment against the current committed fixture.

The fixture contains 154 non-empty source files. Nodes per file have median 5, p90 16, p95 23, and max 46. The initial cap-8 design improved tight-budget recall locally but regressed a frozen TypeScript transfer gate. The retained design caps at five and gives inferred same-file candidates lexical-overlap value only.

## Goals / Non-Goals

**Goals:**

- Admit symbols from the primary resolved focal's source file into adaptive context candidates without persisting new graph edges, ranking query-overlapping symbols first and valuing them below persisted graph neighbors.
- Keep admission deterministic, capped, deduplicated, and visible through the existing per-entry `via` field.
- Demonstrate a strict recall improvement at one or more committed budgets with no recall decrease at the other budgets before retaining the mechanism.
- Preserve fixed gather, serialized graph parity, token accounting, and request schemas.

**Non-Goals:**

- Expanding from every reached node's file; that multiplies fan-out and does not match the measured focal-file diagnostic.
- Traversing graph edges outward from admitted same-file candidates.
- Adding telemetry fields, relation types, persisted edges, embeddings, or fallback behavior.
- Regenerating the fixture or weakening tolerances to make the experiment pass.

## Decisions

### Candidate-only expansion from the primary resolved focal file

After the ordinary BFS completes, adaptive gathering will consider siblings sharing a non-empty `source_file` with the primary resolved focal. Admitted siblings enter `reached` at depth two with `via="same_file"`, but they are not pushed onto the BFS frontier.

This directly targets the measured same-file/focal gap and prevents one admitted sibling from opening an unrelated three-hop neighborhood. Persisted same-file edges were rejected because they would alter `graph.json` and every parity surface. Expanding every core node's file was rejected because it compounds candidate growth across each traversed file.

### Rank by query overlap, cap at five, and omit structural hop value

A non-file, non-memory sibling is eligible when it shares the primary focal's source file. Siblings are ordered by query overlap descending, centrality descending, then id ascending; the first five not already reached are admitted. In knapsack packing, same-file candidates receive query-overlap value only rather than the ordinary hop-value term.

The initial depth-1 hop value let siblings of an ambiguous lexical focal tie real graph neighbors and displaced relevant TypeScript symbols. Query-overlap-only value retains candidates for relevant matches without treating inferred file proximity as a persisted edge. Five is a measured code constant rather than a public option. The knapsack remains the final budget authority.

### Preserve fixed mode structurally

The entire same-file path is guarded by the existing `adaptive` boolean. `gather="fixed"` never builds the per-file index or changes `reached`, so its response stays byte-for-byte compatible.

### TDD and measured kill gate

First add a focused `daemon_ops_test.cpp` case and prove it fails before implementation. Then implement the smallest candidate-admission block. Run the committed query-only retrieval test and record exact recall before changing its baselines. Retain and re-baseline the feature only if at least one budget improves strictly and no budget decreases; otherwise remove the mechanism and close the change with the negative measurement.

Latency will be measured on the changed `context` op itself through repeated resident-daemon client calls, not through `scripts/benchmark_daemon_query.py`, whose current workload exercises the separate `query` op. Compare at least 50 warmed calls before and after on the same graph/query; a median regression greater than 10% blocks shipping pending profiling.

## Risks / Trade-offs

- [The old 17% diagnostic may not transfer to today's adaptive query-only path] → Treat current-fixture recall as the ship gate and preserve a negative result rather than forcing the feature through.
- [Non-overlapping focal-file symbols may dilute tight budgets] → Give same-file candidates lexical-overlap value only and require no recall decrease on both the local and frozen TypeScript gates.
- [Dense files may dilute knapsack selection] → Cap at five, rank deterministically, and require no recall decrease at any committed budget.
- [Extra indexing work may slow context] → Build the index only in adaptive mode, measure the actual `context` op, and block on >10% median regression.
- [A sibling may already be reachable through a real edge] → Preserve the existing shallower reach record and do not overwrite it.

## Migration Plan

No data migration is required. Deployment is the normal binary replacement. Rollback is removal of the candidate-admission block; serialized graphs and callers require no changes.

## Open Questions

None before the experiment. The measured gate decides whether the mechanism remains in the change.

## Measured Evidence

### Pre-change baseline

- `cgraph_retrieval_quality_test` on the unchanged engine: 0.223972 at 2k, 0.314825 at 4k, and 0.381758 at 8k (N=35, query-only, engine defaults); all committed gates passed.
- Provisional resident-daemon `context` latency before the graph was held constant: median 44.333354 ms over 50 calls. This value is not used for the ship comparison.
- Red test: the unchanged `cgraph_daemon_ops_test` exited at assertion code 131 because adaptive gathering returned only the persisted neighbor instead of that neighbor plus 16 eligible same-file candidates.

### Strategy comparison

- All lexical-seed files with a query-overlap eligibility gate: 0.223972 / 0.310063 / 0.380001; rejected because 4k and 8k regressed.
- Primary focal file with a query-overlap eligibility gate: 0.221375 / 0.314825 / 0.382598; rejected because 2k regressed.
- Primary focal file with depth-1 hop-valued, capped candidates: 0.234336 / 0.314825 / 0.381758 locally, but rejected after frozen-TypeScript recall regressed at 2k and 4k.
- Cap 4: 0.223972 / 0.314825 / 0.380001; rejected because the gain disappeared and 8k regressed.
- Cap 8: 0.234336 / 0.314825 / 0.381758 on the local fixture, before the transfer failure was measured.
- Cap 16: preserved the gain but context latency grew from 54.269021 ms unchanged-binary median to 115.562980 ms, then 298.728563 ms under repeat load; rejected.
- Cap 5 with depth-2, lexical-overlap-only value: 0.223972 / 0.314825 / 0.382598; accepted because 8k improved by 0.000840 and 2k/4k were unchanged.
- Frozen Cybertron graph (4,976 nodes, 5,443 edges, 74 usable rows): unchanged and final engines both measured exactly 0.2194554512 / 0.3163540126 / 0.4047348107.

### Apples-to-apples latency

- Unchanged main binary on the frozen 4,976-node Cybertron graph, five warmups plus two 50-call batches: 51.404875 ms and 51.602875 ms median.
- Final cap-5 depth-2 binary on the same graph/query: 51.372333 ms and 51.609312 ms median. No sustained latency regression.
