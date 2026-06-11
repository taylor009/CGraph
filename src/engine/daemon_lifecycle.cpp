#include "cgraph/daemon_lifecycle.hpp"

#include "cgraph/export_json.hpp"
#include "cgraph/fragment_json.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace cgraph {
namespace {

[[nodiscard]] BuildState parse_build_state(int value) {
  switch (value) {
    case static_cast<int>(BuildState::DeterministicReady):
      return BuildState::DeterministicReady;
    case static_cast<int>(BuildState::Enriching):
      return BuildState::Enriching;
    case static_cast<int>(BuildState::Idle):
      return BuildState::Idle;
    case static_cast<int>(BuildState::Failed):
      return BuildState::Failed;
    case static_cast<int>(BuildState::Empty):
    default:
      return BuildState::Empty;
  }
}

[[nodiscard]] Node parse_node_link_node(const nlohmann::json& value) {
  Node node;
  node.id = value.value("id", std::string{});
  node.label = value.value("label", node.id);
  node.source_file = value.value("source_file", std::string{});
  node.kind = value.value("type", value.value("kind", std::string{}));
  if (const auto iter = value.find("confidence"); iter != value.end() && iter->is_string()) {
    Confidence confidence = Confidence::Extracted;
    if (parse_confidence(iter->get<std::string>(), confidence)) {
      node.confidence = confidence;
    }
  }
  if (const auto iter = value.find("confidence_score"); iter != value.end() && iter->is_number()) {
    node.confidence_score = iter->get<double>();
  }
  if (const auto iter = value.find("properties"); iter != value.end() && iter->is_object()) {
    for (const auto& [key, property] : iter->items()) {
      node.properties.emplace(key, property.is_string() ? property.get<std::string>() : property.dump());
    }
  }
  return node;
}

[[nodiscard]] Edge parse_node_link_edge(const nlohmann::json& value) {
  Edge edge;
  edge.source = value.value("source", std::string{});
  edge.target = value.value("target", std::string{});
  edge.relation = value.value("relation", std::string{});
  if (const auto iter = value.find("confidence"); iter != value.end() && iter->is_string()) {
    Confidence confidence = Confidence::Extracted;
    if (parse_confidence(iter->get<std::string>(), confidence)) {
      edge.confidence = confidence;
    }
  }
  if (const auto iter = value.find("confidence_score"); iter != value.end() && iter->is_number()) {
    edge.confidence_score = iter->get<double>();
  }
  if (const auto iter = value.find("properties"); iter != value.end() && iter->is_object()) {
    for (const auto& [key, property] : iter->items()) {
      edge.properties.emplace(key, property.is_string() ? property.get<std::string>() : property.dump());
    }
  }
  return edge;
}

[[nodiscard]] GraphSnapshot parse_node_link_graph(const nlohmann::json& value) {
  GraphSnapshot graph;
  const auto meta = value.value("graph", nlohmann::json::object());
  graph.build_state = parse_build_state(meta.value("build_state", static_cast<int>(BuildState::Empty)));
  graph.cache_hit_rate = meta.value("cache_hit_rate", 0.0);

  for (const auto& node : value.value("nodes", nlohmann::json::array())) {
    graph.nodes.push_back(parse_node_link_node(node));
  }
  for (const auto& edge : value.value("links", nlohmann::json::array())) {
    graph.edges.push_back(parse_node_link_edge(edge));
  }
  return graph;
}

}  // namespace

void record_daemon_activity(DaemonLifecycleState& lifecycle, DaemonClock::time_point now) {
  lifecycle.last_activity = now;
}

void mark_graph_dirty(DaemonLifecycleState& lifecycle, DaemonClock::time_point now) {
  // dirty_since anchors to the FIRST unpersisted change: re-marking on every
  // subsequent edit must not push the persist deadline out, or a steady edit
  // stream would starve persistence indefinitely.
  if (!lifecycle.graph_dirty) {
    lifecycle.dirty_since = now;
  }
  lifecycle.graph_dirty = true;
}

bool should_shutdown_for_idle(
    const DaemonLifecycleState& lifecycle,
    const DaemonLifecycleConfig& config,
    DaemonClock::time_point now) {
  return now - lifecycle.last_activity >= config.idle_timeout;
}

bool cleanup_daemon_endpoint(const std::filesystem::path& endpoint_path) {
  if (endpoint_path.empty()) {
    return true;
  }
  std::error_code error;
  std::filesystem::remove(endpoint_path, error);
  return !error;
}

bool persist_graph_snapshot(const DaemonState& state, const std::filesystem::path& graph_path) {
  if (graph_path.empty()) {
    return false;
  }

  std::error_code error;
  std::filesystem::create_directories(graph_path.parent_path(), error);
  if (error) {
    return false;
  }

  const auto temp_path = graph_path.parent_path() / (graph_path.filename().string() + ".tmp");
  {
    std::ofstream output(temp_path);
    if (!output) {
      return false;
    }
    output << to_node_link_json(*read_graph_snapshot(state)).dump(2) << '\n';
  }

  std::filesystem::rename(temp_path, graph_path, error);
  if (!error) {
    return true;
  }

  std::filesystem::remove(graph_path, error);
  error.clear();
  std::filesystem::rename(temp_path, graph_path, error);
  return !error;
}

bool load_graph_snapshot(DaemonState& state, const std::filesystem::path& graph_path) {
  std::ifstream input(graph_path);
  if (!input) {
    return false;
  }

  try {
    const auto json = nlohmann::json::parse(input);
    publish_graph_snapshot(state, parse_node_link_graph(json));
  } catch (const nlohmann::json::exception&) {
    return false;
  }
  return true;
}

bool persist_if_due(
    const DaemonState& state,
    DaemonLifecycleState& lifecycle,
    const DaemonLifecycleConfig& config,
    DaemonClock::time_point now) {
  if (!lifecycle.graph_dirty || now - lifecycle.dirty_since < config.persist_interval) {
    return false;
  }
  if (!persist_graph_snapshot(state, config.graph_path)) {
    return false;
  }
  lifecycle.graph_dirty = false;
  lifecycle.last_persist = now;
  return true;
}

}  // namespace cgraph
