# Stabilize the pack_context parity gate on a committed fixture graph

## Why

The knapsack/adaptive parity gate (`tests/smoke/pack_context_parity_test.cpp`) measures mean
grade-2 recall against recorded model-4 targets (0.591 / 0.625 / 0.666). It loads its graph
and eval from the **mutable working tree**:

- `pack_context_parity_test.cpp:52` — `root / "cgraph-out" / "graph.json"`
- `pack_context_parity_test.cpp:53` — `root / "research" / "eval" / "queries.jsonl"`
- `pack_context_parity_test.cpp:55-59` — when either is absent the test **SKIPS (exit 0)**.

Both inputs are gitignored, regenerated artifacts. `cgraph-out/graph.json` is whatever the
local daemon last persisted — a snapshot that silently accretes whatever roots/dirs the
daemon watched. We observed it grow to **10,997 nodes, of which 9,639 (88%) were `research/`
nodes** plus 123 `build/` nodes — none of which existed when the targets were calibrated, and
**0 of the 273 eval targets live in `research/`**. That pure dilution recomputed global
centrality and inflated neighborhoods, dropping budget-2000 knapsack recall from the
calibrated **0.591 to 0.561** and failing the gate by 0.0003 past its tolerance — with no code
change. A clean code-only rebuild (1,181 nodes) restores **0.591287** (|Δ|=0.0003, PASS).

So the gate has two defects:
1. **Locally flaky** — it asserts fixed targets against a graph that drifts with daemon
   activity, producing false failures unrelated to the code under test.
2. **No gate in CI** — on a clean checkout the artifacts are absent, so it skips. The one
   place we want a stable parity gate is the one place it never runs.

The clean code-only build is **byte-deterministic** (two rebuilds produce an identical SHA),
so a committed fixture pair is reproducible and fixes both defects at once.

## What Changes

- Add a committed, version-controlled fixture pair under
  `tests/fixtures/pack_context_parity/`:
  - `graph.json` — the deterministic **code-only** graph (src/tests/scripts; no `research/`,
    no `build/`), the same graph the model-4 targets were calibrated on.
  - `queries.jsonl` — a **verbatim snapshot** of the current eval set (no label edits, no
    target changes; the discipline against modifying eval data is preserved).
- Repoint the parity test at the fixture dir (a new `CGRAPH_PARITY_FIXTURE_DIR` compile
  define) instead of `cgraph-out/` + `research/eval/`.
- Because the fixture is always present, the gate **runs in CI** rather than skipping; the
  skip path is retained only as a defensive fallback if the fixture is ever missing.
- The recorded targets (0.591 / 0.625 / 0.666) and tolerance (0.03) are **unchanged** — the
  fixture graph reproduces them exactly, so this is a stability fix, not a recalibration.

## Goals

- The parity gate is deterministic and reproducible: same fixture → same numbers, on any
  checkout, independent of daemon state or working-tree drift.
- The gate actually runs in CI (no silent skip), so a real recall regression is caught.
- No change to the measured contract: same targets, same tolerance, same metric.

## Non-Goals

- No change to `pack_context`, the `context` op, or any retrieval/packing behavior.
- No change to eval labels, grades, queries, or the recorded targets (no metric loosening).
- Not fixing the separate `research/` one-shot SIGBUS crash, and not changing what dirs the
  daemon watches — those are tracked as their own issues.
- No new daemon op, no MCP change.

## Capabilities

- **graph-daemon-client** (MODIFIED): the in-engine parity gate measures against a committed
  deterministic fixture and runs in CI; the artifact-absent skip becomes a fallback.

## Impact

- New tracked fixtures: `tests/fixtures/pack_context_parity/{graph.json (~1.1 MB),
  queries.jsonl (~105 KB)}`. Larger than the existing 631-byte `daemon_query/graph.json`
  fixture, but a one-time deterministic blob; regenerating it is a documented, reproducible
  step.
- `tests/smoke/CMakeLists.txt` — add the `CGRAPH_PARITY_FIXTURE_DIR` define
  (precedent: `CGRAPH_REPO_ROOT` at `tests/smoke/CMakeLists.txt:695`).
- `tests/smoke/pack_context_parity_test.cpp` — read the fixture dir; keep the skip as a
  fallback.
- Known tradeoff: fixture node ids carry absolute paths (`users_taylorgagne_tools_cgraph_…`),
  matching the eval ids they cross-reference. They are internally consistent within the
  frozen pair (the test only matches ids between the two files), so the gate is
  checkout-path-independent. Normalizing ids to repo-relative is a possible follow-up, out of
  scope here to avoid touching eval labels.
