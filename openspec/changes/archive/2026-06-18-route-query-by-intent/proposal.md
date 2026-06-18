## Why

cgraph now has both retrieval halves: precise **structural** primitives (`explain` with a typed
`relation` filter, `impact`) and a validated **semantic** path (lexical multi-seed focal + adaptive
gather + knapsack packing). What is missing is the connective tissue every comparable system
converges on — RANGER (arXiv:2509.25257) names it outright: a **dual-stage pipeline that routes by
query type**, fast structural lookups for entity queries vs exploration for NL.

Today the agent's natural first call, `graph_query`, is a flat case-insensitive name search. To
answer a structural question — "who calls `buildApp`?" — the agent must *know* to orchestrate
`graph_query` → take the id → `graph_explain(id, relation:CALLS, direction:in)`. A naive query gets a
flat symbol list even when the intent was structural. The typed primitive exists; the routing to it
does not.

The intent decision is **deterministic** — an exact match against the symbol table, the lexical
shape of the query (single identifier vs multi-word sentence), and a small fixed grammar of
structural-intent phrases. No model is involved, so this stays inside the host-skill contract
(cgraph owns deterministic graph/text work; hosts own model logic).

## What Changes

`graph_query` classifies each query into one of three routes and shapes its response accordingly,
tagging the route taken:

- **Entity** — the query exactly resolves to a symbol (id or label). Return that node plus a compact
  typed-neighbor summary (callers / callees / references counts and top-N ids) inline, so the common
  "find `buildApp` and who calls it" is answered in one call instead of three.
- **Structural phrase** — the query matches a fixed intent grammar (`callers of X`, `who calls X`,
  `callees of X` / `what does X call`, `references to X` / `uses of X`, `implementations of X` /
  `who implements X` / `subclasses of X`, `importers of X` / `who imports X`). Resolve `X`; return
  the matching typed neighbors directly (the same set `explain` would, filtered to that relation and
  direction).
- **Lexical search** — anything else (a natural-language phrase, a partial name). Return today's
  importance-ranked / lexical-overlap symbol list, unchanged.

Every response carries a `route` field. `graph_query`'s MCP description is updated to advertise that
it answers structural questions directly. Op-stats optionally records the route distribution
(telemetry, mirroring the existing adaptive-context counter).

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: the `query` op gains deterministic intent routing — exact-entity and
  structural-phrase queries are answered through typed traversal, everything else through the
  existing lexical search; responses self-describe the route. The `graph_query` MCP tool advertises
  this.

## Non-Goals

- **Taking over `graph_context`'s job.** Routing the "natural-language" class stays a *symbol list*,
  not a token-budgeted bundle. `graph_context` remains the tool for packing a context bundle; the
  agent still chooses it when it wants snippets packed to a budget.
- **A model/LLM intent classifier.** Classification is deterministic lexical/structural rules only.
  An ambiguous or unrecognized query falls through to today's lexical search — worst case is exactly
  the status quo.
- **Removing or changing `graph_explain` / `graph_impact` / `graph_context`.** They remain explicit
  primitives for agents that already know what they want; routing is additive.
- **A new `graph_find` op** (the rejected Shape A) — `graph_query` is already the documented "start
  here" tool, so making *it* route avoids a fourth overlapping retrieval tool.

## Impact

- `src/engine/daemon_ops.cpp` (`query_graph` dispatch + a deterministic `classify_query_intent`
  helper and structural-phrase parser), `src/mcp/mcp_server.cpp` (`graph_query` description),
  `tests/smoke/daemon_ops_test.cpp`, optional op-stats route counter (`op_stats`).
- **Parity:** `query` is a daemon op, not a `graph.json` surface — not a parity gate. Additive and
  default-safe (unrecognized intent = current behavior).
- **Measurement:** the current git-mined NL eval cannot measure entity/structural routing. A small
  research probe (an entity/structural query set for classification accuracy, plus the synthesis
  "testable nugget" — precision and token cost of a typed neighbor bundle vs the untyped dump) is the
  honest validation; parked as a research task, not a blocker for the unit-tested classifier.
- Verified by: classifier + phrase-parser unit tests (each route, plus ambiguous → search
  fallthrough); full suite; parity goldens unchanged.
