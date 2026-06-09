#include "cgraph/mcp_server.hpp"

#include <nlohmann/json.hpp>

#include <string>

int main() {
  nlohmann::json forwarded;
  cgraph::McpForwarder forwarder = [&](const nlohmann::json& request) {
    forwarded = request;
    return nlohmann::json{{"ok", true}, {"result", {{"nodes", {{{"id", "a"}, {"label", "Alpha"}}}}}}};
  };

  const auto initialized = cgraph::handle_mcp_request(
      nlohmann::json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", nlohmann::json::object()}},
      forwarder);
  if (initialized["jsonrpc"] != "2.0" || initialized["id"] != 1 || !initialized["result"].contains("capabilities")) {
    return 1;
  }

  const auto listed = cgraph::handle_mcp_request(
      nlohmann::json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", nlohmann::json::object()}},
      forwarder);
  if (listed["result"]["tools"].empty() || listed["result"]["tools"][0]["name"] != "graph_query") {
    return 1;
  }

  const auto called = cgraph::handle_mcp_request(
      nlohmann::json{
          {"jsonrpc", "2.0"},
          {"id", 3},
          {"method", "tools/call"},
          {"params", {{"name", "graph_query"}, {"arguments", {{"query", "Alpha"}}}}}},
      forwarder);
  if (forwarded["op"] != "query" || forwarded["params"]["q"] != "Alpha" ||
      called["result"]["content"][0]["type"] != "text" ||
      called["result"]["content"][0]["text"].get<std::string>().find("Alpha") == std::string::npos) {
    return 1;
  }

  const auto unknown = cgraph::handle_mcp_request(
      nlohmann::json{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"}, {"params", {{"name", "missing"}}}},
      forwarder);
  if (!unknown.contains("error") || unknown["error"]["code"] != -32602) {
    return 1;
  }

  return 0;
}
