#include "cgraph/daemon_identity.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace cgraph {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

}  // namespace

std::filesystem::path canonical_project_root(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (error) {
    canonical = std::filesystem::absolute(path, error);
  }
  if (error) {
    canonical = path;
  }
  return canonical.lexically_normal();
}

std::string stable_project_hash(const std::filesystem::path& canonical_root) {
  const auto value = canonical_root.generic_string();
  std::uint64_t hash = kFnvOffset;
  for (const auto ch : value) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= kFnvPrime;
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

DaemonIdentity daemon_identity_for(const std::filesystem::path& project_root) {
  auto canonical = canonical_project_root(project_root);
  auto hash = stable_project_hash(canonical);
  return DaemonIdentity{
      .project_root = std::move(canonical),
      .root_hash = hash,
      .endpoint_name = "graphd-" + hash,
  };
}

}  // namespace cgraph
