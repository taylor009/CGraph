## 1. launchctl loaded-state probe

- [x] 1.1 Add `launchctl_is_loaded(label)` to `launch_agent.cpp` + `.hpp`: probe via
      `launchctl print gui/<uid>/<label>` exit status (0 = loaded, non-zero = "Could not find
      service"), same `run_command`/`gui_domain` style as `launchctl_bootstrap`/`bootout`
      (`launch_agent.cpp:146-159`). Returns false on non-macOS.

## 2. Residency reconcile decision + wiring

- [x] 2.1 Add the pure `decide_drainer_residency(plist_exists, service_loaded)` function
      (`daemon_supervisor.cpp` + `.hpp`) returning `kRebootstrap` only when the plist exists and the
      service is NOT loaded, else `kNone`. No launchctl side effects.
- [x] 2.2 Wire it into `supervisor_sync` (apply path): after the per-repo reconcile, probe the
      drainer plist (`is_regular_file`) and `launchctl_is_loaded(kDrainerLabel)`; on `kRebootstrap`,
      `launchctl_bootstrap` the plist and set `SupervisorSyncResult::drainer_rebootstrapped` (or push
      the label to `failed` on bootstrap failure). Installation stays explicit — reconcile never
      writes the plist.
- [x] 2.3 `cgraph daemon sync` prints "restored enrichment drainer residency" when
      `drainer_rebootstrapped` is set (`src/cli/main.cpp`).

## 3. Drain cadence

- [x] 3.1 Change `DrainerConfig::interval_seconds` default from `86400` to `14400`
      (`src/engine/include/cgraph/enrich_drainer.hpp`), keeping the 10-chunk cap. No new CLI flag —
      the existing `--interval` override in `run_drain_command` is unchanged.

## 4. Tests

- [x] 4.1 `daemon_supervisor_test.cpp`: assert the full `decide_drainer_residency` truth table
      (present+unloaded -> kRebootstrap; present+loaded -> kNone; absent -> kNone both ways).
- [x] 4.2 `enrich_drainer_test.cpp`: assert `drainer_spec` default `start_interval == 14400`
      (updated from 86400), and the plist still renders `StartInterval`.

## 5. Verify end-to-end

- [x] 5.1 Full suite `ctest --preset default` passes (report count).
- [x] 5.2 On macOS: `launchctl bootout gui/501/com.cgraph.enrich-drain`, confirm
      `launchctl print gui/501/com.cgraph.enrich-drain` reports "Could not find service", run the
      supervisor sync code path, confirm `launchctl print` now finds the service (residency
      restored). Quote the actual command outputs. Leave the service loaded.
