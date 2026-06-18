## ADDED Requirements

### Requirement: Query op routes by deterministic intent
The `query` op SHALL classify each query into one of three routes using only deterministic signals —
an exact match against the snapshot's symbol table, a fixed case-insensitive grammar of
structural-intent phrases, and the lexical fallback — with no model or inference. Classification
SHALL be evaluated in priority order: (1) a structural-intent phrase whose operand resolves to a
symbol, (2) an exact-entity match, (3) lexical search. An unrecognized or unresolved query SHALL
fall through to lexical search, so the op's behavior is never worse than the prior name search.

The structural-phrase grammar SHALL recognize, at minimum: `callers of X` / `who calls X` /
`what calls X` (relation `CALLS`, incoming); `callees of X` / `what does X call` (relation `CALLS`,
outgoing); `references to X` / `uses of X` (relation `references`, incoming); `implementations of X`
/ `who implements X` / `subclasses of X` (relation `inherits`, incoming); `importers of X` /
`who imports X` (relation `imports`, incoming). The operand `X` SHALL be resolved with the same exact
matcher the entity route uses; if it does not resolve, the query falls through to lexical search.
Relation tokens SHALL reuse the edge-type strings already stored on edges and accepted by the
`explain` relation filter — no new vocabulary is introduced.

#### Scenario: Structural-intent phrase returns typed neighbors
- **WHEN** the query is `who calls buildApp` and `buildApp` resolves to a symbol
- **THEN** the response is routed to incoming `CALLS` traversal and returns the caller symbols
  (the same set `explain` would yield filtered to that relation and direction), not a flat name
  search over the words `who`, `calls`, `buildApp`

#### Scenario: Unique exact symbol returns the entity with a typed-neighbor summary
- **WHEN** the query is whitespace-free and pins down exactly one symbol — `matching_nodes` returns a
  single node that the query equals by id or case-insensitive label / bare symbol name
- **THEN** the response returns that node together with a compact typed-neighbor summary (callers,
  callees, references — counts and a capped set of top ids), answering "the symbol and who uses it"
  in a single call

#### Scenario: A name that also matches other symbols stays a search
- **WHEN** the query exactly matches one symbol's name but is also a substring of other symbols
  (e.g. `alpha` matches both `Alpha` and `AlphaLeaf`)
- **THEN** the query is NOT routed to the single-symbol entity result; it stays in lexical search and
  returns all matches, so routing never narrows a result the prior search would have returned

#### Scenario: Natural-language query keeps lexical search
- **WHEN** the query is a multi-word natural-language phrase that is neither an exact symbol nor a
  recognized structural phrase (e.g. `review and land a finished run`)
- **THEN** the response is the existing importance / lexical-overlap ranked symbol list, unchanged

#### Scenario: Structural phrase with an unresolvable operand falls through
- **WHEN** the query matches a structural phrase shape but its operand does not resolve to any symbol
  (e.g. `references to the old config format`)
- **THEN** the query is not forced into a typed traversal; it falls through to lexical search and
  returns ranked matches (or did-you-mean suggestions when none), exactly as today

### Requirement: Query responses self-describe their route
Every `query` response SHALL carry a `route` field naming the path taken (`entity`, the structural
intent name such as `callers`/`callees`/`references`/`implementations`/`importers`, or `search`), so
an agent can tell a precise structural answer from a fuzzy search and the op-stats ledger can record
the route distribution. The structural route SHALL also identify the resolved operand it traversed
from.

#### Scenario: Route is reported on every path
- **WHEN** any `query` call returns
- **THEN** the response includes a `route` field identifying which of entity / structural-intent /
  search produced the result, and a structural route additionally reports the operand symbol it
  traversed from

### Requirement: graph_query advertises intent routing
The `graph_query` MCP tool description SHALL state that it answers structural questions directly —
callers, callees, references, implementations, and importers of a symbol — in addition to name
search, and that the response reports the `route` taken, so a host agent issues the structural
question to `graph_query` instead of manually orchestrating `graph_query` then `graph_explain`.

#### Scenario: Tool description guides direct structural use
- **WHEN** a host inspects the `graph_query` tool schema
- **THEN** the description advertises direct structural answers (callers/callees/references/
  implementations/importers) and the `route` field, not only case-insensitive name search
