# Design — stabilize-parity-gate-fixture

## Test strategy

The artifact under change is the test harness itself, so "the test passes deterministically"
is the behavior to verify. Verification is concrete:

1. **Gate runs and passes on the fixture** — point the test at
   `tests/fixtures/pack_context_parity/` and confirm it reports PASS with the recorded
   targets and tolerance unchanged (budget-2000 knapsack ≈ 0.591, |Δ| ≤ 0.03; adaptive block
   PASS at 2k/4k). This is the same assertion logic, new input source.
2. **No skip when the fixture is present** — the test must reach the measurement and exit on
   the parity result, not the artifact-absent early return. Assert via a non-skip run (the
   fixture is committed, so the artifact-present branch is always taken).
3. **Determinism check (one-time, recorded in tasks)** — rebuilding the code-only graph twice
   yields a byte-identical `graph.json` (already verified: identical SHA across two rebuilds).
   This justifies committing a single blob rather than rebuilding in-test.

There is no new product behavior to unit-test; the "test" is that the gate is stable and
runs. The existing parity assertions are the coverage — they now run against a fixed input.

## Why a committed frozen pair (not an in-test rebuild)

Two routes were considered:

- **Route A — commit a frozen `{graph.json, queries.jsonl}` pair (chosen).** The clean build
  is byte-deterministic, so the committed graph is reproducible. The two files are internally
  consistent (their node ids cross-reference each other), making the gate independent of
  checkout path and daemon state. Matches existing fixture precedent
  (`tests/fixtures/daemon_query/graph.json`). Cost: ~1.2 MB of tracked blobs.
- **Route B — rebuild a code-only graph at test/configure time.** Rejected: the documented
  one-shot rebuild currently **crashes (SIGBUS) on `research/`** as an aggregate, so a test
  would have to special-case exclusion; node ids would be checkout-path-dependent (breaking
  the eval cross-reference on other machines); and it adds a build step and tree mutation to a
  smoke test. More moving parts, less determinism.

Route A is strictly more stable for a parity gate whose whole purpose is reproducibility.

## Fixture generation (documented, reproducible)

The fixture graph is the code-only deterministic build. Generation procedure (recorded so it
can be regenerated):

1. Move the untracked `research/` aside (it both pollutes the graph and crashes the one-shot).
2. `cgraph --root <repo> --out <tmp>` → `graph.json` (1,181 nodes, 1,521 edges, deterministic).
3. Copy `<tmp>/graph.json` → `tests/fixtures/pack_context_parity/graph.json`.
4. Copy the current `research/eval/queries.jsonl` verbatim →
   `tests/fixtures/pack_context_parity/queries.jsonl` (no edits).
5. Restore `research/`.

The targets in the test are NOT recomputed from this graph — they are the pre-existing
calibrated values, and the fixture is the graph that already reproduces them (0.591287 vs
0.591). If the fixture is ever regenerated and the numbers move, that is a real signal to
investigate, not a reason to retune targets (which would require its own proposal per the
research/eval discipline).

## Wiring

- `tests/smoke/CMakeLists.txt`: add
  `target_compile_definitions(... PRIVATE CGRAPH_PARITY_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/pack_context_parity")`,
  mirroring the existing `CGRAPH_REPO_ROOT` define.
- `pack_context_parity_test.cpp`: resolve `graph.json` / `queries.jsonl` from
  `CGRAPH_PARITY_FIXTURE_DIR`. Keep the existing absent-artifact skip as a defensive fallback
  (it should no longer trigger, since the fixture is committed).

## Untestable-directly notes

The "runs in CI" property can't be asserted from inside the test (it has no view of CI). It is
satisfied structurally: the fixture is committed, so the artifact-present branch is always
taken on any checkout. The determinism claim is verified once at fixture-generation time
(identical SHA across rebuilds), recorded in tasks, not re-asserted every run.
