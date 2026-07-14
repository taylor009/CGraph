# Tasks: host-enrichment-driver

- [x] 1. `cgraph skills` verb. Add `run_skills_command` beside `run_daemon_command` in
      `src/cli/main.cpp` (dispatcher at :631); install/status/uninstall symlink the two
      `integrations/skills/*` dirs into `~/.claude/skills/` and the shared `~/.agents`
      skills path, resolving the source from the executable's repo layout (reuse the
      `current_executable_path` idiom, `src/cli/main.cpp:424-460`). Files:
      `src/cli/main.cpp`, plus engine helper + 1:1 test if logic lands in `src/engine/`
      (edit `src/engine/CMakeLists.txt`, `tests/smoke/CMakeLists.txt`).
- [x] 2. Install + discovery proof. Run `cgraph skills install`; `cgraph skills status`
      shows both links resolving into this repo; a fresh Claude Code session lists
      `cgraph` and `cgraph-enrich` in its available skills — in this repo AND in
      `full-turing/backend`.
- [x] 3. Drain this repo end to end. Invoke the `cgraph-enrich` loop against
      `/Users/taylorgagne/tools/cgraph` (72 pending at time of writing). Done when
      `status` shows `enrichment_pending: 0`, `enrichment_failed: 0`, and the
      before/after `semantic` block (connectivity_rate, orphan_docs, doc_code_edges) is
      recorded in this change.
- [x] 4. Drain the production repo. Same against
      `/Users/taylorgagne/turinglabs/full-turing/backend` (128 pending, semantic layer
      empty). Done when doc/concept nodes exist there, `doc_code_edges > 0`, and a
      `graph_query` resolves a `doc:`/`concept:` node linked to a real code node.
- [x] 5. Idempotency + staleness proof. Re-run `enrich-plan` on both repos → `0 input(s)`;
      touch one doc → next plan lists exactly that input as stale; drain it.
- [x] 6. Headless drainer. LaunchAgent (label `com.cgraph.enrich-drain`) rendered/installed
      alongside the supervisor's (reuse `launch_agent.cpp` rendering + supervisor repo
      discovery): daily `StartInterval`; per repo, `status` gate → skip if
      `enrichment_pending == 0`, else headless host CLI run of the enrich loop on Sonnet
      with a per-run chunk cap. Files: `src/engine/daemon_supervisor.cpp` (or sibling
      `enrich_drainer.cpp` + 1:1 test), `src/cli/main.cpp` wiring.
- [x] 7. Drainer proof. Force one repo stale, trigger the LaunchAgent
      (`launchctl kickstart`), verify it drains and that a second firing exits at the
      status gate with no model spawn (log evidence).
- [x] 8. Docs. `docs/host-skill-contract.md`: one line naming
      `integrations/skills/cgraph-enrich` as the reference driver. `README.md:261-263`:
      replace the manual-copy sentence with the `cgraph skills install` verb, covering
      BOTH skills.
