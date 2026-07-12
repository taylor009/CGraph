## ADDED Requirements

### Requirement: Auto-discovery of tracked repos
The supervisor SHALL determine the set of tracked repos by scanning a configured list of search
roots and selecting every directory whose `.mcp.json` registers a `cgraph` MCP server (an
`mcpServers.cgraph` entry). Discovery SHALL be deterministic for a given filesystem state and SHALL
key each tracked repo by the same canonical-project-root hash the daemon uses for its endpoint
identity, so a repo's discovery key, LaunchAgent label, and socket name all agree. A configuration
file MAY extend or exclude search roots; absent configuration, the search roots default to the
parents of already-known cgraph repos.

#### Scenario: Only cgraph-registered repos are tracked
- **WHEN** discovery runs over a tree containing a directory with a `cgraph` `.mcp.json`, a directory
  with a non-cgraph `.mcp.json`, and a directory with no `.mcp.json`
- **THEN** the tracked set contains exactly the directory with the `cgraph` `.mcp.json`

#### Scenario: Discovery key matches daemon identity
- **WHEN** a repo is discovered
- **THEN** its tracking key equals the canonical-root hash the daemon derives for that same root

### Requirement: One immortal resident daemon per tracked repo
For every tracked repo the supervisor SHALL ensure exactly one resident daemon is running with idle
shutdown disabled (never-idle mode), so the daemon stays present and folds code edits into the graph
continuously without any manual refresh. The supervisor SHALL NOT start a second daemon for a repo
that already has a live one.

#### Scenario: A tracked repo gets a resident daemon
- **WHEN** the supervisor reconciles and a tracked repo has no running daemon
- **THEN** after reconciliation a never-idle daemon is running for that repo

#### Scenario: An edit is reflected without manual refresh
- **WHEN** a file in a tracked repo is edited while its resident daemon is running
- **THEN** a subsequent `query` to that daemon reflects the edit with no manual rebuild or `update`
  command having been issued

### Requirement: Reconciliation as repos appear and disappear
The supervisor SHALL reconcile the running daemon set against the discovered set: newly-discovered
repos SHALL gain a daemon, repos no longer discovered SHALL have their daemon stopped and their
managed LaunchAgent removed, and repos present in both SHALL be left running undisturbed.
Reconciliation SHALL be idempotent — reconciling twice with no change to the discovered set SHALL
start, stop, and rewrite nothing. Reconciliation SHALL run at login and on a recurring interval so a
freshly cloned or removed repo is picked up without any manual step.

#### Scenario: Newly cloned repo is picked up
- **WHEN** a new repo carrying a `cgraph` `.mcp.json` appears under a search root and reconciliation
  runs
- **THEN** the reconcile plan adds that repo and a daemon is started for it

#### Scenario: Removed repo is torn down
- **WHEN** a previously tracked repo no longer exists (or no longer registers cgraph) and
  reconciliation runs
- **THEN** the reconcile plan removes that repo and its managed daemon/LaunchAgent is stopped

#### Scenario: Reconcile is idempotent
- **WHEN** reconciliation runs twice with no change to the discovered set
- **THEN** the second run's plan adds and removes nothing

### Requirement: Self-healing and start-at-login
A managed daemon that exits unexpectedly SHALL be restarted, and the whole supervised set SHALL come
up automatically at user login without a manual command. These lifecycle guarantees SHALL be
provided through the platform service manager (macOS `launchd`: per-daemon LaunchAgents with
`KeepAlive` and `RunAtLoad`, plus a supervisor LaunchAgent that runs reconciliation at load and on a
recurring interval).

#### Scenario: Crashed daemon is restarted
- **WHEN** a managed daemon process for a tracked repo terminates unexpectedly
- **THEN** it is restarted and resumes serving that repo

#### Scenario: Supervised set starts at login
- **WHEN** the user logs in after the supervisor has been installed
- **THEN** reconciliation runs and a daemon is present for each tracked repo without a manual command

### Requirement: Supervisor management command
The `cgraph` CLI SHALL provide a `daemon` subcommand family to manage the supervisor:
`install` (register and start the supervisor and reconcile once), `sync` (reconcile the daemon set
against discovery now), `status` (report each tracked repo and whether its daemon is live), and
`uninstall` (stop and remove the supervisor and all managed daemons). `install` and `uninstall`
SHALL be reversible — `uninstall` after `install` SHALL leave no managed LaunchAgents behind. The
supervisor SHALL never pull, commit, or otherwise mutate any tracked repo's contents; it only reads
the working tree to build the graph.

#### Scenario: Install then uninstall leaves no residue
- **WHEN** `cgraph daemon install` is run and later `cgraph daemon uninstall` is run
- **THEN** no supervisor or per-repo managed LaunchAgent remains and no managed daemon is running

#### Scenario: Status reports liveness per tracked repo
- **WHEN** `cgraph daemon status` is run with some tracked repos served and others not
- **THEN** it lists each tracked repo and reports for each whether a live daemon is currently serving
  it

#### Scenario: Supervisor never mutates a repo
- **WHEN** the supervisor manages a tracked repo across many reconciliation cycles and edits
- **THEN** it performs no git operation and makes no change to the repo's tracked files
