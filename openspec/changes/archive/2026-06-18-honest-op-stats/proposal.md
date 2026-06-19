## Why

Dogfooding the live turing frontend/backend exposed that the op-stats ledger — the one signal for
"is retrieval working in production" — is untrustworthy in exactly the situations it's consulted:

- **Build-window queries are counted as zero-hits.** `handle_daemon_request` records every read with
  `op_stats.record(op, latency, zero_hit, …)` (daemon_ops.cpp:1682) where `zero_hit` is just
  `total==0` / `focus null`, with **no `build_state` check**. A query served while the daemon is
  still `building` (empty snapshot) records as a real miss. On the frontend this read **53.8%
  zero-hit when the true ready-state rate was ~0%** — 7 false misses from queries fired during the
  rebuild. And since every binary reinstall bumps the logic version → the first query triggers a full
  rebuild, this window is hit constantly during development.
- **Routing is invisible in the ledger.** `DaemonOpStats` tracks `adaptive_context` but no query
  **route** counters, so the ledger cannot answer "how often did structural routing fire?" or "what
  fraction of NL queries resolved?" — the questions for judging the routing feature in production.
- **`cgraph stats` defaults to `--since today`**, so checking accumulated history shows `0` unless
  activity happened today (it did show 0 for the frontend until `--since 365d`).

The retrieval quality is good; this change makes its observability *honest and legible* so it can be
trusted and tracked over time instead of probed by hand.

## What Changes

- **Exclude not-ready reads from quality accounting.** `record` gains a `not_ready` flag; the daemon
  passes `graph.build_state == BuildState::Empty`. Not-ready ops increment a separate `not_ready`
  counter and are **excluded** from per-op counts, zero-hit counters, latency, and the recent window
  — so `query_zero_hit_rate` reflects only reads against a ready graph. `not_ready` is surfaced
  (status + ledger) so build-window traffic is visible without polluting the quality metric.
- **Record query route distribution.** Add `entity` / `structural` / `search` route counters
  (bucketing the query op's `route` field). Surface them in `status` and the durable ledger + rollup,
  so routing adoption is observable like `adaptive_context` already is.
- **Default `cgraph stats` to all-time** (no `--since` filter) instead of `today`; `--since` still
  narrows. Checking stats shows the full durable history by default.

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: op-stats exclude not-ready (building-snapshot) reads from zero-hit and op
  accounting and surface them separately; op-stats record the query route distribution; the `stats`
  command reports all-time by default.

## Non-Goals

- **Changing retrieval behavior** — this is accounting/telemetry only; query/routing/gather logic is
  untouched.
- **Per-structural-route breakdown** (callers vs callees vs references) — bucketed to `structural`
  for now; finer breakdown is a later refinement.
- **A metrics dashboard / export format** — the ledger + `cgraph stats` rollup stay the surface.
- **Auto-waiting for ready in the client/MCP** — a related but separate operational change (the
  daemon already signals `graph_state:building`); this change only stops the *metric* from lying.

## Impact

- `src/engine/include/cgraph/operation_stats.hpp` + `operation_stats.cpp` (`not_ready` + route
  counters on `DaemonOpStats`, `record` signature, ledger line + cross-session rollup),
  `src/engine/daemon_ops.cpp` (pass `not_ready` and the query route to `record`; surface in
  `status`), `src/cli/main.cpp` (`stats` default window), `tests/smoke/operation_stats_test.cpp`
  (+ `daemon_ops_test.cpp`).
- **No parity surface, no retrieval-path change.**
- Verified by: a unit test that a building-state query does NOT raise `query_zero_hits` (and bumps
  `not_ready`), that route counters tally entity/structural/search; a live check on the turing
  frontend that the ready-state zero-hit rate reads ~0% (not inflated by the rebuild window) and the
  route counts are populated; full suite.
