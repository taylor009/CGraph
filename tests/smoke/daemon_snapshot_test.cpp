#include "cgraph/daemon_ops.hpp"

#include "cgraph/protocol.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

cgraph::GraphSnapshot graph_with_label(const std::string& id, const std::string& label) {
  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = id, .label = label, .kind = "class"});
  return graph;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  cgraph::DaemonState state;
  cgraph::publish_graph_snapshot(state, graph_with_label("old", "Old"));

  std::atomic<bool> writer_entered{false};
  std::atomic<bool> writer_can_publish{false};
  std::atomic<bool> saw_partial{false};
  std::atomic<bool> stop_reading{false};

  std::thread writer([&] {
    cgraph::mutate_graph_snapshot(state, [&](cgraph::GraphSnapshot& graph) {
      graph.nodes.clear();
      graph.nodes.push_back(cgraph::Node{.id = "partial", .label = "Partial", .kind = "class"});
      writer_entered.store(true);
      while (!writer_can_publish.load()) {
        std::this_thread::sleep_for(1ms);
      }
      graph.nodes.clear();
      graph.nodes.push_back(cgraph::Node{.id = "new", .label = "New", .kind = "class"});
    });
    stop_reading.store(true);
  });

  while (!writer_entered.load()) {
    std::this_thread::sleep_for(1ms);
  }

  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&] {
      while (!stop_reading.load()) {
        const auto snapshot = cgraph::read_graph_snapshot(state);
        if (snapshot->nodes.size() != 1 || snapshot->nodes.front().id == "partial") {
          saw_partial.store(true);
        }
      }
    });
  }

  std::this_thread::sleep_for(10ms);
  writer_can_publish.store(true);
  writer.join();
  for (auto& reader : readers) {
    reader.join();
  }

  const auto published = cgraph::read_graph_snapshot(state);
  if (saw_partial.load() || published->nodes.size() != 1 || published->nodes.front().id != "new") {
    return 1;
  }

  std::atomic<int> active_writers{0};
  std::atomic<int> max_active_writers{0};
  std::vector<std::thread> writers;
  for (int i = 0; i < 8; ++i) {
    writers.emplace_back([&] {
      cgraph::mutate_graph_snapshot(state, [&](cgraph::GraphSnapshot& graph) {
        const auto active = ++active_writers;
        if (active > max_active_writers.load()) {
          max_active_writers.store(active);
        }
        std::this_thread::sleep_for(1ms);
        graph.edges.push_back(cgraph::Edge{.source = "new", .target = "new", .relation = "SELF"});
        --active_writers;
      });
    });
  }
  for (auto& thread : writers) {
    thread.join();
  }

  const auto after_writes = cgraph::read_graph_snapshot(state);
  if (max_active_writers.load() != 1 || after_writes->edges.size() != 8) {
    return 1;
  }

  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (status["result"]["node_count"] != 1 || status["result"]["edge_count"] != 8) {
    return 1;
  }

  return 0;
}
