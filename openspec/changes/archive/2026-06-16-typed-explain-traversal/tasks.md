# Tasks — typed-explain-traversal

## 1. Engine: relation filter on explain
- [x] 1.1 Add a failing daemon_ops test: `explain` with `relation: CALLS` returns only
  `CALLS` neighbors, and a known non-`CALLS` neighbor present unfiltered is absent; a
  no-match relation yields an empty neighbor list (not an error). Confirm/extend the
  fixture so a node is reached by ≥2 distinct relation types.
- [x] 1.2 Add a failing regression test: `explain` with no `relation` returns the exact
  pre-change neighbor set and ordering (parity guard).
- [x] 1.3 Add a failing test: `direction: in` + `relation: CALLS` returns the intersection
  (incoming CALLS only), proving the filters compose.
- [x] 1.4 Implement: read `relation` param and add the
  `if (!relation.empty() && edge.relation != relation) continue;` guard after the existing
  direction gate in `explain_node`, before sort/limit. Mirror `impact_radius` exactly.
- [x] 1.5 Run `ctest --preset default -R daemon_ops_test`; all green.

## 2. MCP: document typed traversal, pin forwarding
- [x] 2.1 Add a failing mcp_server test: `tools/call graph_explain` with `relation: CALLS`
  forwards to `op == "explain"` with `params.relation == "CALLS"`.
- [x] 2.2 Add a failing tools/list assertion: the `graph_explain` description names the
  `relation` parameter and the find-callers/find-callees/find-references usage patterns.
- [x] 2.3 Implement: add `relation` to the `graph_explain` tool schema and rewrite its
  description with the five named patterns (callers/callees/references/imports/inheritance)
  and the exact relation tokens. Forwarding stays verbatim (no `daemon_request_for_tool`
  change needed) — the forwarding test confirms it.
- [x] 2.4 Run `ctest --preset default -R mcp_server_test`; all green.

## 3. Verify end-to-end
- [x] 3.1 Run the full suite `ctest --preset default`; record pass/fail counts.
- [x] 3.2 Live check through the daemon: `cgraph-client ... explain '{"id":"<call target>","relation":"CALLS"}'`
  returns only CALLS neighbors; same id with no relation returns the full neighbor set.
  Record both outputs as evidence (real-flow verification, not unit-only).
