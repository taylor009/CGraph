## ADDED Requirements

### Requirement: Never-idle daemon mode
A daemon started with an idle timeout of zero or less SHALL NOT idle-shut-down: it SHALL remain
resident and continue watching the project tree until it receives an explicit `shutdown` op or a
termination signal. A daemon started with a positive idle timeout SHALL retain the existing
behavior — it shuts down after that much time with no request and no code edit. The configured
idle timeout SHALL be settable via the daemon's `--idle-timeout SECONDS` argument.

#### Scenario: Zero idle timeout keeps the daemon resident
- **WHEN** a daemon is started with `--idle-timeout 0` and then receives no requests and observes no
  code edits for longer than the default idle window
- **THEN** the daemon is still listening and answers a `status` op

#### Scenario: Positive idle timeout still shuts down
- **WHEN** a daemon is started with a positive `--idle-timeout` and then receives no requests and
  observes no code edits for longer than that timeout
- **THEN** the daemon leaves its serve loop and shuts down (and the existing op-stats ledger flush
  on shutdown still occurs)

#### Scenario: Explicit shutdown always works
- **WHEN** a never-idle daemon receives a `shutdown` op
- **THEN** it leaves its serve loop and terminates regardless of the idle-timeout setting

### Requirement: Single-owner endpoint bind
Daemon startup SHALL guarantee at most one live daemon per canonical project root. Before claiming
the endpoint, startup SHALL probe the existing socket: if a live daemon answers, the starting
instance SHALL NOT unlink or rebind the endpoint — it SHALL report that the root is already served
and exit without error. Only when the probe finds no live listener (a stale socket left by a crashed
daemon, or no socket at all) SHALL startup unlink any stale endpoint and bind its own.

#### Scenario: Second start defers to the live daemon
- **WHEN** a daemon is already resident and serving a root, and a second daemon is started for the
  same root
- **THEN** the second instance reports the root is already served and exits without error, the
  original daemon still owns the socket, and a `query` issued to that socket is answered by the
  original daemon

#### Scenario: Stale socket is reclaimed
- **WHEN** a daemon is started for a root whose socket file exists but no daemon is listening on it
  (a crashed predecessor)
- **THEN** startup unlinks the stale socket, binds its own, and serves normally
