# Graph session memory (checkpoint / recall)

## Why

Claude Code long-session token usage is dominated by context that never gets released:
86% of usage occurs at >150k context and 84% comes from sessions active 8+ hours. Agents
hoard context because `/compact` and `/clear` lose task state — there is nowhere durable to
park "what I was doing and which code it touched."

cgraph is uniquely positioned to be that durable layer, and the investigation
(`research/agent-primitives-synthesis` exploration) showed the substrate is already there:

- **cgraph state is external to Claude's context.** `/clear` is a Claude Code context
  operation; it never touches `graphd` or `cgraph-out/graph.json`. The daemon persists the
  graph every 30s and on exit (`daemon_lifecycle.cpp:131,177`) and fast-loads it on restart
  (`daemon_lifecycle.cpp:162`), keyed one-per-project-root (`daemon_identity.cpp:40`). So a
  checkpoint written before `/clear` is trivially still there after.
- **The data model already permits memory nodes.** `Node.properties` is an arbitrary
  `string→string` map (`types.hpp:42`); `Node.kind` and `Edge.relation` are free-form strings
  with no enum (`types.hpp:39,48`); IDs are minted via `make_id` (`normalize.cpp:111`) and the
  engine already namespaces non-code nodes by id prefix (`semantic_connectivity.cpp:12-14`,
  `doc:`/`concept:`/`topic:`). A `memory:checkpoint:<ts>` node is legal today.
- **Recall can reuse existing machinery.** `graph_context` (`daemon_ops.cpp:564`) already
  resolves a focal node and returns its source snippet + neighbors packed to a token budget
  (default 6000, `:41`), and `with_source` (`daemon_ops.cpp:147`) reads a node's `source_file`
  into a capped snippet (40 lines / 2000 chars, `:23-24`). If a checkpoint node's
  `source_file` points at a markdown file, its body is already snippet-readable.

Four gaps block this today:
1. **No live write verb.** The only write path is the `enrich-ingest` CLI batch, which
   rebuilds the base graph and re-exports (`main.cpp:95`) — far too heavy for "append one
   checkpoint mid-session."
2. **Bodies aren't surfaced.** `node_brief` emits `id/label/kind/source_file/line/centrality`
   but **not** `properties` (`daemon_ops.cpp:80-95`), so memory stuffed into `properties`
   would be stored but never returned. (Hence the `source_file`→markdown design.)
3. **No recency recall.** `query_graph` is code-name substring; there is no "newest
   checkpoint" verb and nodes carry no created-at ordering.
4. **Pollution risk.** Memory nodes would distort `analyze_graph` centrality
   (`analysis.cpp:152`) and `detect_communities` (`analysis.cpp:96`) exactly as the 9,639
   `research/` nodes silently broke the pack_context parity gate — they must be inert to code
   analysis and retrieval scoring.

## What Changes (v1)

- **`graph_remember`** (new write op) — `{title, body, touches?: [id|name], tags?}`.
  - Writes the body to a markdown file **only** under `cgraph-out/memory/` (sandboxed path;
    no traversal outside it).
  - Body is **size-capped**; an oversized body is rejected (graph unchanged, error returned).
  - Mints a `memory:checkpoint:<iso-timestamp>` node with `kind:"checkpoint"`,
    `label` = title, `source_file` = the markdown file, `confidence: Inferred`, and
    `properties{created_at, tags}`.
  - Adds `concerns` edges from the checkpoint to each `touches` entry that **resolves to an
    existing node**; unresolved entries are reported, never turned into dangling edges.
  - Merges through the existing single-writer path (`mutate_graph_snapshot`,
    `daemon_ops.cpp:1079`) — not the heavy `enrich-ingest` batch.
  - The checkpoint fragment is persisted under `cgraph-out/memory/` and **re-overlaid after a
    full rescan/restart**, reusing the same survival mechanism semantic drops use
    (`ingest_all_drops`), so a subsequent full rebuild from `index.files` does not drop it.
- **`graph_recall`** (new read op) — `{query?, limit?}`.
  - Returns `memory:checkpoint:*` nodes **newest-first** (by `created_at`), optionally
    filtered by `query` over title/tags.
  - Each entry carries the checkpoint **body snippet** (via `with_source`) plus **briefs of
    its linked code nodes** (the `concerns` targets).
  - Response size is bounded (a `limit` and the existing snippet caps).
- **Analysis/retrieval exclusion** — nodes whose id is in the `memory:` namespace are
  excluded from centrality (`analyze_graph`), community detection (`detect_communities`), and
  from candidate gathering in `pack_context`, so they never alter the scores or rankings of
  code nodes.
- **Docs** — a workflow doc describing the discipline:
  - before `/compact` or `/clear`: `graph_remember(title, body, touches)`
  - after `/clear`: `graph_recall()` to restore the thread
  - then `graph_context` on the linked nodes to recover bounded code context
  - never persist raw Playwright DOMs or chain-of-thought — only distilled summaries

## Goals

- An agent can checkpoint task state mid-session and recover it after `/clear` from a
  bounded (~KB) payload, instead of replaying 150k of history.
- Memory is completely inert to code analysis and code retrieval: zero change to existing
  query/context/impact rankings.
- Recall is reliable about recency (newest-first) and links back to the live code.

## Non-Goals

- No vector / embedding search (recall is recency + lexical over title/tags).
- No automatic summarization — the agent authors the body; cgraph stores it verbatim.
- No raw browser/MCP payload storage and no chain-of-thought storage.
- No GC/TTL in v1 (accumulation risk is documented; pruning is a follow-up unless trivial).
- No default behavior change to any existing retrieval op.
- No centrality/community pollution.

## Capabilities

- **graph-session-memory** (ADDED): the `remember`/`recall` ops, the `memory:` namespace
  contract, the analysis/retrieval exclusion, and the persistence/re-overlay guarantee.

## Impact

- Engine: new ops in `DaemonOp` (`operation_stats.hpp:65`), `daemon_op_name` /
  `daemon_op_from_string` (`operation_stats.cpp:44,49`), and the dispatch switch
  (`daemon_ops.cpp:1107`); the `memory:` exclusion in `analysis.cpp` (`analyze_graph:152`,
  `detect_communities:96`) and in `pack_context` candidate gathering; a memory-overlay hook
  alongside `ingest_all_drops`.
- MCP: `graph_remember` / `graph_recall` tools in `src/mcp/mcp_server.cpp` forwarding to the
  new ops.
- Tests: `daemon_ops_test` (write sandbox, oversize reject, recall ordering, snippet-readable
  body, resolved-only `concerns` edges, analysis exclusion), `mcp_server_test` (both tools
  forward), `analysis_test` (memory nodes get no centrality/community).
- New on-disk artifacts: `cgraph-out/memory/<ts>-<slug>.md` + a memory fragment file.
- Parity: additive; existing op responses and `graph.json` code-node output unchanged.
