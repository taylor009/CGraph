#pragma once

#include "cgraph/types.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cgraph {

// Result of generating a cross-service seam fragment. On success `ok` is true and
// `fragment` holds the contract graph; on any hard error (malformed spec, unknown
// endpoint/schema reference, missing consumer graph, or an anchor that resolves to
// no node) `ok` is false, `fragment` is empty, and `errors` explains why -- a
// partial or dangling seam is never produced. `resolution_log` records every
// resolved anchor (for stderr auditability) on success.
struct SeamResult {
  bool ok = false;
  Fragment fragment;
  std::vector<std::string> errors;
  std::vector<std::string> resolution_log;
};

// Generate a cross-service contract fragment from a host-authored seam spec and the
// named consumer code graphs (name -> path to that service's graph.json). Anchors
// in the spec are resolved against the real graphs; this is deterministic and
// fail-loud. See docs/host-skill-contract.md and the cross-service-seam capability.
[[nodiscard]] SeamResult generate_seam(
    const nlohmann::json& spec,
    const std::unordered_map<std::string, std::filesystem::path>& graph_paths);

}  // namespace cgraph
