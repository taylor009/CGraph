#include "cgraph/daemon_ops.hpp"
#include "cgraph/mcp_server.hpp"

#include <iostream>
#include <string>

int main() {
  cgraph::DaemonState state;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = nlohmann::json::parse(line, nullptr, false);
    if (request.is_discarded()) {
      std::cout << nlohmann::json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "parse error"}}}}.dump() << '\n';
      continue;
    }
    const auto response = cgraph::handle_mcp_request(request, [&](const nlohmann::json& daemon_request) {
      return cgraph::handle_daemon_request(state, daemon_request);
    });
    if (!response.empty()) {
      std::cout << response.dump() << '\n';
    }
  }
  return 0;
}
