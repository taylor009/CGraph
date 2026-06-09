#pragma once

#include <nlohmann/json.hpp>

#include <functional>

namespace cgraph {

using McpForwarder = std::function<nlohmann::json(const nlohmann::json& daemon_request)>;

[[nodiscard]] nlohmann::json handle_mcp_request(const nlohmann::json& request, const McpForwarder& forwarder);

}  // namespace cgraph
