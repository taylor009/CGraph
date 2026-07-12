# Tasks: End-to-End Retrieval Gate

## 1. Contract fixes (small, independent)

- [x] 1.1 Widen the `recall` query filter (`src/engine/daemon_ops.cpp` ~1531-1537) to also
      match case-insensitively against the checkpoint body text read from `source_file`
      (reuse the existing body-read machinery; memory nodes only).
- [x] 1.2 Add a `daemon_ops_test.cpp` regression case: `remember` a checkpoint whose body
      (not title, not tags) contains a unique term; `recall` with that term returns it;
      `recall` with a term in none of title/tags/body returns nothing.
- [x] 1.3 Correct `graph_context`'s MCP description and `gather` schema text
      (`src/mcp/mcp_server.cpp` ~92, ~102-104) to state `adaptive` is the default and
      `fixed` is the opt-out; correct the stale comment at `src/engine/daemon_ops.cpp` ~808.
      Do NOT change the engine default at ~811.

## 2. End-to-end gate (red-green)

- [x] 2.1 Add `tests/smoke/retrieval_quality_test.cpp`: load the committed fixture pair
      (`tests/fixtures/pack_context_parity/{graph.json,queries.jsonl}`), and for each
      symbol-granularity row call `handle_daemon_request` with op `context` and params
      `{q: row.query, budget}` only (no id, no gather/packing/depth overrides). Compute mean
      grade-2 recall at budgets 2k/4k/8k; unresolved rows count as recall 0. Assert against a
      placeholder baseline that MUST fail (red).
- [x] 2.2 Register the test in `tests/smoke/CMakeLists.txt` as its own executable + `add_test`
      with `cgraph_set_warnings` / `cgraph_enable_sanitizers`, passing the fixture dir the same
      way the parity test receives `CGRAPH_PARITY_FIXTURE_DIR`.
- [x] 2.3 Measure the real end-to-end recall on the fixture; record the exact numbers and the
      command in this change.
- [x] 2.4 Prove the gate bites: locally break the free-text resolution path (e.g. invert the
      `3efa8e0` exact-match gate or disable the lexical fallback), rebuild, observe the gate go
      red; revert the local break and observe green. Do not commit the broken state.
- [x] 2.5 Pin the baseline constants (measured value, tolerance 0.03) and decide which budgets
      are gated vs neutral based on the measured spread (mirror the parity test's approach).
      Test goes green.

## 3. Eval tooling promotion

- [x] 3.1 `git add scripts/bootstrap_eval.py .research-eval.toml`; add a short "Regenerating
      the eval fixture" note in `tests/fixtures/pack_context_parity/` (README.md) documenting
      the exact regeneration command and the graph the current numbers were measured on
      (node/edge counts).

## 4. Verification

- [x] 4.1 Full suite: `ctest --preset default` green including the new gate; report pass/fail
      counts (expect 62/62).
- [x] 4.2 Confirm no parity surface changed: `cgraph_pack_context_parity_test` and the
      extractor goldens still pass unchanged.
- [x] 4.3 Record before/after evidence in the change: recall-by-body test output, the measured
      end-to-end recall table, and the bite-proof (red) run output.

## Evidence (recorded at implementation, 2026-07-12)

- Measured end-to-end grade-2 recall on the committed fixture (N=35 symbol rows,
  q-only, engine defaults; graph 1181 nodes / 1521 links):
  `2000 -> 0.223972`, `4000 -> 0.314825`, `8000 -> 0.381758`.
  Command: `./build/default/tests/smoke/cgraph_retrieval_quality_test`.
- Bite-proof: with the lexical fallback locally disabled
  (`seeds = lexical_matches(...)` short-circuited), all three budgets measured
  `0.000` and the gate FAILED; revert restored the numbers above. Broken state
  never committed.
- Baselines pinned at the measured values, tolerance 0.03, all budgets gated
  (the end-to-end spread is monotone and well-separated; no neutral budget).
- recall-by-body: daemon_ops_test exit 0 with the new cases ("next y" -> body-only
  hit returns "Refactor charge"; "zzqx" -> empty).
- Full suite: `ctest --preset default` -> 100% tests passed, 0 failed out of 62
  (was 61; the new gate is #62). Parity + extractor goldens re-run explicitly:
  3/3 passed (`ctest -R 'pack_context_parity|extractor_goldens|retrieval_quality'`).
- For context, the focal-injected parity numbers on the same fixture remain
  0.591/0.625/0.666 -- the ~0.3 gap between injected and end-to-end recall is the
  focal-resolution loss this gate now watches.
