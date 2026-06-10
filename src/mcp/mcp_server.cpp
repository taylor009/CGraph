#include "cgraph/mcp_server.hpp"

#include "cgraph/protocol.hpp"

#include <string>
#include <utility>

namespace cgraph {
namespace {

[[nodiscard]] nlohmann::json response(const nlohmann::json& id, nlohmann::json result) {
  return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

[[nodiscard]] nlohmann::json error_response(const nlohmann::json& id, int code, std::string message) {
  return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

[[nodiscard]] nlohmann::json text_content(const nlohmann::json& value) {
  return nlohmann::json{{"content", {{{"type", "text"}, {"text", value.dump()}}}}};
}

[[nodiscard]] nlohmann::json tool_schema(std::string name, std::string description, nlohmann::json properties) {
  return nlohmann::json{
      {"name", std::move(name)},
      {"description", std::move(description)},
      {"inputSchema", {{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", true}}},
  };
}

[[nodiscard]] nlohmann::json tools() {
  return nlohmann::json::array({
      tool_schema("graph_query", "Search graph nodes by query text", {{"query", {{"type", "string"}}}}),
      tool_schema("graph_path", "Find a graph path between node ids", {{"source", {{"type", "string"}}}, {"target", {{"type", "string"}}}}),
      tool_schema("graph_explain", "Explain a graph node and its neighbors", {{"id", {{"type", "string"}}}}),
      tool_schema(
          "graph_impact",
          "Transitive blast radius of a node: dependents (callers/importers that break if it changes), "
          "dependencies (what it relies on), or both, bounded by max_depth",
          {{"id", {{"type", "string"}}},
           {"direction", {{"type", "string"}, {"enum", {"dependents", "dependencies", "both"}}}},
           {"relation", {{"type", "string"}}},
           {"max_depth", {{"type", "integer"}}},
           {"limit", {{"type", "integer"}}}}),
      tool_schema(
          "graph_context",
          "Token-budgeted context bundle for a symbol: the focal node plus its most relevant "
          "neighbors with source snippets, greedily packed to fit a token budget",
          {{"id", {{"type", "string"}}},
           {"query", {{"type", "string"}}},
           {"budget", {{"type", "integer"}}},
           {"max_depth", {{"type", "integer"}}}}),
      tool_schema("graph_update", "Request a deterministic graph update", {{"path", {{"type", "string"}}}}),
      tool_schema("graph_status", "Return daemon graph and enrichment status", nlohmann::json::object()),
      tool_schema("graph_shutdown", "Ask the daemon to shut down", nlohmann::json::object()),
  });
}

[[nodiscard]] nlohmann::json daemon_request_for_tool(std::string_view name, const nlohmann::json& arguments) {
  if (name == "graph_query") {
    return make_request("query", {{"q", arguments.value("query", arguments.value("q", std::string{}))}});
  }
  if (name == "graph_path") {
    return make_request("path", {{"source", arguments.value("source", std::string{})}, {"target", arguments.value("target", std::string{})}});
  }
  if (name == "graph_explain") {
    return make_request("explain", {{"id", arguments.value("id", std::string{})}});
  }
  if (name == "graph_impact") {
    // Forward arguments verbatim; the daemon op applies direction/depth/limit
    // defaults. id is required and carried through.
    return make_request("impact", arguments);
  }
  if (name == "graph_context") {
    // Forward arguments verbatim; the daemon op applies budget/depth defaults and
    // resolves the focal node from id, label, or query.
    return make_request("context", arguments);
  }
  if (name == "graph_update") {
    return make_request("update", arguments.empty() ? nlohmann::json::object() : arguments);
  }
  if (name == "graph_status") {
    return make_request("status");
  }
  if (name == "graph_shutdown") {
    return make_request("shutdown");
  }
  return {};
}

}  // namespace

nlohmann::json handle_mcp_request(const nlohmann::json& request, const McpForwarder& forwarder) {
  const auto id = request.value("id", nlohmann::json(nullptr));
  const auto method = request.value("method", std::string{});
  if (request.value("jsonrpc", std::string{}) != "2.0" || method.empty()) {
    return error_response(id, -32600, "invalid JSON-RPC request");
  }

  if (method == "initialize") {
    return response(id, {{"protocolVersion", "2024-11-05"}, {"capabilities", {{"tools", nlohmann::json::object()}}}, {"serverInfo", {{"name", "cgraph-mcp"}, {"version", "0.1.0"}}}});
  }
  if (method == "tools/list") {
    return response(id, {{"tools", tools()}});
  }
  if (method == "notifications/initialized") {
    return nlohmann::json{};
  }
  if (method != "tools/call") {
    return error_response(id, -32601, "method not found");
  }

  const auto params = request.value("params", nlohmann::json::object());
  const auto name = params.value("name", std::string{});
  const auto arguments = params.value("arguments", nlohmann::json::object());
  const auto daemon_request = daemon_request_for_tool(name, arguments);
  if (daemon_request.empty()) {
    return error_response(id, -32602, "unknown tool: " + name);
  }
  if (!forwarder) {
    return error_response(id, -32603, "missing daemon forwarder");
  }

  const auto daemon_response = forwarder(daemon_request);
  if (!daemon_response.value("ok", false)) {
    return error_response(id, -32603, daemon_response.value("error", std::string{"daemon request failed"}));
  }
  return response(id, text_content(daemon_response.value("result", nlohmann::json::object())));
}

}  // namespace cgraph
