## Context

cgraph has no operation instrumentation and three permanently-zero status fields. The goal is a
two-layer design: a measured substrate (A) and a derived ledger (B). The hard constraints are the
Graphify `graph.json` parity golden (must stay byte-identical) and the project's "every zero hides
a bug" rule (no field may ship as a permanent zero).

## Architecture

```
LAYER B (operation_stats.cpp)  pure derivation — no timers, no I/O except stats.json writer
  cache_saved_ms  ≈ files_cache_hit × mean(extract_ms)      [MODELED, labeled estimate]
  query summary   = count, total_ms→p50/mean, zero_hit_rate [REVEALED]
        ▲
        │ reads plain structs, never measures
        │
LAYER A (measured at the seams)
  run_one_shot()          ScopedTimer per phase → BuildStats on PipelineResult
  handle_daemon_request()  ScopedTimer per op   → DaemonOpStats on DaemonState
  incremental apply        files_cache_hit / files_reextracted + phase_ms
```

### Why the seams, not the leaves

`run_one_shot` already calls the six phases sequentially; `handle_daemon_request` already dispatches
the eight ops through one switch. Timing at these two boundaries plus the incremental-apply path
captures every operation without threading a `Metrics&` parameter through `python_extractor`,
`graph_builder`, `dedup`, etc. This keeps Layer A in ~3 files and respects "cross-cutting changes go
to the source" — the source here is the orchestrator, not each consumer.

### Scoped timer

A small RAII `ScopedTimer{steady_clock, &out_ms}` that writes elapsed milliseconds into a target on
destruction. Uses `std::chrono::steady_clock` (monotonic; the codebase already uses it in
`daemon_lifecycle`). `Date.now`/wall-clock is not needed and not used.

### Data shapes (illustrative)

```cpp
struct BuildStats {                  // on PipelineResult
  double extract_ms, merge_ms, resolve_ms, dedup_ms, communities_ms, analyze_ms;
  std::size_t files_extracted, files_cache_hit, nodes, edges;
};
struct DaemonOpStats {               // accumulated on DaemonState (since boot)
  std::array<std::size_t, kOpCount> op_count;
  std::array<double, kOpCount>      op_total_ms;
  std::size_t query_zero_hits;
  RollingWindow recent;             // ring buffer of last N (op, ms, hit) records
};
```

`RollingWindow` is a fixed-size ring buffer (e.g. last 256 ops) — O(1) insert, no allocation after
construction, no time-based eviction needed for the "useful now" signal.

### The dead-field fixes

| Field | Today | Fix |
|---|---|---|
| `uptime_seconds` | never set, 0.0 | `steady_clock::now() - state.started_at`, set in `status()` |
| `cache_hit_rate` | never computed, 0.0 | `files_cache_hit / files_total` from Layer A on each (re)build |
| `enrichment_running` | never set, 0 | set `EnrichmentState::Running` around the active ingest in `run_enrichment_refresh` (`daemon_server.cpp:210-238`), clear after |

### Modeled saving honesty

`cache_saved_ms` is emitted under a key that names it an estimate (e.g.
`"cache_saved_ms_estimate"`) and is only present when `files_cache_hit > 0` and at least one real
extraction timing exists to form `mean(extract_ms)`. If no extraction ran this session (full cache
hit, cold mean unknown), the estimate is omitted rather than fabricated from a stale or zero mean.

## Test strategy

- `operation_stats_test.cpp` (new, red-first): derivation math with concrete inputs — modeled saving
  equals `hits × mean`; zero hits → no estimate key; zero queries → no division by zero; rolling
  window evicts oldest past capacity; p50 from a known latency multiset.
- `pipeline_test`: after a real one-shot build, `BuildStats` phase timings are all > 0, `nodes`/
  `edges` match the snapshot, and `stats.json` exists and parses.
- `daemon_ops_test`: after issuing N queries (some zero-hit), `status` reports `op_count[query]==N`,
  a non-zero `uptime_seconds`, and a `query_zero_hit_rate` matching the issued mix.
- `incremental_*_test`: a warm rescan reports `files_cache_hit > 0` and `cache_hit_rate` in (0,1].
- **Parity guard:** existing `extractor_goldens_test` / `graph.json` golden must still pass
  byte-identical — proves the sidecar approach did not perturb the parity surface.

### Cannot be tested directly

Absolute timing *values* are machine-dependent, so tests assert ordering/positivity/relationships
(phase_ms > 0, total ≈ sum of phases within tolerance), not exact milliseconds.

## Open questions

- Rolling window capacity (256?) — tunable constant, not a contract.
- Exact `stats.json` schema field names — settle during implementation; the contract is "the
  documented counters and the modeled estimate, each labeled".
