## Why

The semantic-enrichment drainer LaunchAgent (`com.cgraph.enrich-drain`) is installed by
`cgraph drain install` (`src/engine/enrich_drainer.cpp::drainer_install`, bootstrap at :102), but
**nothing reconciles it afterward**. The supervisor's reconcile pass
(`src/engine/daemon_supervisor.cpp::supervisor_sync`) manages only the per-repo graphd LaunchAgents
and the supervisor itself — the drainer is outside its loop entirely.

Verified on this machine today: the plist existed at
`~/Library/LaunchAgents/com.cgraph.enrich-drain.plist`, yet
`launchctl print gui/501/com.cgraph.enrich-drain` returned **"Could not find service"** — the
drainer had silently fallen out of the launchd domain (a login without a reload, or a stray
`bootout`). With no reconciler to notice, enrichment was frozen with a **210-chunk backlog** on one
repo and no signal that anything was wrong.

Second problem: even when loaded, the cadence is too slow for a real backlog. The drainer default
was `StartInterval 86400` (daily) with a 10-chunk-per-repo-per-run cap
(`src/engine/include/cgraph/enrich_drainer.hpp`). A 210-chunk backlog at 10 chunks/day takes
**~3 weeks** to drain.

## What Changes

- **Residency reconcile.** The supervisor reconcile pass now checks the enrichment drainer: if its
  plist exists in `launch_agents_dir` but its service is not loaded in the gui domain, it
  re-bootstraps it. Installation stays explicit (`cgraph drain install`) — reconcile only restores
  residency of an *already-installed* drainer; it never installs one. The decision is a pure,
  directly-testable function (`decide_drainer_residency(plist_exists, service_loaded)`), with the
  launchctl probe behind a new `launchctl_is_loaded(label)` helper in `launch_agent.cpp`.
- **Drain cadence.** The drainer `StartInterval` default drops from `86400` (daily) to `14400`
  (every 4 hours), keeping the 10-chunk cap. A 210-chunk backlog now drains in a few days, not weeks.
  No new CLI flags — the existing `--interval` override is unchanged.

## Capabilities

### Modified Capabilities

- `resident-daemon-supervisor`: the reconcile pass gains a requirement to restore residency of an
  installed enrichment drainer whose service has fallen out of the launchd domain, and the drainer
  schedule requirement now defaults to a cadence that drains a few-hundred-chunk backlog within days.

## Non-Goals

- **Installing the drainer from reconcile.** Reconcile restores residency only when the plist is
  already present; a machine that never ran `cgraph drain install` gets no drainer. Installation
  stays an explicit, opt-in step.
- **Per-run chunk-cap changes.** The 10-chunk cap is unchanged; only the interval moves. Draining a
  backlog faster is achieved by more-frequent runs, not larger runs.
- **Running launchctl in tests.** The decision logic is pure and unit-tested; the launchctl seam is
  validated end-to-end manually on macOS (CI runs Linux + macOS, where a loaded service cannot be
  assumed).

## Impact

- `src/engine/daemon_supervisor.cpp` + `include/cgraph/daemon_supervisor.hpp`
  (`decide_drainer_residency`, drainer branch in `supervisor_sync`, `drainer_rebootstrapped` result).
- `src/engine/launch_agent.cpp` + `include/cgraph/launch_agent.hpp` (`launchctl_is_loaded`).
- `src/engine/include/cgraph/enrich_drainer.hpp` (`interval_seconds` default 86400 -> 14400).
- `src/cli/main.cpp` (`sync` prints when it restored drainer residency).
- `tests/smoke/daemon_supervisor_test.cpp` (residency-decision truth table),
  `tests/smoke/enrich_drainer_test.cpp` (cadence assertion 86400 -> 14400).
- Verified by: pure residency-decision unit tests, the cadence assertion, full suite, and an
  end-to-end run on macOS — `launchctl bootout` the drainer, confirm `launchctl print` reports
  "Could not find service", run the supervisor sync path, confirm the service is loaded again.
