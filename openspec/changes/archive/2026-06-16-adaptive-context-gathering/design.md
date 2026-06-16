## Context

`pack_context` (`daemon_ops.cpp:564`) does: resolve focal → undirected BFS to `max_depth` →
collect candidates → sort (depth, centrality, label) → pack (greedy default, or knapsack behind the
`packing` flag). The BFS (`:603-625`) is the gather step this change targets. Today it expands every
neighbor of every node below the depth cap (`:618-624`); the only knob is the depth cap itself,
selected from the packing mode (`:570`). The research shows the depth cap is a blunt instrument:
k=2 under-reaches (recall 0.474), k=3 over-reaches (+96% tokens for +0.090).

## Architecture

### The gate, in the existing BFS

Adaptive gather changes one thing: **whether a node expands**, not the loop shape.

```
pop current @ depth
if depth >= max_depth: continue                      # existing cap (adaptive: max_depth = 3)
if gather == "adaptive" and depth >= 2:              # NEW gate — only past the k=2 core
    if query_term_overlap(query_terms, current.label) < gather_theta: continue
for (to, rel) in adjacency[current]: ... enqueue     # existing expansion (:618-624)
```

- **Depth 0 and 1 always expand** → the full k=2 ego graph is always present (adaptive never loses
  recall vs k=2). Only depth-2 nodes are gated, so the *third* hop is selective.
- The gate reuses `query_term_overlap` (`:207`) / `lexical_terms` (`:167`) — already computed for the
  knapsack value (`:667`), so the query terms are already in scope on the knapsack path.
- `gather = "adaptive"` implies `max_depth = kKnapsackContextDepth` (3) and the knapsack fill; it is
  meaningless with greedy/k=2 (gate never fires at depth ≥ 2 when cap is 2).

### Parameters (mirror the `packing` flag pattern at `:569`)

- `gather`: `"fixed"` (default) | `"adaptive"`. Default `fixed` ⇒ byte-identical current behavior.
- `gather_theta`: double, default `0.05` (the research knee; θ=0.20 collapses to k=2, θ=0.0 equals
  k=3). Clamp to [0,1].

### Why a flag, not a default

`fixed` remains the default and the rollback boundary, exactly like `packing="greedy"` is today
(`:566-569`). Nothing about the live default path changes until a separate decision, gated on the
in-engine results below.

### Data shapes / response

Unchanged. Adaptive only changes which candidates the existing knapsack sees; the `context` response
shape (`focus`/`included`/`omitted`/`tokens_used`) is identical.

## Rollout plan

1. Land adaptive **behind `gather: "adaptive"`, default `fixed`** — no behavior change for any
   existing caller. (This change.)
2. Extend the parity gate (below) so adaptive cannot become default until it reproduces the
   research deltas through the engine's real accounting.
3. Benchmark candidate-gather latency (extra `read_source_snippet` per candidate vs k=2/k=3).
4. **Separate, later decision** (not this change): if parity + benchmark hold, propose flipping the
   default gather/packing. Rollback = set `gather` back to `fixed` (one param) or revert the gate.

## Validation plan

- **Unit (`daemon_ops_test.cpp`):** on a small fixture graph, a depth-2 node with
  `query_term_overlap < theta` does NOT contribute its depth-3 neighbors; one with `>= theta` does;
  and `gather="fixed"` is unchanged. Test-first (red) per the repo TDD config.
- **In-engine parity (`pack_context_parity_test.cpp`, extended):** drive the `context` op with
  `gather=adaptive`, `packing=knapsack` over the 35-row eval and assert, under the engine's real
  cost model (char/4 + snippet cap), that grade-2 recall ≥ baseline + 0.04 and candidate-token cost
  is far below k=3 — matching the research evidence within tolerance. Adaptive is **non-default
  until this passes**, mirroring the existing knapsack gate (`pack_context_parity_test.cpp:1-15`).
- **Benchmark:** mean candidate count + gather wall-time, fixed-k2 vs adaptive vs k3, on a real
  query, recorded in the change.
- **Acceptance is the in-engine run, not the Python harness.**

## Risks

- **Engine cost-model divergence (load-bearing).** Step A (`research/2510.00446/results.md`) showed
  the engine's `estimate_tokens(full.dump())` JSON-entry accounting erased a Python knapsack win.
  The adaptive recall gain is candidate-set-driven (less cost-model-sensitive than packing), but it
  MUST be reproduced in-engine before trusting it. → the parity gate above.
- **Lexical gate weakness.** `query_term_overlap` on labels is sparse; for queries that don't
  lexically match the relevant third-hop node's *source label*, adaptive under-expands (it cannot
  reach the ~28% of grade-2 symbols not graph-near any seed — that is the focal-resolution
  follow-up, out of scope). θ needs tuning; default 0.05 is the research knee, not a universal.
- **Latency.** Adaptive reads fewer candidates than k=3 but more than k=2 (each candidate costs one
  `read_source_snippet`, `:105`); the gate itself is O(neighbors). Net should be < k=3; measure.
- **Graph dependency / reproducibility.** The research deltas were measured on a 10,997-node graph,
  not the 1,251-node graph the eval was originally bootstrapped on; re-validate on the canonical
  eval graph and record node/edge counts (per CLAUDE.md Research/Eval Discipline).

## Explicit revalidation note

**The Python harness result (`research/recall_lever.py`: 0.474 → 0.531, +0.057, +13% tokens) is
evidence only.** It was produced with a tiktoken cost proxy over uncapped source slices and a
held-perfect focal — not the engine's real path. Before this mode ships or its default flips, the
gain MUST be reproduced through the engine's actual accounting (char/4 over the capped snippet, the
JSON-entry overhead, and production focal resolution) via the extended `pack_context_parity_test`.
If it does not reproduce in-engine, the mode stays flag-gated-off and the result is parked, not
shipped — exactly as the knapsack packing path is gated today.

## Test strategy

Pure-ish and deterministic: the gate is lexical and deterministic, so both the unit gate test and
the parity recall numbers are reproducible. The parity test SKIPS when the eval/graph artifacts are
absent (CI-safe), and is the real gate locally where they exist — same posture as the current
knapsack parity test.
