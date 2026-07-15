#pragma once

#include "cgraph/launch_agent.hpp"

#include <filesystem>

namespace cgraph {

// launchd label for the scheduled enrichment drainer. The agent runs the
// host-side drain script; the binary only renders/installs the plist and never
// selects models or dispatches agents itself (host-skill contract).
inline constexpr const char* kDrainerLabel = "com.cgraph.enrich-drain";

struct DrainerConfig {
  std::filesystem::path script_path;        // integrations/always-on/cgraph-enrich-drain.sh
  std::filesystem::path cgraph_binary;      // absolute; script lists tracked repos with it
  std::filesystem::path client_binary;      // absolute; script reads status with it
  std::filesystem::path launch_agents_dir;  // default: ~/Library/LaunchAgents
  int interval_seconds = 14400;             // every 4 hours: drains a few-hundred-chunk
                                            // backlog (10 chunks/repo/run) within days, not weeks
  int chunk_cap = 10;                       // max chunks a single run may author, per repo
  // Host coding-agent CLI resolved at install time (absolute path), baked into
  // the agent's environment as CGRAPH_HOST_CLI. Install runs in the user's
  // interactive environment where shim/homebrew PATH entries exist; launchd's
  // does not, and the script's login-shell fallback misses .zshrc-managed PATH.
  // Empty -> no env entry; the script resolves at runtime as before.
  std::string host_cli;
};

// Absolute path of the host CLI as visible from the CURRENT environment:
// $CGRAPH_HOST_CLI when set, else `name` looked up on $PATH. Empty when
// unresolvable — install proceeds and runtime resolution remains the fallback.
[[nodiscard]] std::filesystem::path resolve_host_cli(const std::string& name = "claude");

// The drain script that ships beside the skills: walk up from the executable and
// then from cwd until an ancestor contains integrations/always-on/cgraph-enrich-drain.sh.
// Empty when neither walk finds it.
[[nodiscard]] std::filesystem::path resolve_drain_script(const std::filesystem::path& executable,
                                                         const std::filesystem::path& cwd);

// Pure spec: /bin/sh <script> <cgraph> <cgraph-client> <chunk-cap>, RunAtLoad +
// StartInterval. No filesystem or launchctl side effects.
[[nodiscard]] LaunchAgentSpec drainer_spec(const DrainerConfig& config);

// Plist present in launch_agents_dir.
[[nodiscard]] bool drainer_installed(const DrainerConfig& config);

// Write the plist and bootstrap it into the user domain. Returns false when the
// plist cannot be written or launchctl rejects it.
[[nodiscard]] bool drainer_install(const DrainerConfig& config);

// Boot the agent out and remove the plist. True when nothing managed remains.
[[nodiscard]] bool drainer_uninstall(const DrainerConfig& config);

}  // namespace cgraph
