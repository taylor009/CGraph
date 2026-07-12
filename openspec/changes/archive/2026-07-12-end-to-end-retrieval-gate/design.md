# Design: End-to-End Retrieval Gate

## Context

`tests/smoke/pack_context_parity_test.cpp` gates the gather/pack stages against the committed
fixture pair `tests/fixtures/pack_context_parity/{graph.json,queries.jsonl}` (38 git-mined,
graded rows; byte-identical to `research/eval/queries.jsonl`). It resolves each row's focal
itself (highest-centrality grade-2 node) and injects it as `{"id", row.focal}` — deliberately
isolating packing, but leaving the production entry path (free-text `q` → `matching_nodes` →
lexical multi-seed focal resolution → gather → pack) with zero automated quality coverage.
The route-2 entity-routing bug (`3efa8e0`) lived on exactly that uncovered path.

Two contract lies ride along: `mcp_server.cpp` advertises `gather` default `fixed` while
`daemon_ops.cpp:811` defaults `adaptive`; `recall`'s `query` filter matches title/tags but not
the body `remember` tells agents to write their substance into (`daemon_ops.cpp:1531-1537`).

## Goals / Non-Goals

**Goals:**
- A committed, CTest-run gate over the *default* end-to-end retrieval path: `q`-only `context`
  calls, engine defaults for gather/packing/depth, mean grade-2 recall@budget vs a pinned
  baseline.
- The gate demonstrably bites: a deliberate local regression on the resolution path goes red.
- `recall` finds checkpoints by body content; MCP schema text matches engine defaults.
- `scripts/bootstrap_eval.py` + `.research-eval.toml` tracked as the fixture-regeneration path.

**Non-Goals:**
- No change to engine retrieval behavior or defaults (gather stays `adaptive`; routing untouched).
- No new fixture; no changes to `graph.json` output or any Graphify parity surface.
- No pagination/offset work; no Windows work; `research/` stays untracked.

## Decisions

1. **Separate test target, same fixture.** A new `retrieval_quality_test.cpp` rather than a
   section in the parity test: the parity test pins the focal to isolate packing; this gate
   deliberately does not. Separate CTest targets mean a failure names its surface. Reusing the
   committed fixture keeps the two gates comparable (same rows, same graph) and avoids a second
   1.1 MB fixture.
2. **Measure the default path, not a tuned one.** Calls send only `{q, budget}` — whatever the
   engine defaults to (today: adaptive gather, knapsack, depth 3) is what agents get, so that is
   what the gate protects. If a future change flips a default, the gate re-measures the new
   reality rather than a lab configuration.
3. **Rows that fail to resolve count as recall 0, not skipped.** Resolution failure is the
   production failure mode this gate exists to catch (the route-2 bug made *every* row resolve
   badly). Skipping unresolved rows would blind the gate to exactly that regression class.
4. **Baseline = measured constant + tolerance, pinned red-green.** Write the assertion with a
   placeholder that must fail, measure the real number on the fixture, then pin
   `kBaseline - kTol` (tolerance 0.03, matching the parity test). Before pinning, prove the gate
   bites by locally breaking the resolution path (e.g. reverting the `3efa8e0` gate condition or
   disabling the lexical fallback) and observing red; the broken build is never committed.
5. **`recall` body matching reads the sidecar body.** The filter at `daemon_ops.cpp:1531-1537`
   additionally matches `contains_ci` over the checkpoint's body text (read via the existing
   snippet machinery from `source_file`). Only memory nodes are candidates, bodies are small
   markdown files, and `recall` already reads bodies for every *returned* entry — reading them
   at filter time changes constant factors, not complexity.
6. **Docs follow code on the gather default.** The adaptive default was a measured, deliberate
   flip (recall@8k 0.507 → 0.569, archived `default-adaptive-context-gather`). The schema text
   and the stale comment at `daemon_ops.cpp:808` are what change.

## Risks / Trade-offs

- [End-to-end recall may be low in absolute terms (free-text subjects are noisy)] → The gate
  asserts non-regression against the measured baseline, not an aspirational threshold; the
  absolute number is recorded in the change for context.
- [Fixture queries are commit subjects, not real agent phrasing] → Accepted: they are the only
  graded ground truth available, and the same rows already back the packing gate. The
  regeneration tooling (`bootstrap_eval.py`) is promoted in this change so the set can grow.
- [Reading bodies during recall filtering adds I/O per memory node] → Bounded by the memory
  inventory size; measured as negligible for realistic checkpoint counts (< hundreds).
- [Baseline could be pinned against an accidentally-broken build] → Mitigated by the mandatory
  bite-proof step (deliberate regression → red → revert → green) before pinning.

## Open Questions

None — all decisions above are settled; measurement happens during implementation.
