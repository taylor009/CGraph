## Context

Host-authored semantic fragments are thin because connecting a doc to the code it describes
requires the host to discover real code-node ids via `graph_query` — friction that pushes
authoring toward disconnected `doc -> concept` stubs. cgraph owns the code graph and can compute
candidate doc->code links deterministically at plan time, making a connected fragment the easy path.

## Spike evidence (why symbol-name matching works)

Building `symbol_name -> [node_id]` from code-node labels and scanning a doc's tokens recovers the
symbols it documents. Raw matching has precision noise from English-word collisions; the fix is an
identifier-shape filter, confirmed on real docs:

| doc | strong (compound, kept) | weak (bare lowercase, dropped) |
|---|---|---|
| design.md | FileCacheEntry, SemanticChunkPlan, classify_cached_file, files_hashed | cache, change, count, daemon |
| CLAUDE.md | GraphSnapshot, analyze_graph, detect_communities, ClientRuntimeHooks | cache, client, connect |

## Matcher design (`semantic_code_links`)

```
struct CandidateLink { std::string node_id; std::string label; };

SymbolIndex build_symbol_index(const GraphSnapshot& graph):
  for each node: name = leading identifier of node.label
    record name -> node_id, and a per-name count (specificity)

std::vector<CandidateLink> compute_candidate_links(doc_text, index, max_links = 10):
  tokens = identifiers in doc_text (deduped)
  for each token matching a symbol name:
    accept if shape is high-precision:
      - compound: contains '_' OR internal CamelCase (/[a-z][A-Z]/)
      - OR Capitalized identifier whose matched node kind is type-like (class/struct/enum/...)
    reject bare all-lowercase words (cache, change, count, ...)
  rank: rarer name first (count asc), then longer token first
  cap at max_links
```

Pure functions over a `GraphSnapshot` and a string — no I/O, fully unit-testable. Media inputs
have no text and are skipped (no candidates).

### Precision policy

- **Shape filter** is the primary lever (the spike's finding): compound identifiers and capitalized
  type names are almost never coincidental; bare lowercase words almost always are.
- **Specificity ranking**: a name owned by one node is a stronger signal than one shared by many
  (e.g. an overloaded `size`). Rarer ranks higher.
- **Cap** per doc (default 10) so the host receives a focused list, not noise. The cap is a tunable
  constant, not a contract.

## Where the code graph comes from

`plan_enrichment` (CLI `enrich-plan`) is the path that writes `plan.json` for hosts — including
hosts driving the live daemon, which still call `enrich-plan` to get the manifest. It will:

1. Load `<out>/graph.json` via the existing graph loader (the daemon persists it there; a prior
   one-shot build also writes it).
2. If present, `build_symbol_index` once and attach `candidate_links` to each **document** input as
   the manifest is written.
3. If absent, emit empty `candidate_links` and proceed — additive, never blocks on a build.

The daemon's `run_enrichment_refresh` stays count-only (it computes pending/stale, not the host
manifest), so no daemon change is needed.

### Cost

Computing candidates reads each planned document's text once (media skipped). This is bounded to
the uncached docs already in the plan, and only when a graph is available. The symbol index is
built once per plan, O(nodes).

## Plan schema (additive)

```json
{"chunks": [{"index": 0, "fragment": "chunk_00.json", "inputs": [
  {"path": ".../design.md", "kind": "document", "content_hash": "...", "size": 1234,
   "candidate_links": [
     {"id": "users_..._semantic_chunk_plan_cpp_plan_semantic_chunks", "label": "plan_semantic_chunks(...)"},
     {"id": "users_..._file_cache_hpp_filecacheentry", "label": "FileCacheEntry"}
   ]}
]}]}
```

Existing consumers that don't read `candidate_links` are unaffected.

## Test strategy

- `semantic_code_links_test` (red first):
  - A doc mentioning a compound symbol (`classify_cached_file`) yields that node id as a candidate.
  - A doc mentioning only a bare lowercase word that is also a symbol name (`cache`) yields no
    candidate from that token (shape filter).
  - A capitalized type name (`GraphSnapshot`, kind=struct) is kept.
  - Specificity ranking: a unique name outranks a name shared by many nodes.
  - The result is capped at `max_links`.
  - Media/empty text yields no candidates.
- Orchestration/integration: `plan_enrichment` over a fixture with a doc that names a known code
  symbol writes `plan.json` whose input carries the matching `candidate_links`; with no
  `<out>/graph.json` present, `candidate_links` is empty and the plan is otherwise unchanged.

### Hard-to-test directly

Authoring *quality* (whether a host actually uses the candidates) is out of cgraph's control — the
deterministic contract is only that correct, high-precision candidates are offered. Measuring
realized connectivity is a separate concern (a future "semantic connectivity" metric), explicitly
not in scope here.

## Open questions

- Whether to also match doc **path/filename** against code file paths (a `daemon_server.md` next to
  `daemon_server.cpp`). Deferred: symbol-mention matching already gives strong precision; path
  proximity can be added later if recall is insufficient.
- Whether to eventually move candidate computation into the daemon so a live host could fetch links
  without the CLI. Deferred: the CLI plan path is the documented host surface today.
