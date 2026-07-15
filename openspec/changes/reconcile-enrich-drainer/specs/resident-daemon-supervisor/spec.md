## ADDED Requirements

### Requirement: Reconcile restores enrichment-drainer residency
The supervisor reconcile pass SHALL restore residency of an installed enrichment drainer whose
launchd service has fallen out of the domain: when the drainer plist is present in the launch-agents
directory but its service is not loaded in the launchd domain, the reconcile pass SHALL re-bootstrap
it. Reconcile SHALL NOT install a drainer that was never installed — installation remains explicit
(`cgraph drain install`), and reconcile takes no action when the drainer plist is absent, nor when
the service is already loaded. The residency decision SHALL be a pure function of two inputs — whether
the plist exists and whether the service is loaded — so it is testable without invoking launchctl;
the launchctl probe and re-bootstrap remain behind the launch-agent helpers.

#### Scenario: Installed but unloaded drainer is re-bootstrapped
- **WHEN** the drainer plist exists in the launch-agents directory but its service is not loaded in
  the launchd domain (e.g. a login without a reload, or a stray `bootout`), and reconciliation runs
- **THEN** the reconcile pass re-bootstraps the drainer so its service is loaded again, and reports
  that it restored the drainer's residency

#### Scenario: Loaded drainer is left untouched
- **WHEN** the drainer plist exists and its service is already loaded, and reconciliation runs
- **THEN** the reconcile pass takes no drainer action (it does not bootout, rewrite, or re-bootstrap)

#### Scenario: Reconcile never installs an absent drainer
- **WHEN** no drainer plist exists in the launch-agents directory (the drainer was never installed)
  and reconciliation runs
- **THEN** the reconcile pass writes no drainer plist and starts no drainer service — installation
  stays explicit

### Requirement: Drainer schedule drains a backlog within days
The enrichment drainer schedule SHALL default to a cadence that drains a few-hundred-chunk backlog
within days rather than weeks. With a per-run cap of 10 chunks per repo, the default relaunch
interval SHALL be 4 hours (`StartInterval` 14400 seconds), so a few-hundred-chunk backlog clears in a
few days. An explicit override MAY set a different interval, but the default SHALL NOT be the former
daily (86400-second) cadence, which took roughly three weeks to clear a 210-chunk backlog.

#### Scenario: Default drainer cadence is four-hourly
- **WHEN** the drainer LaunchAgent spec is built with default configuration
- **THEN** its `StartInterval` is 14400 seconds (4 hours), and its per-run chunk cap remains 10

#### Scenario: A few-hundred-chunk backlog clears within days
- **WHEN** a repo accumulates a few-hundred-chunk enrichment backlog and the default-cadence drainer
  runs against it
- **THEN** at 10 chunks per repo every 4 hours the backlog drains within days, not the ~3 weeks the
  former daily cadence required
