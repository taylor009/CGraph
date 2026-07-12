## 1. Async enrichment worker

- [x] 1.1 Move `plan_semantic_chunks` off the build/update path onto a coalescing worker thread:
      `request_refresh()` (signal) replaces synchronous `refresh_enrichment_state()` at all three
      call sites (rescan, Tier-1 load, serve-loop drop ingestion).
- [x] 1.2 `run_enrichment_refresh` snapshots the cache under `graph_mutex`, plans off-lock, writes
      `enrichment_*` back under `graph_mutex`. Worker signalled + joined at shutdown.
- [x] 1.3 Full suite `ctest --preset default` (55/55), including the daemon malformed-fragment
      enrichment-state test which now observes the async-updated `failed` state.

## 2. Verification (recorded)

- [x] 2.1 On a 39,114-node / 7,008-enrichable-file repo: `update .` re-extracting 3-4 files went
      from ~50.7s (synchronous enrichment) to ~15s. `status` still reports `enrichment_state` and
      `pending` (=7008), populated asynchronously.
