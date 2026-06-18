## Why

`pack_context` defaults to the worst-measured retrieval config: `gather="fixed"`,
`packing="greedy"` (daemon_ops.cpp:634,638). On the clean graph that is **grade-2 recall@8k =
0.507** — the baseline of every sweep. Every better config is already implemented, parameterized,
and parity-tested; they are simply opt-in.

The ceiling diagnostic (`research/ceiling-diagnostic/results.md`) showed the misses are a
**gather-depth** problem — 100% of unreachable grade-2 symbols are 3–7 hops away, 0% disconnected —
not a missing-edge problem. The sweep (`research/gather-default/results.md`) then measured the knee:

| config | recall@8k | cand tokens |
|---|---:|---:|
| greedy + fixed k2 (current default) | 0.507 | 12.5k |
| knapsack + adaptive θ=0.05 (the knee) | **0.569 (+0.062, +12%)** | 13.9k (+11%) |
| knapsack + fixed k3 | 0.603 | 21.3k (+70%) |
| knapsack + fixed k4 | 0.600 | 51.7k (strictly worse) |

`gather="adaptive"` already cascades to knapsack + `kKnapsackContextDepth=3` + the θ=0.05 relevance
gate — the exact knee. And the spec requirement *"In-engine revalidation gates adaptive gather"*
defined the precondition for flipping the default: the recall gain must be reproduced in-engine by a
parity test. `cgraph_pack_context_parity_test` does that and passes. **The precondition is met; this
is the flip the spec staged for** (daemon_ops.cpp:1521 even pre-instruments adaptive adoption).

## What Changes

- Flip the `pack_context` default `gather` from `"fixed"` to `"adaptive"` (daemon_ops.cpp:638),
  which cascades to knapsack packing + depth-3 + the θ=0.05 gate. Default recall@8k 0.507 → 0.569.
- `gather="fixed"`, `packing`, and `max_depth` remain caller overrides — `fixed` stays byte-for-byte
  the old default behavior, and an explicit `k3`/`k4` remains available; the engine never *defaults*
  to k4.
- Add a live-path latency check (`scripts/benchmark_daemon_query.py`) to the verification: the +11%
  candidate pool means ~11% more `read_source_snippet` reads per query (compounded by multi-seed
  focal resolution), so the flip is gated on acceptable real latency, not just recall.

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: the `context` default gather becomes `"adaptive"` (knapsack + depth-3 +
  θ=0.05); `"fixed"` is preserved as an explicit override; the parity gate that authorized the flip
  continues to guard the recall/cost deltas against regression.

## Non-Goals

- **Changing the adaptive mechanism or θ** — θ=0.05 is the measured knee and is unchanged.
- **Defaulting to fixed k3/k4** — k3 buys more recall at +70% I/O (caller-overridable); k4 is
  strictly worse and never a default.
- **Enrichment / new edges** — the diagnostic decoupled enrichment from this recall ceiling (nothing
  disconnected); pursue it as a capability, not for this metric.
- **Focal resolution** — shipped separately (lexical-focal-resolution).

## Impact

- `src/engine/daemon_ops.cpp` (one default string), `tests/smoke/daemon_ops_test.cpp` (default-path
  expectations now adaptive/knapsack), the existing `cgraph_pack_context_parity_test` (now exercises
  the default path).
- **Latency is the gating risk** — validate with `scripts/benchmark_daemon_query.py` before/after.
  Fallback if depth-3 latency is unacceptable: flip `packing` only (greedy→knapsack), which keeps
  +0.049 recall@8k at *zero* extra gather I/O (k2 candidate pool unchanged).
- Verified by: parity gate (recall delta in-engine), default-path unit tests, full suite, and a
  recorded before/after latency benchmark on the live daemon path.
