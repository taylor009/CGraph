## Why

The semantic enrichment layer is effectively dormant, and the one trial that ran it produced the
failure mode the `cgraph-enrich` skill explicitly warns against: thin `doc -> concept DESCRIBES`
fragments with **zero edges into the code graph** (see `cgraph-out/semantic-drop/chunk_00.json`).
The semantic nodes and the 1031-node code graph are disconnected islands, so the layer answers
nothing useful even where it was "activated."

The root cause is friction, not capability. Connecting a doc to the code it describes requires the
authoring host to run `graph_query` per symbol to discover real code-node ids — extra agentic work
that nobody pays, so the path of least resistance is a thin stub. cgraph owns the code graph and
can remove that friction deterministically.

A spike confirms the signal is real and high-precision. Matching doc tokens against code-node
symbol names, **filtered to compound identifiers** (snake_case / CamelCase) and ranked by rarity,
cleanly recovers the symbols a doc actually documents:

```
design.md  -> FileCacheEntry, IncrementalGraphIndex, SemanticCacheRecord, SemanticChunkPlan,
              classify_cached_file, files_hashed, files_stat_reused
CLAUDE.md  -> GraphSnapshot, analyze_graph, detect_communities, detect_project_files,
              extract_detected_file, code_poll_interval, ClientRuntimeHooks
```

Bare lowercase word collisions (`cache`, `change`, `count`, `daemon`) are dropped by the shape
filter. So cgraph can hand the host candidate doc->code links in the plan, making a connected
fragment the easy path instead of extra work.

## What Changes

- **Compute candidate code links at plan time (deterministic).** A new matcher builds a symbol
  index from the current code graph's node labels and, for each planned document, scans its text
  for mentions of those symbols. Matches are filtered by identifier shape (compound or
  capitalized type names accepted; bare lowercase dictionary words dropped), ranked by specificity
  (a name on one node beats a name on many), and capped per document.
- **Surface the candidates in `plan.json`.** Each chunk input gains a `candidate_links` array of
  `{id, label}` — the real code-node ids the authoring host should connect the doc to. cgraph
  suggests ids and evidence only; it does not invent the relation (the host picks `DESCRIBES` /
  `DOCUMENTS` / etc. from reading the doc).
- **Source the code graph for the CLI plan.** `enrich-plan` loads the persisted
  `<out>/graph.json` (written by a prior build or the daemon) to build the symbol index. If no
  graph is present, `candidate_links` is empty and planning proceeds unchanged — the feature is
  purely additive.
- **Update the `cgraph-enrich` skill** to author from `candidate_links` (emit `doc -> <id>` edges
  using the handed-over ids) instead of discovering ids via `graph_query`.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `semantic-fragment-ingest`: the chunk plan includes deterministic candidate doc->code links per
  document input, so host-authored fragments connect prose to real code nodes by default instead
  of producing disconnected stubs.

## Non-Goals

- **No model logic in the binary.** Matching is deterministic symbol-name scanning; cgraph never
  decides what a doc *means*, only which code symbols it mentions.
- **No invented relations.** Candidates are node ids + labels (evidence). The host chooses the edge
  relation; cgraph does not fabricate semantics.
- **No authoring automation.** This lowers the cost of good authoring; it does not author fragments
  or call any model.
- **No change to fragment validation or the content-hash cache.** Candidate links are advisory plan
  metadata; a fragment that ignores them is still valid.
- **No forced code-graph build in `enrich-plan`.** If `<out>/graph.json` is absent, candidates are
  simply empty — planning never blocks on a slow build it didn't previously do.

## Impact

- New `src/engine/semantic_code_links.{hpp,cpp}`: `build_symbol_index(const GraphSnapshot&)` and
  `compute_candidate_links(doc_text, index, max_links)` (pure, unit-testable). Register in
  `src/engine/CMakeLists.txt` + `tests/smoke/semantic_code_links_test.cpp`.
- `src/engine/semantic_orchestration.cpp` (`plan_enrichment`): load `<out>/graph.json`, build the
  index once, attach `candidate_links` to each document input when writing `plan.json`.
- `plan.json` schema gains `inputs[].candidate_links` (additive; existing consumers ignore it).
- `cgraph-enrich` skill doc updated to use `candidate_links`.
- Tests: `semantic_code_links_test` (compound accepted, bare-word dropped, rarity ranking, cap,
  media files skipped) and an assertion that `plan.json` carries candidate links for a doc that
  mentions a known code symbol.
