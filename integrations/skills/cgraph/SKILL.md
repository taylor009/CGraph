---
name: cgraph
description: "Use FIRST for any question about THIS codebase's structure, symbols, or relationships — where a function/class/file is defined, what calls or imports it, what breaks if you change it, how two parts connect, or to load focused source context before editing or reviewing. Routes to the cgraph MCP tools (graph_query / graph_explain / graph_impact / graph_path / graph_context), which serve ranked file:line results, node neighborhoods, transitive blast radius, and token-budgeted source bundles from a resident per-project graph daemon in ~10ms — far cheaper than grepping and reading files. Prefer over blind grep/read for code navigation, dependency tracing, and impact analysis."
trigger: /cgraph
---

# cgraph

cgraph keeps a live, queryable graph of the current project — symbols (functions,
classes, types, files), their call/import/inheritance/containment edges, and
centrality ranking — served by a resident per-project daemon through MCP tools.

**Reach for these tools before grepping or reading files** when a question is
about code structure or relationships. A graph call is one ~10ms round-trip and
returns exactly the file:line (and often the source) you need, instead of many
grep/read calls that burn context.

## Routing: question → tool

| When the user / task needs… | Call |
| --- | --- |
| "Where is X? Find the symbol named …" | `graph_query` `{query}` — ranked by importance, each hit has `source_file` + `line` |
| "What is X? Show its callers/callees/imports" | `graph_explain` `{id}` — node + neighbors (each with `direction` and a navigable brief) + a source snippet |
| "What breaks if I change X? What depends on it?" | `graph_impact` `{id, direction:"dependents"}` — transitive blast radius, bounded by `max_depth` |
| "What does X rely on?" | `graph_impact` `{id, direction:"dependencies"}` |
| "How does A connect to B?" | `graph_path` `{source, target}` — shortest path, with `path_nodes` briefs |
| "Load context on X" / before editing or reviewing X | `graph_context` `{query or id, budget}` — focal node + most-relevant neighbors with snippets, packed to a token budget |
| "Is the graph current? / I just changed files" | `graph_status`, then `graph_update {path:"."}` if stale |

## How to use the results

- `graph_query` matches case-insensitively and can be narrowed with `kind`
  (e.g. `"function"`, `"class"`, `"file"`), `file` (source-path substring), and
  `limit`. On zero matches it returns `suggestions` — the closest symbol names —
  so correct the spelling and retry instead of falling back to grep.
- Every id-taking tool (`graph_explain` / `graph_impact` / `graph_path` /
  `graph_context`) also accepts a bare symbol name (e.g. `"merge_fragments"`);
  the response echoes the canonical `id` it resolved. A miss comes back with
  `found: false` plus `suggestions`.
- `graph_query` returns ids and `source_file`:`line`. Open the file at `line`
  directly — no second search needed.
- `graph_explain` takes `direction` (`"in"` = callers/importers, `"out"` =
  callees/imports) and `limit`; neighbors are ordered most-important-first and
  `neighbor_count`/`truncated` flag when a hub has more edges than returned.
- `graph_context` is the highest-leverage call before an edit or review: ask for
  a budget (e.g. `4000`) and it returns the focal symbol's source plus its
  neighborhood's source, ranked and trimmed to fit. Read that instead of opening
  files one by one. It reports `omitted` / `truncated` if more existed.
- `graph_impact` with `dependents` is the safety check before changing a
  signature or deleting a symbol: it lists everything that would be affected,
  by depth.

## Practicalities

- The `cgraph` MCP server must be registered (it auto-spawns a per-project daemon
  keyed to the project root). The first tool call in a project triggers a
  one-time graph build (seconds), then queries are warm (~10ms).
- While that first build runs, results carry `"graph_state": "building"` — an
  empty result then means "not built yet", not "no match". Retry after a few
  seconds or poll `graph_status` until `build_state` is `"ready"`.
- After editing source, a `graph_update {path:"."}` rescan refreshes the graph;
  `graph_status` shows node/edge counts and freshness.
- Fall back to grep/read only when cgraph genuinely has no answer — e.g. a
  string literal, a comment, a config value, or a file type cgraph does not
  extract. For symbols and their relationships, prefer the graph.
- cgraph indexes code structure deterministically. Prose/doc relationships
  appear only if semantic enrichment fragments have been ingested; don't assume
  documentation concepts are present unless `graph_status` shows enrichment.
