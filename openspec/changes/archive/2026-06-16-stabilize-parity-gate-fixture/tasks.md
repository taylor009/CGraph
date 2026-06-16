# Tasks — stabilize-parity-gate-fixture

## 1. Generate and commit the fixture pair
- [x] 1.1 Verify the code-only build is byte-deterministic: with `research/` moved aside,
  run `cgraph --root <repo> --out <tmpA>` and `--out <tmpB>`; assert
  `shasum <tmpA>/graph.json == shasum <tmpB>/graph.json`. Record the SHA in the commit body.
  (Verified: both builds SHA `78208dc622a2147b5167eb9554674a8c9ea8772d`.)
- [x] 1.2 Copy the deterministic code-only `graph.json` (1,181 nodes / 1,521 edges) to
  `tests/fixtures/pack_context_parity/graph.json`.
- [x] 1.3 Copy the current `research/eval/queries.jsonl` **verbatim** to
  `tests/fixtures/pack_context_parity/queries.jsonl` (no label/grade/query/target edits).
- [x] 1.4 Confirm the pair is internally consistent: every grade-2 eval `node_id` resolves in
  the fixture `graph.json` (243/243). Confirmed code-only: 0 real `build/` or `research/`
  nodes (the 11 `_build_` id matches are source symbols like `build_state`).

## 2. Repoint the parity test at the fixture
- [x] 2.1 Add `CGRAPH_PARITY_FIXTURE_DIR` compile define for
  `cgraph_pack_context_parity_test` in `tests/smoke/CMakeLists.txt`, mirroring the existing
  `CGRAPH_REPO_ROOT` define.
- [x] 2.2 Update `pack_context_parity_test.cpp` to resolve `graph.json` / `queries.jsonl` from
  `CGRAPH_PARITY_FIXTURE_DIR`; keep the absent-artifact skip as a defensive fallback.
- [x] 2.3 Run `ctest --preset default -R pack_context_parity --output-on-failure`; confirmed it
  reaches the measurement (does NOT skip) and PASSES with targets 0.591/0.625/0.666 and
  tolerance 0.03 unchanged. Budget-2000 knapsack 0.589784 (|Δ|=0.0012); adaptive PASS at 2k/4k.

## 3. Verify stability and full suite
- [x] 3.1 Proved drift-immunity: with the fixture wired, repopulated `cgraph-out/graph.json`
  with the 10,997-node accumulated graph and re-ran the parity test — identical result
  (0.589784, PASS), confirming it reads only the fixture.
- [x] 3.2 Ran the full suite `ctest --preset default`: 59/59 passed, fixture-stable.
