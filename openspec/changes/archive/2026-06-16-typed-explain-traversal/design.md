# Design — typed-explain-traversal

## Test strategy (before implementation)

This is a behavior change on a query op, so every behavior is pinned by a deterministic
smoke test before the code changes (repo TDD config).

1. **Unfiltered parity (regression).** Call `explain` on a node with no `relation`. Assert
   the neighbor set is exactly what it is today — same edges, same centrality ordering,
   same count. This is the load-bearing "do no harm" test: it must pass before and after,
   and would fail if the filter ever leaked into the default path.
2. **Filtered correctness.** Call `explain` on a node that has mixed adjacent relations
   (e.g. an incoming `CALLS` and an incoming `references`/`contains`). Assert that with
   `relation: CALLS` **every** returned neighbor carries `relation == "CALLS"` and that a
   known non-`CALLS` neighbor present in the unfiltered result is now absent. Assert a
   relation with no matching edges returns an empty neighbor list (not an error).
3. **Direction + relation compose.** With `direction: in` and `relation: CALLS`, assert
   results are the intersection (incoming CALLS edges only), proving the new filter stacks
   with the existing direction gate rather than replacing it.
4. **MCP forwarding.** A `graph_context`-style forwarding assertion already exists for
   adaptive params; add the equivalent for `graph_explain`: a `tools/call` with
   `relation: CALLS` lands as `op == "explain"`, `params.relation == "CALLS"`. This pins
   the verbatim-forward contract so the param can never be silently dropped.

A fixture with at least one node reached by two different relation types is required for
test 2; the existing daemon_ops fixtures already build a multi-relation graph (CALLS +
contains + imports), so no new extraction fixture is needed — confirm during apply and only
add one if coverage is missing.

## Mechanism

Mirror `impact_radius` exactly. In `explain_node`:

- Read `const auto relation = params.value("relation", std::string{});`
  (same spelling/idiom as `daemon_ops.cpp:443`).
- In the neighbor loop, after the existing direction gate (`daemon_ops.cpp:852`), add:
  `if (!relation.empty() && edge.relation != relation) { continue; }`
  — identical predicate to `impact_radius` (`daemon_ops.cpp:467`).

That is the entire engine change: one read + one guard, placed so the relation filter is
applied **before** centrality sorting and the `limit` truncation, so a typed query that
matches few edges is never crowded out by high-centrality off-relation neighbors.

## Case sensitivity (decision)

Exact, case-sensitive match — identical to `impact_radius`. Rationale: a single canonical
filter behavior across both ops avoids a second dialect, and the stored tokens are stable
and known. The tokens are intentionally mixed-case in this codebase:

- `CALLS` (uppercase) — resolved call edges (`graph_builder.cpp:17`, `kCallRelation`).
- `contains`, `defines`, `imports`, `inherits`, `references` (lowercase) — extractor edges.
- `re_exports`, `imports_from` — JS/TS module edges.

The MCP description carries the exact tokens so the agent never has to guess casing. We do
**not** add case-folding or alias mapping — that would be a new behavior diverging from
`impact_radius` and is explicitly out of scope.

## Validation

- `ctest --preset default` — full smoke suite (report pass/fail counts).
- `ctest --preset default -R daemon_ops_test` — focused explain filter coverage.
- `ctest --preset default -R mcp_server_test` — relation forwarding.
- Build identity / parity: additive default path; no `graph.json` or export change, so the
  existing parity goldens stay green without modification.

## Untestable-directly notes

None. Every behavior here is a deterministic function of the in-memory graph and request
params — no model, no wall-clock, no filesystem. All four behaviors are pinned by smoke
tests above.
