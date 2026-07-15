## 1. Wire boundary

- [x] 1.1 Add the shared `kMaxFrameBodyBytes` cap to `protocol.hpp` and enforce it in `encode_frame`, replacing the uint32-max send guard.
- [x] 1.2 Validate the declared body length against the cap in the daemon's `read_frame` before allocating, rejecting oversized frames with no body read.
- [x] 1.3 Apply `SO_RCVTIMEO`/`SO_SNDTIMEO` to every accepted connection in both serve loops so a stalled peer is dropped and the loop continues.
- [x] 1.4 Cover both in tests: oversized-length frame rejected without allocation; stalled half-frame connection dropped while the daemon still answers a follow-up request.

## 2. Status race

- [x] 2.1 Add `enrichment_mutex` to `DaemonState` guarding the enrichment counters and the `unextracted` map.
- [x] 2.2 Take it in every writer: enrichment refresh publish, drop-ingest failure count, fast-load coverage rebuild, `note_unextracted_change`, `full_stat_index_rescan`, and `EnrichmentRunningScope`.
- [x] 2.3 Snapshot the counters and map under the lock in the `status` op and build the payload from the copies.
- [x] 2.4 Cover with a status-during-concurrent-update consistency test (no flaky stress loop).

## 3. Durable persistence

- [x] 3.1 Make `persist_graph_snapshot` atomic-rename-only: on failure leave the existing `graph.json` untouched, remove the orphan temp, and return the failure.
- [x] 3.2 Cover with a test proving a failed rename leaves the prior `graph.json` intact.

## 4. Fragment referential integrity

- [x] 4.1 Export `node_key` from `graph_builder` (linkage only, behavior unchanged) so ingest keys fragment nodes exactly as merge does.
- [x] 4.2 Reject a fragment atomically in `ingest_semantic_fragment` when any edge endpoint is in neither the fragment nor the current graph snapshot, counting it as a failed fragment.
- [x] 4.3 Cover with tests: dangling-endpoint fragment rejected with graph unchanged; fragment edges into existing graph nodes still merge.

## 5. Verification

- [x] 5.1 Full default CTest suite passes (64/64 on 2026-07-14, macOS).
- [x] 5.2 End-to-end against a live daemon: oversized frame and stalled half-frame survived, normal status answered afterward.
