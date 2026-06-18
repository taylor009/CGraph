# Design — deterministic intent routing for the query op

## The classifier

`classify_query_intent(needle, graph) -> Intent` is a pure function over the query string and the
snapshot's symbol table. Evaluated in priority order; the first match wins:

```
1. STRUCTURAL PHRASE   needle matches the fixed intent grammar AND the extracted operand resolves
                       (via resolve_node — lenient: the phrase is an explicit intent signal)
                       -> { relation, direction, target_node }
2. ENTITY              needle is whitespace-free AND matching_nodes(needle) returns EXACTLY ONE node
                       whose id or (case-insensitive) label / bare-symbol equals the needle
                       -> { target_node }
3. LEXICAL (default)   anything else                 -> {}  (today's behavior)
```

**Why ENTITY requires a unique exact match (refined during apply).** An earlier draft routed to
ENTITY whenever `resolve_node(needle)` succeeded. But `resolve_node` is lenient — it does a
case-insensitive bare-symbol match — so `"alpha"` resolves to a symbol `Alpha` even though a
substring search legitimately also matches `AlphaLeaf`. Routing `"alpha"` to a single-node entity
result would *rob a useful search* and regress existing behavior. The rule is therefore conservative:
ENTITY fires only when the needle pins down exactly one symbol (`matching_nodes` size 1) that it
equals exactly. If any other symbol also matches as a substring, the query stays in lexical search
and returns them all. This keeps routing strictly additive — worst case is the status quo. (The
STRUCTURAL operand keeps the lenient `resolve_node` because the phrase prefix is itself a strong,
unambiguous intent signal that an NL query essentially never trips.)

Ordering rationale: a structural phrase like `callers of buildApp` must be parsed *before* the
exact-entity test, or the whole phrase would fail the entity test and fall to lexical search. The
phrase grammar extracts the trailing operand `X` and tests *that* against the symbol table.

### Structural-phrase grammar (fixed, case-insensitive)

| phrase shapes | relation | direction |
|---|---|---|
| `callers of X`, `who calls X`, `what calls X` | `CALLS` | in |
| `callees of X`, `what does X call`, `what X calls` | `CALLS` | out |
| `references to X`, `who references X`, `uses of X` | `references` | in |
| `implementations of X`, `who implements X`, `subclasses of X`, `who extends X` | `inherits` | in |
| `importers of X`, `who imports X` | `imports` | in |

`X` is the remaining token(s) after stripping the phrase prefix/suffix; it is resolved with the same
`matching_nodes` (exact) the entity route uses. If `X` does not resolve, the query is NOT forced —
it falls through to lexical search (so "references to the old config format", with no symbol
`the old config format`, still searches lexically). This keeps the grammar additive and low-risk.

The relation tokens reuse the exact edge-type strings already stored on edges and already accepted
by `explain`'s `relation` filter (`CALLS`, `references`, `inherits`, `imports`) — no new vocabulary.

## Dispatch and response shape

`query_graph` adds a `route` field to every response and shapes results per route:

```jsonc
// ENTITY: "buildApp"
{ "route": "entity",
  "nodes": [ <brief of buildApp> ],
  "neighbors": { "callers": {"count": 12, "top": [<id>, ...]},
                 "callees": {"count": 7,  "top": [...]},
                 "references": {"count": 3, "top": [...]} },
  "total": 1, "returned": 1 }

// STRUCTURAL: "who calls buildApp"
{ "route": "callers",            // relation+direction collapsed to the intent name
  "of": "<buildApp id>",
  "nodes": [ <briefs of the incoming-CALLS neighbors, importance-ranked> ],
  "total": 12, "returned": 12 }

// LEXICAL: "review and land a finished run"   (unchanged behavior + tag)
{ "route": "search",
  "nodes": [ <lexical-overlap-ranked briefs> ],
  "total": N, "returned": min(N, limit),
  "suggestions": [...] }        // only when total == 0, as today
```

The `neighbors` summary on the entity route reuses the same neighbor walk `explain_node` already
performs (grouped by relation/direction, capped to a small `top` per group). The structural route is
exactly `explain`'s typed-neighbor set rendered as a `nodes` list, so the two stay consistent.

`limit`, `kind`, and `file` narrowing apply to the `nodes` list on every route, unchanged.

## Why this is in-contract

The host-skill contract: cgraph owns deterministic graph work; hosts own model/LLM logic. Intent
classification here is a string match against the symbol table plus a fixed phrase grammar — fully
deterministic, no inference, reproducible. It is the same class of work as the existing
`matching_nodes` / `lexical_matches` resolution. No API keys, providers, or model calls are added.

## Telemetry (optional, low-cost)

Mirror the existing `context_adaptive_count` pattern: count queries per route in op-stats so the
durable ledger shows how often structural routing fires in production. Useful to confirm adoption
and to see whether agents lean on the structural path once it exists.

## Measurement plan (research, parked)

The git-mined eval (`research/eval/queries.jsonl`) is all NL commit subjects, so it exercises only
the lexical route. Validating routing needs a separate probe, written under `research/`:

1. **Classification accuracy** — a hand-built set of entity / structural / NL queries; assert each
   lands in the expected route. (Deterministic; no graph dependence beyond symbol resolution.)
2. **Typed-bundle precision/cost (the synthesis nugget)** — for change-impact-style questions,
   compare the structural route's typed neighbor set vs the untyped `explain` dump: precision
   against the eval's relevant-set and token cost of the returned bundle. Hypothesis: typed routing
   raises precision and cuts tokens with no recall loss. This is a *primitive-precision* check, not
   recall tuning — ranking stays fixed.

This is parked as a follow-up; the change itself is verified by the classifier/parser unit tests.

## Risks

- **English-only phrase grammar.** Mitigated by the fall-through: an unmatched or unresolved phrase
  becomes lexical search (status quo), never an error or empty result.
- **Overloading `query`'s contract.** Accepted: `query` becomes "find or answer a structural
  question," still returning a node list; it never morphs into `context`'s budget-packed bundle.
- **Phrase false-positives** (e.g. a real symbol literally named `uses of parser`). Vanishingly rare,
  and the operand-must-resolve guard means a misparse that doesn't resolve falls back to search.
