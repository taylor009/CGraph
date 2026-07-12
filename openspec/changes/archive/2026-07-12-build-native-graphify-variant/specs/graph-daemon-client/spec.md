## ADDED Requirements

### Requirement: Per-project daemon
The system SHALL run at most one resident daemon per canonical project root, keyed by the absolute project path and isolated from other project roots.

#### Scenario: Client auto-spawns daemon
- **WHEN** a thin client command runs and no daemon is listening for the project root
- **THEN** the client starts the daemon, waits with bounded backoff, connects, and sends the request

#### Scenario: Concurrent spawn race resolves
- **WHEN** two clients attempt to start a daemon for the same project root concurrently
- **THEN** exactly one daemon binds the project socket or pipe and both clients communicate with that daemon

### Requirement: Cross-platform local IPC
The daemon SHALL support length-prefixed JSON request and response frames over Unix sockets on Linux and macOS. Windows named-pipe transport is deferred: the daemon server, endpoint security descriptor, and client auto-spawn are stubbed on Windows and fail with an explicit not-implemented error rather than a silent degradation.

#### Scenario: Command frame succeeds
- **WHEN** a client sends a valid `query`, `path`, `explain`, `update`, `status`, or `shutdown` frame
- **THEN** the daemon processes the operation and returns a valid response frame

#### Scenario: Protocol version mismatch recovers
- **WHEN** a client connects with an incompatible protocol version
- **THEN** the daemon is shut down or rejected according to the version-skew policy and the client can respawn a compatible daemon

### Requirement: Secure daemon endpoint
The daemon SHALL restrict socket or pipe access to the current user and reject cross-user access where platform support exists.

#### Scenario: Unauthorized peer is rejected
- **WHEN** a peer from another user attempts to connect on a platform with peer credential support
- **THEN** the daemon rejects the connection before processing any graph command

### Requirement: Thin client command surface
The system SHALL provide thin client commands for `query`, `path`, `explain`, `update`, `status`, and `shutdown` that do not rebuild the graph for each request in daemon mode.

#### Scenario: Query uses resident graph
- **WHEN** a user runs a thin client `query` command while the daemon has a loaded graph
- **THEN** the client sends one request and prints the daemon response without running the full pipeline locally

### Requirement: Immutable snapshot concurrency
The daemon SHALL serve reads from immutable graph snapshots and apply graph mutations through a single writer before publishing a new complete snapshot.

#### Scenario: Read during update is consistent
- **WHEN** a read request overlaps a watcher-driven update or semantic fragment merge
- **THEN** the read observes either the previous complete snapshot or the next complete snapshot, never a partially mutated graph

### Requirement: Daemon lifecycle and fallback
The daemon SHALL support idle shutdown, clean socket or pipe cleanup, disk reload on startup, and one-shot CLI fallback for environments that cannot run resident processes.

#### Scenario: Idle daemon exits
- **WHEN** the daemon has no activity for the configured idle timeout
- **THEN** it flushes authoritative outputs as needed, releases the endpoint, and exits

#### Scenario: Restricted environment uses one-shot
- **WHEN** the target environment disallows background resident processes
- **THEN** the same engine can run in one-shot mode without daemon IPC

### Requirement: Daemon status
The daemon SHALL expose status including process id, uptime, node count, edge count, build state, cache hit rate, and resident memory where available.

#### Scenario: Status reports enrichment state
- **WHEN** deterministic graph output is ready but semantic enrichment is still pending or running
- **THEN** the `status` response distinguishes deterministic readiness from enrichment progress
