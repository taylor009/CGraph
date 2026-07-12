## Why

After a build or an `update .`, the daemon synchronously ran `refresh_enrichment_state`, which
calls `plan_semantic_chunks` — a recursive walk of the entire project that sha256-hashes every
doc/media file. On a repo with 7,008 enrichable files this is ~30s, and the `update .` op blocked
on it before responding: a rescan that re-extracted only 3-4 changed files still took ~50s end to
end. The enrichment counts are purely informational status (pending/stale, for the host's
enrichment loop) and have no reason to sit on the graph-response critical path.

## What Changes

- Move enrichment planning onto a dedicated, coalescing worker thread. Build/update/drop paths
  call a cheap `request_refresh()` (sets a flag, notifies) and return immediately. The worker
  snapshots the semantic cache under `graph_mutex` (a quick copy), runs the slow walk/hash
  OFF-lock so it never blocks builds or queries, then writes the counts back under `graph_mutex`.
- Multiple requests in a burst coalesce into one re-plan. The worker is signalled and joined at
  shutdown alongside the build thread.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `graph-daemon-client`: enrichment status is refreshed asynchronously; a build or update returns
  without waiting for the whole-project enrichment scan, and the status counts converge shortly
  after.

## Impact

- `src/engine/daemon_server.cpp` only (worker thread + condition variable + `request_refresh`;
  `run_enrichment_refresh` snapshots the cache and plans off-lock).
- Measured on a 39,114-node / 7,008-enrichable-file repo: `update .` (re-extracting 3-4 changed
  files) dropped from ~50.7s to ~15s. Enrichment status still populates (`pending=7008`), now
  asynchronously. Full suite 55/55, including the daemon enrichment-state tests.
- Behavior change: immediately after a build/update, `status` may briefly report the previous
  enrichment counts until the async re-plan completes. This is acceptable — the counts are
  informational and the host's enrichment loop polls.
- Not addressed: the remaining ~15s of `update .` is full-graph re-merge + dedup + persist of the
  whole graph (a full rescan by design), independent of enrichment.
