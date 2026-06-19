# Design — honest, legible op-stats

## not-ready accounting

`record` gains a `not_ready` flag (default false). The daemon computes it once per dispatch:

```cpp
const bool not_ready = graph->build_state == BuildState::Empty;   // serving the empty build snapshot
...
state.op_stats.record(*known_op, latency_ms, zero_hit, adaptive_context_call, query_route, not_ready);
```

In `record`, when `not_ready` is true:
- increment a single `not_ready` lifetime counter,
- **return early** — do NOT touch `count[op]`, `total_ms`, `latency_hist`, the zero-hit counters, or
  the recent window.

So `query_count`, `query_zero_hits`, latency percentiles, and the recent window all reflect
**ready-graph reads only**. `not_ready` is surfaced separately (it answers "how many queries hit a
still-building daemon" — the build-window traffic), without inflating the quality metric. A query
against an empty/building snapshot is a *not-ready*, not a *miss*.

## Query route distribution

The query op already returns a `route` field: `entity`, a structural intent name
(`callers`/`callees`/`references`/`implementations`/`importers`), or `search`. The dispatch reads it
from the response and buckets to one of three counters on `DaemonOpStats`:

```
query_route_entity      route == "entity"
query_route_structural  route in the structural intent names
query_route_search      route == "search"  (or absent)
```

Surfaced in `status` and persisted in the ledger line + cross-session rollup beside
`adaptive_context`. This makes routing adoption observable: e.g. "of 1000 queries, 120 entity, 90
structural, 790 search" tells you how often typed routing fires in real use.

(Only the query op carries a route; other ops pass an empty route and skip the bucketing.)

## Ledger + rollup + status

`op_stats_ledger_line` (written on shutdown) and `aggregate_op_stats_ledger` (the `cgraph stats`
rollup) gain `not_ready` and the three route counts — same pattern as the existing
`adaptive_context` field, so cross-lifetime sums work unchanged. `status` adds the same fields to its
`ops` block.

## `cgraph stats` default window

`run_stats` defaults `since` to `"today"`, which surfaced `0` for the frontend (its activity was on
prior days). Change the default to **all-time** (no lower bound) so `cgraph stats` shows the full
durable history; `--since today|<ISO8601>|<N>h|<N>d` still narrows. Pure default change; the parser
is untouched.

## Why this is the right altitude

Telemetry-only: no retrieval, routing, or gather behavior changes. It fixes the *metric* that the
dogfooding showed lies (build-window zero-hits) and adds the *coverage* it lacks (route
distribution), so retrieval quality becomes trustworthy and legible over time rather than
hand-probed. Auto-waiting-for-ready in the client (so fewer not-ready queries happen at all) is a
separate operational change, explicitly out of scope — the daemon already emits `graph_state:building`;
honoring it is the caller's job and a later thread.

## Risks

- **Under-counting if `build_state` semantics shift** — `not_ready` keys on `BuildState::Empty`,
  which is exactly what `annotate_build_state` already treats as "building"; they stay in lockstep.
- **Route bucket drift** — if new structural intent names are added later, the `structural` bucket
  must include them; mitigated by bucketing on "not entity and not search" rather than an explicit
  allowlist.
