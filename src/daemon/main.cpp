#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/engine.hpp"
#include "cgraph/protocol.hpp"

#include <iostream>
#include <string>

namespace {

void print_usage() {
  std::cout << "Usage: graphd [--version] [--benchmark-query --graph PATH --query TEXT]\n";
}

}  // namespace

int main(int argc, char** argv) {
  const auto info = cgraph::build_info();
  std::filesystem::path graph_path;
  std::string query;
  bool benchmark_query = false;

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--version") {
      std::cout << "graphd " << info.version << '\n';
      return 0;
    }
    if (arg == "--benchmark-query") {
      benchmark_query = true;
      continue;
    }
    if (arg == "--graph" && index + 1 < argc) {
      graph_path = argv[++index];
      continue;
    }
    if (arg == "--query" && index + 1 < argc) {
      query = argv[++index];
      continue;
    }
    std::cerr << "unknown argument: " << arg << '\n';
    return 2;
  }

  if (benchmark_query) {
    if (graph_path.empty() || query.empty()) {
      std::cerr << "--benchmark-query requires --graph and --query\n";
      return 2;
    }
    cgraph::DaemonState state;
    if (!cgraph::load_graph_snapshot(state, graph_path)) {
      std::cerr << "failed to load graph: " << graph_path << '\n';
      return 1;
    }
    const auto response = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", query}}));
    if (!response.value("ok", false)) {
      std::cerr << response.value("error", std::string{"daemon query failed"}) << '\n';
      return 1;
    }
    std::cout << response.dump(2) << '\n';
    return 0;
  }

  std::cout << "graphd " << info.version << '\n';
  return 0;
}
