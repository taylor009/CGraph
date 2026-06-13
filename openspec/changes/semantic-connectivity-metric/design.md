## Context

Enrichment value is unmeasured: `status` shows `enrichment_pending`/`node_count` but nothing about
whether authored fragments actually connect prose to code. This change adds a deterministic
connectivity metric over the snapshot and surfaces it in `status`.

## What counts as a semantic node

The `cgraph-enrich` skill mandates namespaced ids (`doc:`, `concept:`, `topic:`) precisely so
semantic nodes never collide with code nodes. The metric uses that contract: a node is semantic iff
its id has one of those prefixes; everything else is a "code node". This needs no new node metadata
and cannot misclassify a code node.

## Connectivity is transitive (the spike's key finding)

On the enriched graph, only 28% of docs link to code directly; 50% reach code within 2 hops
(`doc -> concept -> code`), because rich fragments route through shared concepts. So a doc is
"connected" if it reaches **any** code node within a small hop bound:

```
connected(doc) = BFS from doc over the undirected edge set, bounded to `hop_bound` hops,
                 returns true on the first non-semantic (code) node reached
```

`hop_bound` defaults to 2 (doc -> concept -> code), the natural depth for the skill's
doc/concept/code authoring shape. It is a tunable constant, not a contract.

## Metric shape

```cpp
struct SemanticConnectivity {
  std::size_t doc_nodes;        // ids starting doc:
  std::size_t concept_nodes;    // ids starting concept: (topic: counted with concepts)
  std::size_t connected_docs;   // doc nodes reaching a code node within hop_bound
  std::size_t orphan_docs;      // doc_nodes - connected_docs
  std::size_t orphan_concepts;  // concept nodes with no edge (any direction) to a code node
  std::size_t doc_code_edges;   // edges from a semantic node directly to a code node
  double connectivity_rate;     // connected_docs / doc_nodes (0 when no docs)
};

SemanticConnectivity compute_semantic_connectivity(const GraphSnapshot&, std::size_t hop_bound = 2);
```

Pure: builds an undirected adjacency map once (O(edges)), then a bounded BFS per doc node. For a
realistic graph (hundreds of docs, thousands of edges) this is sub-millisecond, so it is computed
on demand in `status` — which keeps it always correct after a live enrichment ingest (the ingest
path merges nodes/edges without re-running analysis, so a stored metric would go stale).

## status payload (additive)

```json
"semantic": {
  "doc_nodes": 14, "concept_nodes": 6,
  "connected_docs": 7, "orphan_docs": 7, "connectivity_rate": 0.5,
  "orphan_concepts": 2, "doc_code_edges": 24
}
```

A connectivity_rate near 0 is the thin-stub failure mode made visible.

## Test strategy

- `semantic_connectivity_test` (red first):
  - Direct edge `doc -> code` counts the doc as connected (1 hop).
  - `doc -> concept -> code` counts the doc as connected at hop_bound 2 but NOT at hop_bound 1
    (proves transitive measurement and the bound).
  - A `doc` with only a `doc -> concept` edge whose concept has no code link is an orphan doc.
  - A concept with no code edge is an orphan concept; one with a code edge is not.
  - `doc_code_edges` counts only semantic->code edges, not semantic->semantic.
  - A pure-code graph reports all-zero semantic counts and `connectivity_rate == 0` (no divide by
    zero).
- `daemon_ops_test`: after publishing a snapshot containing a `doc -> code` edge, `status.semantic`
  reports `doc_nodes >= 1` and `connectivity_rate > 0`.

### Cannot be tested directly

Whether the connectivity number *should* be high for a given repo is a judgement (some docs are
legitimately about external process, not code — the ~50% floating opsx docs). The metric reports
the structural fact; interpreting "good enough" is the operator's call.

## Open questions

- Whether to also surface `semantic` in the one-shot `stats.json`. Deferred: enrichment is a
  daemon-resident concern; the one-shot build has an empty semantic layer. `status` is the surface.
- Whether to break `connected_docs` down by hop distance (direct vs via-concept). Deferred: the
  single rate is the headline; distance histograms can come later if useful.
