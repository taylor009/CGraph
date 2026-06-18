## Test strategy

The recall side is already gated in-engine; this change re-points that gate at the default path and
adds the latency dimension.

- **Parity gate (exists):** `cgraph_pack_context_parity_test` drives `context` with adaptive over the
  committed fixture and asserts the recall/cost deltas. After the flip it validates the *default*
  path. Recorded targets/tolerance unchanged (this change does not re-tune anything).
- **Default-path unit tests (update, red first):** the `daemon_ops_test` assertions that today expect
  the default response to be `gather:"fixed"`, `packing:"greedy"` flip to `"adaptive"`/`"knapsack"`;
  a new assertion confirms an explicit `gather:"fixed"` request still yields the old fixed/greedy
  bundle byte-for-byte (the override path is unchanged).
- **Latency benchmark (new, gating):** `scripts/benchmark_daemon_query.py` on the live daemon,
  before vs after, on a real graph. Record the per-query latency delta of the +11% candidate pool
  (depth-3 × multi-seed). This is the go/no-go signal the recall numbers don't capture.
- **Telemetry as before/after:** the adaptive-adoption op-stat (daemon_ops.cpp:1521, "pre-flip
  telemetry") becomes the post-flip measure — every default context call now records adaptive.

## Decisions

- **Flip via the `gather` default, not a separate packing flip.** `gather="adaptive"` already
  cascades to knapsack + depth-3 + θ-gate (the `use_knapsack = packing=="knapsack" || adaptive`
  coupling, daemon_ops.cpp:641). One default string change delivers the whole knee config; no new
  branching.
- **θ=0.05 unchanged.** It is the measured knee (θ=0.20 collapses to k2; 0.05–0.10 are equivalent).
  No tuning in this change.
- **Preserve `fixed` exactly.** The override path must stay byte-for-byte the old default so callers
  that depend on the 2-hop greedy bundle (and the parity contract for fixed) are unaffected.
- **Latency-gated, with a defined fallback.** If depth-3 latency on the live path is unacceptable,
  fall back to flipping `packing` greedy→knapsack only: +0.049 recall@8k at *zero* extra gather I/O
  (same k2 candidate set). This is the safe partial win and is captured as the contingency, not the
  primary.

## Validation that cannot be tested directly

- Absolute recall (0.569) is the offline harness number; the in-engine parity gate asserts the
  *delta* under the engine's real cost model, which is the durable contract. The live recall on
  arbitrary repos will vary; the gate guards the fixture-measured delta, not an absolute.
- Latency is environment-sensitive (graph size, disk, multi-seed breadth); the benchmark records a
  representative number rather than asserting a hard threshold in a unit test.
