#pragma once

#include "cgraph/daemon_identity.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cgraph {

struct ClientRequest {
  std::filesystem::path project_root;
  std::string operation;
  nlohmann::json params = nlohmann::json::object();
  std::filesystem::path daemon_path;
  int max_connect_attempts = 8;
  std::chrono::milliseconds initial_backoff{10};
};

struct ClientResult {
  std::optional<nlohmann::json> response;
  bool spawned = false;
  int connect_attempts = 0;
  std::string error;
};

struct ClientRuntimeHooks {
  std::function<std::optional<nlohmann::json>(const DaemonIdentity&, const nlohmann::json&)> connect;
  std::function<bool(const DaemonIdentity&)> spawn;
  std::function<void(std::chrono::milliseconds)> sleep;
};

[[nodiscard]] ClientRuntimeHooks default_client_runtime_hooks(const ClientRequest& request);
[[nodiscard]] ClientResult send_thin_client_request(const ClientRequest& request, ClientRuntimeHooks hooks);

}  // namespace cgraph
