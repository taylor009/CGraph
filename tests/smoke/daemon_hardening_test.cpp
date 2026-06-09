#include "cgraph/client_runtime.hpp"
#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/daemon_security.hpp"
#include "cgraph/protocol.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

namespace {

std::optional<nlohmann::json> ok_response() {
  return nlohmann::json{{"ok", true}};
}

cgraph::GraphSnapshot one_node_graph(std::string id) {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = std::move(id), .label = graph.nodes.empty() ? "" : graph.nodes.back().id, .kind = "class"});
  graph.nodes.back().label = graph.nodes.back().id;
  return graph;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  {
    std::atomic<int> spawn_calls{0};
    std::atomic<bool> daemon_ready{false};
    cgraph::ClientRuntimeHooks hooks;
    hooks.connect = [&](const cgraph::DaemonIdentity&, const nlohmann::json&) -> std::optional<nlohmann::json> {
      if (daemon_ready.load()) {
        return ok_response();
      }
      return std::nullopt;
    };
    hooks.spawn = [&](const cgraph::DaemonIdentity&) {
      ++spawn_calls;
      std::this_thread::sleep_for(5ms);
      daemon_ready.store(true);
      return true;
    };
    hooks.sleep = [](std::chrono::milliseconds) {};

    cgraph::ClientRequest request{
        .project_root = std::filesystem::current_path(),
        .operation = "status",
        .max_connect_attempts = 4,
        .initial_backoff = 1ms,
    };
    std::vector<std::thread> clients;
    std::atomic<int> successes{0};
    for (int index = 0; index < 6; ++index) {
      clients.emplace_back([&] {
        if (cgraph::send_thin_client_request(request, hooks).response.has_value()) {
          ++successes;
        }
      });
    }
    for (auto& client : clients) {
      client.join();
    }
    if (spawn_calls.load() != 1 || successes.load() != 6) {
      return 1;
    }
  }

  {
    cgraph::DaemonState state;
    auto request = cgraph::make_request("status");
    request["protocol_version"] = cgraph::kProtocolVersion + 1;
    const auto response = cgraph::handle_daemon_request(state, request);
    if (response["ok"].get<bool>() || response.value("error", std::string{}).find("protocol version mismatch") == std::string::npos) {
      return 1;
    }
  }

  {
    const auto current = cgraph::current_user_id();
    if (cgraph::peer_is_authorized(cgraph::PeerCredentials{.available = true, .user_id = current + 1U}, current)) {
      return 1;
    }
  }

  {
    cgraph::DaemonLifecycleConfig config{
        .idle_timeout = 5s,
        .persist_interval = 30s,
    };
    cgraph::DaemonLifecycleState lifecycle;
    const auto start = cgraph::DaemonClock::time_point{} + 10s;
    cgraph::record_daemon_activity(lifecycle, start);
    if (cgraph::should_shutdown_for_idle(lifecycle, config, start + 4s) ||
        !cgraph::should_shutdown_for_idle(lifecycle, config, start + 5s)) {
      return 1;
    }
  }

  {
    cgraph::DaemonState state;
    cgraph::publish_graph_snapshot(state, one_node_graph("old"));
    std::atomic<bool> mutating{false};
    std::atomic<bool> finish{false};
    std::atomic<bool> saw_partial{false};

    std::thread writer([&] {
      cgraph::mutate_graph_snapshot(state, [&](cgraph::GraphSnapshot& graph) {
        graph.nodes.clear();
        graph.nodes.push_back(cgraph::Node{.id = "partial", .label = "Partial", .kind = "class"});
        mutating.store(true);
        while (!finish.load()) {
          std::this_thread::sleep_for(1ms);
        }
        graph.nodes.clear();
        graph.nodes.push_back(cgraph::Node{.id = "new", .label = "New", .kind = "class"});
      });
    });

    while (!mutating.load()) {
      std::this_thread::sleep_for(1ms);
    }
    for (int index = 0; index < 100; ++index) {
      const auto snapshot = cgraph::read_graph_snapshot(state);
      if (snapshot->nodes.size() != 1 || snapshot->nodes.front().id == "partial") {
        saw_partial.store(true);
      }
    }
    finish.store(true);
    writer.join();

    const auto snapshot = cgraph::read_graph_snapshot(state);
    if (saw_partial.load() || snapshot->nodes.front().id != "new") {
      return 1;
    }
  }

  return 0;
}
