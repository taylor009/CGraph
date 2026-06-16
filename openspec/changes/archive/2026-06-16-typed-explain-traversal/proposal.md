# Typed explain traversal

## Why

`graph_explain` is the daemon's one-symbol-in-depth primitive: given a node it returns
every adjacent edge (callers, callees, imports, references, containment) ranked by
centrality. Today it can only be narrowed by **direction** (`in`/`out`/`both`) —
`explain_node` reads `id`, `direction`, and `limit` and filters solely on direction
(`src/engine/daemon_ops.cpp:828-832`, gate at `:852`). It already *emits* each edge's
relation (`src/engine/daemon_ops.cpp:858`) but will not accept relation as an **input**
filter.

This forces coding agents to ask the wrong question. An agent that wants "who *calls*
`charge_card`?" must request all incoming neighbors and re-implement edge-type filtering
on the client side — pulling importers, container edges, and references it must then
discard. That is more tokens, noisier context, and filtering logic the agent should not
own.

The sibling op already solved this. `impact_radius` accepts an optional `relation` and
follows only matching edges (`src/engine/daemon_ops.cpp:443`, filter at `:467`), and the
`graph_impact` MCP tool documents it (`src/mcp/mcp_server.cpp:74`). `graph_explain` should
mirror that one parameter so single-hop typed traversal — the atom that change planning,
test-impact, and impact analysis all compose from — is a first-class structural primitive.

## What Changes

- Add an optional `relation` parameter to `explain_node` / the `explain` daemon op.
- When present, return only edges whose relation matches exactly (same semantics as
  `impact_radius`: exact, case-sensitive string compare against the stored relation token).
- When absent, behavior is byte-for-byte unchanged (default = today).
- Update the `graph_explain` MCP tool description to document `relation` and the named
  usage patterns agents reach for, mapped to the project's actual relation tokens:
  - **find callers** — `direction: in`, `relation: CALLS`
  - **find callees** — `direction: out`, `relation: CALLS`
  - **find references** — `relation: references`
  - **trace imports** — `relation: imports`
  - **inspect inheritance / implementation** — `relation: inherits`
- No MCP forwarding change is required: `graph_explain` already forwards arguments verbatim
  to the `explain` op (`src/mcp/mcp_server.cpp:121-123`), so `relation` passes through once
  the op honors it. The forwarding test pins that contract.

## Goals

- `graph_explain` answers a single typed structural question (callers / callees /
  references / imports / inheritance) without the agent post-filtering.
- The relation filter is identical in semantics to `impact_radius` (one canonical
  filter behavior across both ops — no second dialect).
- Zero behavior change when `relation` is omitted (parity-safe, additive).

## Non-Goals

- No new daemon op. This is one parameter on the existing `explain` op.
- No change to graph extraction or the set/casing of emitted relation tokens.
- No change to `impact_radius` behavior.
- No LLM/planning logic in the binary (host-skill contract).
- No test-impact prediction, change planning, or ownership/hotspot signals — those compose
  on top of this primitive in a later change.

## Capabilities

- **graph-daemon-client** (MODIFIED): the `explain` op gains optional relation filtering;
  the `graph_explain` MCP tool advertises typed traversal patterns.

## Impact

- Code: `src/engine/daemon_ops.cpp` (`explain_node`), `src/mcp/mcp_server.cpp`
  (`graph_explain` schema/description; forwarding already verbatim).
- Tests: `tests/smoke/daemon_ops_test.cpp` (filtered/unfiltered explain),
  `tests/smoke/mcp_server_test.cpp` (relation forwards on `graph_explain`).
- Parity: additive only; default path and `graph.json` output unchanged.
