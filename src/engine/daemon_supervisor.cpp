#include "cgraph/daemon_supervisor.hpp"

#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/launch_agent.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace cgraph {
namespace {

LaunchAgentSpec per_repo_spec(const SupervisorConfig& config, const TrackedRepo& repo) {
  LaunchAgentSpec spec;
  spec.label = per_repo_agent_label(repo.root_hash);
  // --idle-timeout 0 => the daemon never idle-shuts-down (resident/immortal).
  spec.program_args = {
      config.graphd_binary.string(), "--root", repo.root.string(), "--idle-timeout", "0"};
  spec.keep_alive = true;   // launchd restarts it on crash
  spec.run_at_load = true;  // and starts it at login
  return spec;
}

LaunchAgentSpec supervisor_spec(const SupervisorConfig& config) {
  LaunchAgentSpec spec;
  spec.label = kSupervisorLabel;
  spec.program_args = {config.cgraph_binary.string(), "daemon", "sync"};
  spec.keep_alive = false;                              // one-shot reconcile, not a resident proc
  spec.run_at_load = true;                              // reconcile at login
  spec.start_interval = config.reconcile_interval_seconds;  // and periodically after
  return spec;
}

}  // namespace

bool mcp_json_registers_cgraph(const std::filesystem::path& mcp_json) {
  std::ifstream input(mcp_json);
  if (!input) {
    return false;
  }
  try {
    const auto json = nlohmann::json::parse(input);
    const auto servers = json.find("mcpServers");
    if (servers == json.end() || !servers->is_object()) {
      return false;
    }
    return servers->contains("cgraph");
  } catch (const nlohmann::json::exception&) {
    return false;
  }
}

std::vector<TrackedRepo> discover_tracked_repos(const SupervisorConfig& config) {
  std::set<std::filesystem::path> excluded;
  for (const auto& entry : config.exclude) {
    excluded.insert(canonical_project_root(entry));
  }

  // Keyed by root_hash: dedups repos reachable via multiple search roots and
  // yields a deterministic order (std::map iterates sorted by key).
  std::map<std::string, TrackedRepo> by_hash;
  const auto consider = [&](const std::filesystem::path& dir) {
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) {
      return;
    }
    const auto mcp = dir / ".mcp.json";
    if (!std::filesystem::exists(mcp, error) || !mcp_json_registers_cgraph(mcp)) {
      return;
    }
    const auto canonical = canonical_project_root(dir);
    if (excluded.count(canonical) != 0) {
      return;
    }
    const auto hash = stable_project_hash(canonical);
    by_hash[hash] = TrackedRepo{canonical, hash};
  };

  for (const auto& search_root : config.search_roots) {
    consider(search_root);  // the search root may itself be a repo
    std::error_code error;
    std::filesystem::directory_iterator it(search_root, error);
    if (error) {
      continue;
    }
    for (; it != std::filesystem::directory_iterator(); it.increment(error)) {
      if (error) {
        break;
      }
      consider(it->path());
    }
  }

  std::vector<TrackedRepo> out;
  out.reserve(by_hash.size());
  for (auto& [hash, repo] : by_hash) {
    out.push_back(repo);
  }
  return out;
}

ReconcilePlan reconcile_tracked_repos(
    const std::vector<TrackedRepo>& discovered,
    const std::vector<std::string>& installed_hashes) {
  const std::set<std::string> installed(installed_hashes.begin(), installed_hashes.end());
  std::set<std::string> discovered_hashes;
  ReconcilePlan plan;
  for (const auto& repo : discovered) {
    discovered_hashes.insert(repo.root_hash);
    if (installed.count(repo.root_hash) == 0) {
      plan.to_add.push_back(repo);
    }
  }
  for (const auto& hash : installed_hashes) {
    if (discovered_hashes.count(hash) == 0) {
      plan.to_remove.push_back(hash);
    }
  }
  return plan;
}

std::vector<std::string> installed_managed_hashes(const std::filesystem::path& launch_agents_dir) {
  std::vector<std::string> out;
  std::error_code error;
  std::filesystem::directory_iterator it(launch_agents_dir, error);
  if (error) {
    return out;
  }
  for (; it != std::filesystem::directory_iterator(); it.increment(error)) {
    if (error) {
      break;
    }
    const auto& path = it->path();
    if (path.extension() != ".plist") {
      continue;
    }
    const auto hash = root_hash_from_label(path.stem().string());
    if (!hash.empty()) {
      out.push_back(hash);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

SupervisorSyncResult supervisor_sync(const SupervisorConfig& config, bool apply) {
  const auto discovered = discover_tracked_repos(config);
  const auto installed = installed_managed_hashes(config.launch_agents_dir);
  SupervisorSyncResult result;
  result.plan = reconcile_tracked_repos(discovered, installed);
  if (!apply) {
    return result;
  }
  for (const auto& repo : result.plan.to_add) {
    const auto spec = per_repo_spec(config, repo);
    const auto plist = write_launch_agent(config.launch_agents_dir, spec);
    if (plist.empty() || !launchctl_bootstrap(plist)) {
      result.failed.push_back(spec.label);
    }
  }
  for (const auto& hash : result.plan.to_remove) {
    const auto label = per_repo_agent_label(hash);
    (void)launchctl_bootout(label);  // best-effort stop; plist removal is what matters
    std::error_code error;
    std::filesystem::remove(config.launch_agents_dir / (label + ".plist"), error);
  }
  return result;
}

bool supervisor_install(const SupervisorConfig& config) {
  const auto spec = supervisor_spec(config);
  const auto plist = write_launch_agent(config.launch_agents_dir, spec);
  if (plist.empty() || !launchctl_bootstrap(plist)) {
    return false;
  }
  return supervisor_sync(config, /*apply=*/true).failed.empty();
}

bool supervisor_uninstall(const SupervisorConfig& config) {
  bool ok = true;
  for (const auto& hash : installed_managed_hashes(config.launch_agents_dir)) {
    const auto label = per_repo_agent_label(hash);
    (void)launchctl_bootout(label);
    std::error_code error;
    std::filesystem::remove(config.launch_agents_dir / (label + ".plist"), error);
    if (error) {
      ok = false;
    }
  }
  (void)launchctl_bootout(kSupervisorLabel);
  std::error_code error;
  std::filesystem::remove(config.launch_agents_dir / (std::string(kSupervisorLabel) + ".plist"), error);
  if (error) {
    ok = false;
  }
  return ok;
}

std::vector<SupervisorRepoStatus> supervisor_status(const SupervisorConfig& config) {
  std::vector<SupervisorRepoStatus> out;
  for (auto& repo : discover_tracked_repos(config)) {
    const auto identity = daemon_identity_for(repo.root);
    const bool live = unix_endpoint_is_live(unix_socket_path(identity));
    out.push_back(SupervisorRepoStatus{std::move(repo), live});
  }
  return out;
}

}  // namespace cgraph
