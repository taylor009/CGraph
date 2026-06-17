## Why

`pack_context` (and the `query` op) resolve a focal node from a free-text query via
`matching_nodes` (daemon_ops.cpp:270-282): the **entire query string** must be a case-insensitive
substring of a node id or label, otherwise focal is null and `graph_context` returns the empty
`focus:null` branch (daemon_ops.cpp:601-606).

This fails for natural-language queries — the dominant agent query shape. A query like *"report
semantic connectivity of the enrichment layer"* is never a substring of a label like
`compute_semantic_connectivity(...)`. Measured offline on the clean graph
(`research/focal-resolution/results.md`, 1,354 nodes, 38 git-mined rows):

| focal resolution | NL queries resolved | grade-2 recall@8k |
|---|---:|---:|
| production (substring) | **0 / 35** | **0.000** |
| lexical top-1 | 35 / 35 | 0.212 |
| lexical multi-seed (top-5) | 35 / 35 | **0.321** (58% of the 0.556 perfect-focal ceiling) |

So production NL retrieval returns nothing — the validated knapsack packer (0.556-capable) never
runs. This is the mechanism behind the production query zero-hit rate. The engine *already computes*
`query_term_overlap` over `lexical_terms` (daemon_ops.cpp:172-224) for the adaptive gather gate and
knapsack value; it simply never uses it for focal resolution.

## What Changes

- **Lexical fallback (shared by `context` and `query`):** when exact-id/label and substring
  matching find nothing, rank nodes by `query_term_overlap(lexical_terms(query), label)` and resolve
  the focal from the top match. Exact/substring behavior is unchanged when it succeeds — precise
  symbol lookups (`merge_fragments`) never regress.
- **Multi-seed gather (`context`):** seed the undirected BFS from the top-N (N=5) lexical focals and
  union their ego graphs (dedup by shallowest depth), then pack once. This is the dominant lever —
  a single lexical top-1 is a truly relevant symbol only ~23% of the time; top-5 raises
  any-seed-relevant to ~43%.
- **Confidence floor:** if the best lexical overlap is below a threshold, the focal stays
  **unresolved** — return `suggestions` and record the context/query zero-hit, exactly as today.
  This preserves the honest "no match" path for genuinely off-topic queries and keeps the zero-hit
  signal meaningful instead of always-resolved.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: focal resolution for `context`/`query` SHALL fall back to lexical
  term-overlap (and `context` SHALL gather from multiple lexical seeds) when exact/substring
  matching is empty, while a below-threshold query SHALL remain an unresolved zero hit.

## Non-Goals

- **Embedding / identifier-aware focal ranking.** The remaining gap (0.321 → 0.556 ceiling) needs
  better focal *ranking*; that is the designated next experiment, not this change.
- **Changing the packer** (knapsack/LongCodeZip) or candidate gathering depth — held constant.
- **Graph construction, ID normalization, or output shape** — untouched (focal resolution is a
  query-time daemon concern, not a parity surface).

## Impact

- `src/engine/daemon_ops.cpp` (focal resolution in `pack_context` + the `query` op; multi-seed
  gather), `tests/smoke/daemon_ops_test.cpp`.
- Interacts with the existing "Context op contributes a zero-result signal" requirement: a
  lexically-resolved focus is no longer a zero hit; a below-threshold query still is.
- Offline-measured win: grade-2 recall@8k 0.000 → 0.321 (N=35, directional, tiktoken cost proxy).
  Validation re-measures on the real engine query path and confirms exact-lookup + zero-hit
  semantics are preserved.
