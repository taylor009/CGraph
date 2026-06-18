## 1. Flip the default

- [x] 1.1 Updated `daemon_ops_test.cpp`: the default `context` response (no params) now asserts
      `gather:"adaptive"`, `packing:"knapsack"`, `reach` present; the prior fixed/greedy test now
      requests `gather:"fixed"` explicitly and asserts byte-for-byte old shape; the fixed-3-hop
      gather test also pins `gather:"fixed"`.
- [x] 1.2 Changed the `gather` default `"fixed"` → `"adaptive"` (daemon_ops.cpp:638); the cascade
      (knapsack, depth `kKnapsackContextDepth=3`, θ=0.05 gate) engages on the default path.
- [x] 1.3 `cgraph_daemon_ops_test` passes.

## 2. Recall gate on the default path

- [x] 2.1 `cgraph_pack_context_parity_test` passes; its greedy/fixed baselines now request
      `gather:"fixed"` explicitly (the default flipped), recorded targets/tolerance unchanged.
- [x] 2.2 Default context calls record `gather:"adaptive"` (confirmed live + the adaptive-adoption
      op-stat test passes), so the pre-flip telemetry now measures the default path.

## 3. Latency (gating)

- [x] 3.1 Measured on the live daemon (cgraph graph, 1,366 nodes, N=30, NL multi-seed query):
      fixed median 23.9ms → adaptive default 31.4ms = **+7.5ms (+32%)**, richer bundle (21→62
      packed nodes), still sub-100ms.
- [x] 3.2 Decision: **keep the gather flip** — +7.5ms is an acceptable cost for +12% recall@8k; no
      fallback to packing-only needed.

## 4. Verify

- [x] 4.1 Live daemon: default `context {"q":"<NL>"}` returns `gather:"adaptive"`/`packing:"knapsack"`
      with a `reach` summary; explicit `gather:"fixed"` returns the old fixed/greedy shape.
- [x] 4.2 Full suite `ctest --preset default` 59/59; parity goldens unchanged.
