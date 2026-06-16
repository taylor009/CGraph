## Why

Adaptive relevance-gated gather shipped flag-gated and in-engine-revalidated
(`openspec/changes/archive/2026-06-16-adaptive-context-gathering/`), but it is **not yet useful to a
coding agent through MCP**, and we cannot judge a future default flip because the mode is invisible
end-to-end. Concretely, reading the real surfaces:

- **No discoverability.** The `graph_context` tool description still reads "greedily packed to fit a
  token budget" (`src/mcp/mcp_server.cpp:80-81`) and never names adaptive or *when* to use it. The
  `gather` enum (`:86-90`) is the only hint; an agent has no reason to reach for it. The
  `query`/`q` requirement (the gate is a silent no-op without query terms — `daemon_ops.cpp:599`,
  `626-630`) is undocumented.
- **Responses are not self-describing.** The knapsack/adaptive branch returns `gather` + `packing`
  (`daemon_ops.cpp:745-756`), but the greedy branch returns **neither** (`:795-803`). A caller
  cannot reliably tell which mode produced a bundle.
- **Adaptive's behavior is a black box.** Nothing in the response reports the one adaptive-specific
  fact — did the depth≥2 gate actually admit a third hop, and how much? `omitted` is a packing
  number (`candidates − chosen`, `:744`), not a reach number. An agent can only infer reach by
  counting `depth:3` entries; telemetry sees nothing.
- **Op-stats are blind to the mode.** `record(op, latency_ms, zero_hit)` (`daemon_ops.cpp:1114`)
  keys per-op only (`operation_stats.cpp:18`); there is no adaptive-vs-fixed split, so the durable
  ledger cannot report adaptive adoption or its latency. And `zero_hit` is set **only for `Query`**
  (`:1083`) — `Context` never flags "answered but found nothing", so a context call that resolves no
  focus or omits everything looks identical to a useful one.
- **No MCP-layer validation.** `tests/smoke/mcp_server_test.cpp` exercises query/explain/unknown but
  **never calls `graph_context`**; nothing proves the adaptive params even forward through MCP.

The flip ordering is therefore forced: **telemetry → docs/validation → observe live traffic →
flip.** You cannot assess flip-readiness until the response and ledger can see adaptive behavior,
and the docs/MCP work is what generates the live traffic to measure. This change does the first two
steps; the flip stays a separate, gated decision.

## What Changes

- **Self-describing context responses.** Every `context` response includes `gather`
  (`"fixed"`/`"adaptive"`) and `packing` (`"greedy"`/`"knapsack"`) in *both* branches — adds the two
  fields to the greedy branch (`daemon_ops.cpp:795-803`) so no inference is needed.
- **Adaptive reach summary.** When `gather = "adaptive"`, the response carries a small reach object
  reporting the candidate-pool size and how many candidates the gate admitted beyond the 2-hop core
  (vs. frontier nodes it rejected) — the signal that says whether adaptive did anything.
- **Context zero-result signal.** The `context` op records the op-stats zero-hit flag when the
  bundle is useless (focus unresolved, or every candidate omitted), mirroring query semantics
  (`daemon_ops.cpp:1083`, `1096-1098`, `1114`).
- **Per-mode op-stats.** The durable ledger tracks adaptive `context` calls separately from fixed,
  so a rollup can report adaptive adoption (and, via existing per-op latency, its cost).
- **MCP discoverability + forwarding test.** Rewrite the `graph_context` tool description to name
  both modes, say when adaptive helps, and note the `query` requirement; add an MCP-layer test that
  `gather`/`gather_theta` forward verbatim to the `context` op. README `graph_context` entry
  (`README.md:204`) gains a concrete adaptive example with the recall/cost framing.

## Goals

- Adaptive gather is **discoverable and self-describing** through MCP: an agent can find it, knows
  when to use it, and can read back which mode ran and whether the third hop expanded.
- The durable op-stats ledger can answer "how often is adaptive used, and at what latency / zero-hit
  rate" — the data a default-flip decision requires.
- MCP-layer test coverage pins the adaptive param forwarding that today is unverified.

## Non-Goals

- **No default flip.** `gather: "fixed"` and `packing: "greedy"` remain the defaults; flipping is a
  separate change gated on the live data this one makes visible (see `design.md` readiness gate).
- **No change to the gather algorithm, the gate threshold, packers, parity targets, or eval data.**
  This is observability + discoverability around the existing, validated mode.
- **No new relevance signal.** Reach reporting uses the existing `query_term_overlap` gate; no
  embedding/LLM.

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: `context` responses become self-describing (gather + packing in every
  branch) and report adaptive gather reach; the `context` op contributes a zero-hit signal and a
  per-mode usage count to the durable op-stats ledger; the `graph_context` MCP tool documents the
  adaptive mode and its params are forward-tested.

## Impact (affected files)

- `src/engine/daemon_ops.cpp` — add `gather`/`packing` to the greedy response branch (`:795-803`);
  add the adaptive reach summary to the knapsack branch (`:745-756`); count gate admissions/
  rejections in the BFS loop (`:624-630`); set `zero_hit` for `Context` at the record boundary
  (`:1096-1098`, `1114`).
- `src/engine/operation_stats.{hpp,cpp}` + `src/engine/include/cgraph/operation_stats.hpp` — carry an
  adaptive-`context` counter through `record`, the in-memory rollup, and the persisted JSONL line so
  the cross-session rollup can report it.
- `src/mcp/mcp_server.cpp` — rewrite the `graph_context` tool description (`:78-90`) to document
  adaptive + the `query` requirement (params already forward; no routing change).
- `tests/smoke/mcp_server_test.cpp` — add a `graph_context` forwarding case (`gather`/`gather_theta`
  reach `op=context`) and assert the description names adaptive.
- `tests/smoke/daemon_ops_test.cpp` — assert greedy responses carry `gather:"fixed"`/`packing`,
  adaptive responses carry the reach summary, and `context` zero-result sets the zero-hit signal.
- `tests/smoke/operation_stats_test.cpp` — assert adaptive `context` calls are counted distinctly
  and survive the persist/rollup round-trip.
- `README.md` — expand the `graph_context` entry with an adaptive example.
- No eval-data, ground-truth, gather-algorithm, or parity-target changes.
