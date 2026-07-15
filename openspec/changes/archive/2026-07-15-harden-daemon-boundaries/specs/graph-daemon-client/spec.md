## ADDED Requirements

### Requirement: Declared frame lengths are validated before allocation
The daemon SHALL validate the declared body length of every received frame against the shared
`kMaxFrameBodyBytes` cap before allocating a buffer, and SHALL reject an oversized frame as a
protocol error without reading its body. The send path SHALL enforce the same cap so both sides
share one ceiling.

#### Scenario: Hostile length header
- **WHEN** a client sends a 4-byte length header declaring a body larger than the cap (e.g.
  `0xFFFFFFFF`)
- **THEN** the daemon rejects the frame without allocating the declared size, drops the
  connection, and continues serving subsequent connections normally

#### Scenario: Oversized payload is never framed
- **WHEN** `encode_frame` is asked to frame a body larger than the shared cap
- **THEN** it returns an empty frame instead of emitting bytes the peer would reject

### Requirement: Connections carry I/O timeouts
The daemon SHALL apply receive and send timeouts to every accepted connection so a peer that
stalls mid-frame cannot block the single serve-loop thread indefinitely; a timed-out connection
is dropped and the serve loop continues.

#### Scenario: Client stalls mid-frame
- **WHEN** a client connects, sends a partial frame, and then sends nothing further
- **THEN** the daemon drops that connection after the timeout and still answers a subsequent
  status request

### Requirement: Status reads enrichment state race-free
The daemon SHALL synchronize every read and write of the enrichment counters
(`enrichment_state`/`pending`/`running`/`stale`/`failed`/`plans_run`) and the `unextracted` map
through one dedicated lock, so a `status` request concurrent with enrichment refresh, drop
ingest, or a rescan never observes a torn counter or iterates the map mid-mutation.

#### Scenario: Status polled during an update
- **WHEN** a `status` request arrives while an incremental update is rewriting the unextracted
  coverage map
- **THEN** the response contains a consistent snapshot of the counters and map

### Requirement: Persistence never destroys the last-known-good graph
The daemon SHALL persist the graph snapshot by writing a temp file and atomically renaming it
over `graph.json`; on rename failure it SHALL leave the existing `graph.json` untouched, remove
the temp file, and surface the failure. It SHALL NOT delete the existing file to retry.

#### Scenario: Rename fails
- **WHEN** the atomic rename of the temp snapshot fails
- **THEN** the prior `graph.json` still exists with its previous content and the temp file is
  cleaned up
