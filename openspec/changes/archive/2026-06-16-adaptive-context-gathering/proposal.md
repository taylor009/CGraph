## Why

`graph_context`/`pack_context` gathers candidates by a **fixed k-hop undirected BFS** from the
focal node (`daemon_ops.cpp:603-625`), then packs them under budget. Depth is fixed by the packing
mode: greedy → `kDefaultContextDepth = 2` (`daemon_ops.cpp:42`), knapsack → `kKnapsackContextDepth =
3` (`daemon_ops.cpp:45`). The BFS expands **every** neighbor unconditionally at every depth below
the cap (`daemon_ops.cpp:618-624`).

The offline Route 1 harness (`research/`) established that the binding constraint on grade-2
recall is **candidate-set construction**, not packing — and that fixed-k has a bad recall/cost
curve:

- baseline (k=2 + greedy) mean grade-2 recall **0.474**
- k=3 + knapsack **0.564** (**+0.090**) but candidate tokens **+96%** (11.9k → 23.2k)
- **adaptive relevance-gated gather + knapsack: 0.531 (+0.057), candidate tokens +13%** (→ 13.4k)

Adaptive captures ~63% of k=3's recall gain for ~13% of its extra reach cost: it keeps the full
k=2 core but only expands *past* 2 hops along edges whose source node is query-relevant, instead of
pulling the whole 3-hop ball (most of which is the same community — see the AdaGReS redundancy
finding in `research/2510.00446/results.md`). The relevance signal it needs already exists in the
engine: `query_term_overlap` (`daemon_ops.cpp:207`) over `lexical_terms` (`daemon_ops.cpp:167`),
already used as the knapsack value at `daemon_ops.cpp:667`.

These are **Python-harness numbers — evidence, not acceptance.** Step A
(`research/2510.00446/results.md`) showed a Python packing win can vanish under the engine's
JSON-entry token accounting, so the gain MUST be revalidated through the engine before any default
flip (see Validation + the explicit revalidation note in `design.md`).

## What Changes

- Add an **adaptive, relevance-gated gather mode** to `pack_context`, behind a new flag
  `gather: "fixed" | "adaptive"` (default `"fixed"` — current behavior byte-for-byte unchanged).
- When `gather = "adaptive"`: keep the full depth-0/1 expansion (the k=2 core), but expand a
  node at depth ≥ 2 only if its `query_term_overlap` with the query ≥ `gather_theta`
  (default ~0.05), to a max depth of `kKnapsackContextDepth` (3). Pairs with `packing = "knapsack"`
  (the validated packer).
- The change is a localized gate inside the existing BFS loop (`daemon_ops.cpp:607-625`); it reuses
  the existing relevance helpers and the existing knapsack path. No new scoring model, no LLM.

## Goals

- A flag-gated retrieval mode that improves grade-2 recall@budget materially over the k=2 baseline
  at a **fraction of k=3's candidate-token cost**, validated **in-engine** (not just in Python).
- Zero change to the default (`gather: "fixed"`) path; the flag is the rollback boundary.
- Token cost of expansion is a first-class, measured acceptance criterion.

## Non-Goals

- **No default flip in this change.** `fixed` stays the default; flipping to `adaptive` (or
  `knapsack`) is a separate decision gated on the parity/benchmark results here.
- **No embedding/LLM relevance signal.** The gate is the existing deterministic lexical overlap.
  (`research` showed a static embedding value did not beat lexical for packing; an embedding *gate*
  is possible future work but needs a model the binary does not have.)
- **No change to focal resolution** (the ~28% of grade-2 symbols not graph-near any seed is a
  separate follow-up; adaptive gather widens reach from a given focal, it does not fix the seed).
- **No change to the greedy or knapsack packers themselves**, the parity targets, or eval data.

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: `pack_context` gains an `adaptive` gather mode (flag-gated, default off)
  that relevance-gates BFS expansion past depth 1, reaching query-relevant symbols beyond 2 hops
  without the full 3-hop fan-out; surfaced through the `context` op and `graph_context` tool params.

## Impact (affected files)

- `src/engine/daemon_ops.cpp` — `pack_context` (`:564`): read `gather`/`gather_theta` params
  (beside `packing` at `:569`); add the depth≥2 relevance gate in the BFS loop (`:607-625`,
  specifically before the neighbor expansion at `:618`); adaptive implies depth `kKnapsackContextDepth`
  and the knapsack fill (`:658`). Reuses `lexical_terms` (`:167`), `query_term_overlap` (`:207`).
- `tests/smoke/daemon_ops_test.cpp` — unit test for the gate (a low-relevance depth-2 node's
  neighbors are NOT pulled; a relevant one's are), beside the existing `context` coverage (`:216-257`).
- `tests/smoke/pack_context_parity_test.cpp` — extend the existing knapsack parity gate (`:1-15`)
  to also drive `gather=adaptive` and assert the in-engine recall/candidate-cost deltas match the
  research evidence within tolerance; adaptive stays non-default until this passes.
- `src/mcp/mcp_server.cpp` — (optional, doc-only) note the `gather`/`gather_theta` params in the
  `graph_context` tool description; params already flow through verbatim, so no logic change.
- No eval-data, ground-truth, greedy-path, or parity-target changes.
