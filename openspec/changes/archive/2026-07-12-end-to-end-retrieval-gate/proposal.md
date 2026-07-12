# End-to-End Retrieval Gate

## Why

The existing retrieval-quality gate (`tests/smoke/pack_context_parity_test.cpp`) injects each
eval row's grade-2 focal id directly into the `context` op (`{"id", row.focal}`), so it validates
gathering and packing but never exercises focal resolution or query routing. That blind spot is
proven, not hypothetical: the route-2 entity-routing bug (every exact-symbol query on a real graph
silently downgraded to lexical search, fixed in `3efa8e0`) lived on the exact path the gate skips
and was only found by manual dogfooding. Separately, two agent-facing contracts lie today:
`graph_context`'s MCP schema advertises `fixed` as the default gather while the engine defaults to
`adaptive`, and `graph_recall` cannot match the checkpoint body that `graph_remember` tells agents
to put their substance in.

## What Changes

- **New end-to-end retrieval gate**: a new CTest smoke test drives the full production path —
  free-text `row.query` into the `context` op via `q` (no id injection), exercising
  `matching_nodes` + lexical multi-seed focal resolution + adaptive gather + knapsack packing —
  on the existing committed fixture (`tests/fixtures/pack_context_parity/`), computes mean
  grade-2 recall@budget, and fails when recall drops below a measured, committed baseline minus
  tolerance. The gate must be proven to bite (a deliberate local regression goes red) before the
  baseline is pinned.
- **`recall` matches checkpoint bodies**: the daemon `recall` op's query filter widens from
  title/tags to title/tags/body, so checkpoints are findable by their content.
- **`graph_context` schema tells the truth**: the MCP tool description and schema state that
  `adaptive` is the default gather mode (matching the engine default); the stale engine comment
  is corrected. The engine default itself does not change.
- **Eval tooling promoted**: `scripts/bootstrap_eval.py` and `.research-eval.toml` move from
  untracked research artifacts to tracked, supported tooling — the documented way to regenerate
  or extend the eval fixture.

## Capabilities

### New Capabilities

<!-- none -->

### Modified Capabilities

- `graph-daemon-client`: new requirement — end-to-end retrieval quality (free-text query to
  packed context) is gated by a committed fixture baseline that runs with the smoke suite.
- `graph-session-memory`: the recall filter requirement changes — `query` matches checkpoint
  title, tags, and body (today: title and tags only).
- `host-integration-mcp`: the MCP surface requirement changes — tool descriptions and schemas
  must state the engine's actual defaults (`graph_context.gather` default is `adaptive`).

## Impact

- `tests/smoke/retrieval_quality_test.cpp` (new) + `tests/smoke/CMakeLists.txt` registration;
  reuses `tests/fixtures/pack_context_parity/{graph.json,queries.jsonl}` — no new fixture.
- `src/engine/daemon_ops.cpp`: recall query filter (~1531-1537) widened to body; stale gather
  comment (~808) corrected. No behavior change to context/query routing.
- `src/mcp/mcp_server.cpp`: `graph_context` description/schema text (~92, 102-104).
- `tests/smoke/daemon_ops_test.cpp`: recall-by-body regression case.
- `scripts/bootstrap_eval.py`, `.research-eval.toml`: added to the repo (tracked).
- No change to `graph.json` output, fragment schema, ID normalization, or any Graphify parity
  surface. `research/` stays untracked per the research/eval discipline.
