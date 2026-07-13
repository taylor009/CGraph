## 1. Baseline and Red Test

- [x] 1.1 Configure and build the unchanged worktree, run `cgraph_retrieval_quality_test`, and record the exact 2k/4k/8k recall in this change before implementation.
- [x] 1.2 Measure at least 50 warmed resident-daemon `context` calls on one fixed graph/query and record the pre-change median latency in this change.
- [x] 1.3 Add focused same-file adaptive/fixed, deduplication, empty-source, deterministic-cap coverage to `tests/smoke/daemon_ops_test.cpp`; run it against the unchanged engine and record the expected failing assertion.

## 2. Candidate Gathering

- [x] 2.1 Implement deterministic, query-ranked, five-per-file depth-2 same-file candidate admission for the primary resolved focal in `src/engine/daemon_ops.cpp`, without frontier expansion or fixed-mode changes, and give inferred candidates lexical-overlap value only.
- [x] 2.2 Run `cgraph_daemon_ops_test` and confirm the new focused coverage passes.

## 3. Measured Ship Gate

- [x] 3.1 Run `cgraph_retrieval_quality_test` before changing its targets; record exact after-change recall and retain the mechanism only if at least one budget improves and none decreases.
- [x] 3.2 If the recall gate passes, update only the measured baselines in `tests/smoke/retrieval_quality_test.cpp`; if it fails, remove the implementation and close the change with the negative result.
- [x] 3.3 Repeat the same 50-call warmed `context` benchmark and block shipping if median latency regresses by more than 10%.

## 4. Regression and Real-Flow Verification

- [x] 4.1 Run `cgraph_pack_context_parity_test`, `cgraph_extractor_goldens_test`, and graph parity tests; confirm persisted graph outputs remain byte-identical.
- [x] 4.2 Run the full default CTest suite and record the pass count (62/62 passed on 2026-07-13).
- [x] 4.3 Run a real query-only `context` flow against a separately built TypeScript repository and confirm recall does not regress on that topology.
- [x] 4.4 Run `openspec validate same-file-gather-expansion --strict` and `git diff --check`.

## 5. Delivery

- [ ] 5.1 Commit the validated OpenSpec artifacts, source, and tests in logical commits; push `same-file-gather-expansion` and open a PR with the recall, parity, latency, and real-flow evidence.
