## Why

Four verified latent defects sit on the daemon's externally reachable trust boundary and its
durability path. None crash today's happy path, which is why 64/64 tests pass — but each is a
real bug in normal or adversarial operation:

- **Unvalidated wire length (DoS):** `daemon_server.cpp` allocated a frame buffer directly from
  the `uint32_t` length read off the socket. A single hostile or corrupt 4-byte header
  (`0xFFFFFFFF`) forces a ~4 GB allocation before any body is read. `protocol.cpp` capped only
  the send side; there was no receive cap.
- **No socket timeouts (hang):** the daemon serves each connection inline on its single
  serve-loop thread with no `SO_RCVTIMEO`/`SO_SNDTIMEO`. A client stalling mid-frame froze
  queries, the watcher poll, persistence, and idle shutdown indefinitely.
- **`status` data race (UB):** the Status op read the enrichment counters and iterated the
  `unextracted` map while the enrichment-refresh, drop-ingest, and rescan paths wrote them under
  a different lock. The enrichment drainer is status-gated and polls constantly, so this raced
  in routine operation.
- **Destructive persist fallback (data loss):** on rename failure, `persist_graph_snapshot`
  deleted the last-known-good `graph.json` and retried; a double failure left the daemon with no
  graph at all.

A fifth gap breaks the host contract: fragment validation was shape-only, so a schema-valid
fragment carrying edges whose endpoints exist in neither the fragment nor the graph merged
dangling edges silently — while `docs/host-skill-contract.md` promises "malformed → rejected,
graph unchanged."

## What Changes

- A shared `kMaxFrameBodyBytes` cap (64 MiB) in `protocol.hpp`, enforced by both `encode_frame`
  and the daemon's `read_frame` — an oversized declared length is rejected before allocation.
- `SO_RCVTIMEO` + `SO_SNDTIMEO` (5 s) applied to every accepted connection in both serve loops;
  a stalled peer is dropped and the loop continues.
- A dedicated `enrichment_mutex` in `DaemonState` guards the enrichment counters and the
  `unextracted` map; every writer (enrichment refresh, drop ingest, rescan/incremental update,
  `EnrichmentRunningScope`) and the `status` reader take it. Status snapshots the values under
  the lock and builds its payload from the copies.
- `persist_graph_snapshot` performs an atomic rename only. On failure it leaves the existing
  `graph.json` untouched, removes the orphan temp file, and returns the failure.
- Semantic ingest rejects a fragment atomically if any edge endpoint resolves against neither
  the fragment's own nodes (keyed exactly as merge keys them, via the now-exported `node_key`)
  nor the current graph snapshot. The rejection is counted as a failed fragment like other
  validation failures. `merge_fragment`/`merge_fragments` semantics are unchanged (Graphify
  parity is a hard contract).

## Capabilities

### Modified Capabilities

- `graph-daemon-client`: the wire boundary validates declared frame lengths against a shared
  cap, connections carry I/O timeouts, `status` reads enrichment state race-free, and graph
  persistence never destroys the last-known-good snapshot.
- `semantic-fragment-ingest`: fragment rejection extends beyond shape validation to referential
  integrity — a dangling edge endpoint rejects the whole fragment with the graph unchanged.
