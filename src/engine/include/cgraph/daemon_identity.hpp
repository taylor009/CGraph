#pragma once

#include <filesystem>
#include <string>

namespace cgraph {

struct DaemonIdentity {
  std::filesystem::path project_root;
  std::string root_hash;
  std::string endpoint_name;
};

[[nodiscard]] std::filesystem::path canonical_project_root(const std::filesystem::path& path);
[[nodiscard]] std::string stable_project_hash(const std::filesystem::path& canonical_root);
[[nodiscard]] DaemonIdentity daemon_identity_for(const std::filesystem::path& project_root);

}  // namespace cgraph
