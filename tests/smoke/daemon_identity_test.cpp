#include "cgraph/daemon_identity.hpp"

#include <filesystem>

int main() {
  const auto root = std::filesystem::temp_directory_path() / "cgraph_daemon_identity_test";
  std::filesystem::create_directories(root / "subdir");

  const auto first = cgraph::daemon_identity_for(root);
  const auto second = cgraph::daemon_identity_for(root / "subdir" / "..");

  std::filesystem::remove_all(root);

  if (first.project_root != second.project_root) {
    return 1;
  }
  if (first.root_hash.empty() || first.root_hash != second.root_hash) {
    return 1;
  }
  if (first.endpoint_name.find("graphd-") != 0) {
    return 1;
  }

  return 0;
}
