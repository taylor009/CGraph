## Why

The semantic layer's value is entirely host-authoring-dependent and, today, unmeasured. cgraph
validates fragment **schema** but never fragment **value**: a thin `doc -> concept DESCRIBES` stub
and a richly code-connected fragment are indistinguishable in `status`. The dormant-layer trial
proved the failure mode (disconnected islands) was invisible — `node_count` went up, but nothing
said the docs connected to nothing.

A spike on the now-enriched graph shows the signal is real and actionable:

```
14 doc nodes — 50% reach a code node (transitively, within 2 hops); 50% are "floating"
6 concept nodes — 2 are orphans (no code link): the opsx-lifecycle and host-split abstractions
24 edges bridge semantic nodes into real code
```

Crucially, only 28% of docs link to code **directly**; the rest reach code **through a concept**
(`doc:claude-md -> concept:deterministic-pipeline -> merge_fragments`). A direct-only metric would
wrongly call those orphan, so connectivity must be measured **transitively**. The ~50% that never
reach code are genuinely floating workflow docs — exactly the thing worth surfacing.

## What Changes

- **Compute semantic connectivity deterministically** from the graph: doc-node count, concept-node
  count, how many doc nodes reach a real code node within a bounded number of hops (the "connected"
  set), the orphan docs (reach no code), orphan concepts (no code link), and the count of edges
  bridging semantic nodes into code.
- **Surface it in `status`** under a `semantic` key, alongside the existing `enrichment_*` fields,
  so "is the semantic layer landing connected?" is a number — not a vibe. A connectivity rate near
  0 (the thin-stub failure mode) is now visible at a glance.
- **Define a semantic node** by the skill's id-namespacing contract (`doc:` / `concept:` /
  `topic:`), so the metric needs no new node metadata and never miscounts a code node as semantic.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: the `status` op reports semantic connectivity — doc/concept counts, the
  transitive doc-to-code connectivity rate, orphan docs and concepts, and semantic->code bridge
  edges — computed from the current snapshot so it always reflects live enrichment.

## Non-Goals

- **No authoring or quality judgement.** The metric measures structural connectivity, not whether a
  fragment is *semantically* correct — that stays the host's responsibility.
- **No model logic.** Pure graph traversal over node ids and edges.
- **No gating.** Low connectivity is reported, never enforced; a sparsely connected layer is still
  valid and served.
- **No change to enrichment ingest or fragment validation.** This is a read-only metric over the
  existing snapshot.

## Impact

- New `src/engine/semantic_connectivity.{hpp,cpp}`: `compute_semantic_connectivity(const
  GraphSnapshot&, hop_bound)` returning the counts (pure, unit-testable). Register in
  `src/engine/CMakeLists.txt` + `tests/smoke/semantic_connectivity_test.cpp`.
- `src/engine/daemon_ops.cpp` (`status`): emit the `semantic` block from the helper.
- `status` payload gains a `semantic` object (additive; existing consumers unaffected).
- Tests: `semantic_connectivity_test` (transitive vs direct reachability, orphan docs/concepts,
  empty graph, a pure-code graph reports zero semantic nodes) and a `daemon_ops` assertion that
  `status` carries the block after a fragment with a doc->code edge is present.
