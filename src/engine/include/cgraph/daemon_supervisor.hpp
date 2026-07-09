#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cgraph {

// A repo cgraph tracks for background refresh: a project root whose .mcp.json
// registers a cgraph MCP server. Keyed by the same canonical-root hash the daemon
// uses for its endpoint identity, so the discovery key, the LaunchAgent label, and
// the socket name all agree.
struct TrackedRepo {
  std::filesystem::path root;  // canonical project root
  std::string root_hash;       // stable_project_hash(root)
};

// Where the supervisor looks, and the binaries it wires into LaunchAgents.
struct SupervisorConfig {
  std::vector<std::filesystem::path> search_roots;  // scanned: each dir + its immediate children
  std::vector<std::filesystem::path> exclude;       // roots to skip (canonicalized on compare)
  std::filesystem::path launch_agents_dir;          // where plists are written (default: ~/Library/LaunchAgents)
  std::filesystem::path graphd_binary;              // absolute path to graphd (per-repo agents)
  std::filesystem::path cgraph_binary;              // absolute path to cgraph (supervisor agent runs `daemon sync`)
  int reconcile_interval_seconds = 300;             // supervisor StartInterval
};

// True if mcp_json parses and registers an `mcpServers.cgraph` entry.
[[nodiscard]] bool mcp_json_registers_cgraph(const std::filesystem::path& mcp_json);

// Scan each search root (the dir itself and its immediate children) for a
// .mcp.json that registers cgraph. Returns tracked repos sorted by root_hash,
// deduped, with `exclude` roots removed. Deterministic for a given tree.
[[nodiscard]] std::vector<TrackedRepo> discover_tracked_repos(const SupervisorConfig& config);

// Pure set difference keyed by root_hash: which discovered repos need an agent
// started (to_add) and which installed agent hashes are no longer discovered
// (to_remove). Idempotent — identical inputs yield empty diffs.
struct ReconcilePlan {
  std::vector<TrackedRepo> to_add;
  std::vector<std::string> to_remove;  // root hashes
};
[[nodiscard]] ReconcilePlan reconcile_tracked_repos(
    const std::vector<TrackedRepo>& discovered,
    const std::vector<std::string>& installed_hashes);

// Root hashes of the managed per-repo LaunchAgents currently present in
// launch_agents_dir (parsed from com.cgraph.graphd.<hash>.plist filenames).
[[nodiscard]] std::vector<std::string> installed_managed_hashes(
    const std::filesystem::path& launch_agents_dir);

// Reconcile the managed per-repo agents against discovery. When apply is true,
// each to_add gets a plist written + bootstrapped and each to_remove gets booted
// out + its plist removed. Returns the plan it acted on (for inspection/tests).
struct SupervisorSyncResult {
  ReconcilePlan plan;
  std::vector<std::string> failed;  // labels that failed to bootstrap/bootout
};
[[nodiscard]] SupervisorSyncResult supervisor_sync(const SupervisorConfig& config, bool apply);

// Write + bootstrap the supervisor agent (which runs `cgraph daemon sync` at load
// and every reconcile_interval_seconds), then run one sync. Returns false on error.
[[nodiscard]] bool supervisor_install(const SupervisorConfig& config);

// Boot out + remove the supervisor agent and every managed per-repo agent.
// Reverses supervisor_install / supervisor_sync — leaves no managed plist behind.
[[nodiscard]] bool supervisor_uninstall(const SupervisorConfig& config);

// Per-repo liveness for the `status` subcommand.
struct SupervisorRepoStatus {
  TrackedRepo repo;
  bool daemon_live = false;  // a daemon is currently listening on the repo's socket
};
[[nodiscard]] std::vector<SupervisorRepoStatus> supervisor_status(const SupervisorConfig& config);

}  // namespace cgraph
