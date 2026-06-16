## Context

The adaptive gather mode exists and is parity-gated (`tests/smoke/pack_context_parity_test.cpp:169-227`),
but it is invisible to the agents that would use it and to the telemetry that would justify making it
default. This change is observability + discoverability only — no change to *what* adaptive gathers,
only to *what callers and the ledger can see about it*. Every edit is additive to the response or the
stats record; the gather algorithm and its parity targets are untouched.

## Test strategy

Per `openspec/config.yaml` (TDD): each behavior change gets a focused test that fails first, then the
minimal implementation. All tests are smoke tests registered 1:1 in `tests/smoke/CMakeLists.txt`,
matching the existing source→test pairing convention.

- **Response self-description** (`daemon_ops_test.cpp`): drive the `context` op with greedy and with
  knapsack/adaptive; assert `result["gather"]` and `result["packing"]` are present and correct in
  *both* branches. The greedy assertion fails against today's code (greedy branch omits both —
  `daemon_ops.cpp:795-803`), which is the red test.
- **Adaptive reach summary** (`daemon_ops_test.cpp`): reuse the existing 3-hop gate fixture
  (`F→n1→{relevant, irrelevant}→{d3a,d3b}`). With a query matching the relevant depth-2 node and
  `gather=adaptive`, assert the reach summary reports ≥1 candidate admitted past hop 2; with a query
  matching nothing at depth 2, assert it reports 0 admitted (adaptive collapses to the k=2 core).
  This directly tests the gate's observable effect, not just that the field exists.
- **Context zero-hit** (`daemon_ops_test.cpp` + `operation_stats_test.cpp`): a `context` request whose
  focus does not resolve must record a zero-hit; a request returning focus + ≥1 included must not.
  Verified through `op_stats` state after `handle_daemon_request`, mirroring how the query zero-hit is
  already asserted.
- **Per-mode stats round-trip** (`operation_stats_test.cpp`): record N adaptive + M fixed `context`
  calls, persist the JSONL line, reload, and assert the cross-session rollup reports the adaptive
  count distinctly and additively (mirrors the existing rollup round-trip test for query counts).
- **MCP forwarding + discoverability** (`mcp_server_test.cpp`): a `tools/call` for `graph_context`
  with `gather:"adaptive"` + `gather_theta` must forward both to `op=context` (mirrors the existing
  query/explain forwarding asserts at `:37-52`); `tools/list` must include "adaptive" in the
  `graph_context` description. Today there is no `graph_context` coverage at all, so both are red.

### Behavior that cannot be unit-tested directly

- **Whether the docs actually drive adoption** is a live-traffic question, not a unit test. The
  README/description wording is validated only by review; its *effect* is measured by the per-mode
  ledger counter on real usage — which is exactly the data the flip gate below consumes.
- **In-engine recall** is already covered by the existing parity gate; this change must not move it.
  The verification command below re-runs that gate to prove no regression.

### Verification commands

```bash
export VCPKG_ROOT="$PWD/.vcpkg"
cmake --build build/default
ctest --preset default -R 'cgraph_daemon_ops_test|cgraph_mcp_server_test|cgraph_operation_stats_test'
CGRAPH_REPO_ROOT=$PWD ctest --preset default -R cgraph_pack_context_parity_test   # must stay green
ctest --preset default                                                            # full suite
```

## Reach summary shape

Additive object on the response, present only when `gather="adaptive"`:

```
"reach": { "candidates": <int>,        // total nodes gathered (== included + omitted)
           "expanded_past_core": <int>, // depth>=3 candidates the gate admitted
           "gated_at_core": <int> }     // depth-2 frontier nodes the gate rejected
```

`expanded_past_core == 0` is the honest signal that adaptive collapsed to k=2 for this query —
the case the flip gate must rule out on real traffic. The counts are accumulated in the BFS loop at
the existing gate site (`daemon_ops.cpp:624-630`): increment `gated_at_core` where the gate currently
`continue`s, and count depth≥3 candidates when assembling the candidate list (`:646-655`).

## Per-mode stats

`DaemonOpStats::record` currently takes `(op, latency_ms, zero_hit)`. The minimal, source-localized
extension adds an `adaptive_context` boolean (default false) that increments a dedicated counter,
persisted as one extra field on the JSONL line and summed in the cross-session rollup. This keeps the
fixed-layout latency histogram and the existing per-op schema untouched — the new counter is strictly
additive, so old ledger files still parse (the rollup reads the field with a 0 default, the same
tolerance the existing reader already uses for absent fields — `operation_stats.cpp:282`).

## Default-flip readiness gate (out of scope here; documented so the data has a purpose)

This change ships nothing flipped. A later flip change MUST clear, on real agent traffic from the new
ledger (not the eval set):

1. **Parity green** — `pack_context_parity_test.cpp:221` (adaptive ≥ greedy@k2 +0.03, ≤ k3 +0.02,
   smaller candidate pool). Non-negotiable; already the gate.
2. **Latency bound** — adaptive `context` p50 within a small, stated margin of fixed (the extra hop
   costs candidate I/O; the per-mode counter + per-op latency make this measurable).
3. **Zero-hit** — adaptive `context` zero-hit rate ≤ fixed (catches over-pruning on real queries),
   readable now that `context` emits zero-hit.
4. **Reach is real** — `expanded_past_core > 0` on a meaningful share of live adaptive calls; if
   adaptive routinely collapses to k=2, `gather_theta` is mistuned for real labels vs. the eval set.

## Risks

- **Response-shape change for the greedy branch.** Adding `gather`/`packing` to greedy responses is
  additive (new keys), but the existing adaptive requirement asserted adaptive "changes only which
  candidates the BFS collects, not the response shape" — this change MODIFIES that requirement so the
  spec stays internally consistent. Consumers that ignore unknown keys are unaffected.
- **Ledger schema drift.** The new counter is additive and read with a 0 default, so existing
  `op-stats-ledger.jsonl` files still roll up; no migration needed.
