#include "cgraph/dedup.hpp"

#include <iostream>

int main() {
  if (cgraph::shannon_entropy("aaaaaa") >= 2.5) {
    return 1;
  }
  if (cgraph::jaro_winkler_similarity("service", "service") != 1.0) {
    return 1;
  }
  if (cgraph::jaro_winkler_similarity("payment_service", "payment_services") < 0.92) {
    return 1;
  }

  cgraph::GraphSnapshot graph;
  graph.nodes.push_back(cgraph::Node{.id = "a", .label = "PaymentService", .source_file = "a.cpp", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Payment Service", .source_file = "b.cpp", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{.id = "c", .label = "aaaaaa", .source_file = "c.cpp", .kind = "class"});
  graph.nodes.push_back(cgraph::Node{
      .id = "d",
      .label = "PaymentServic",
      .source_file = "d.cpp",
      .kind = "class",
      .properties = {{"community", "payments"}},
  });
  graph.nodes.push_back(cgraph::Node{
      .id = "e",
      .label = "PaymentServce",
      .source_file = "e.cpp",
      .kind = "class",
      .properties = {{"community", "payments"}},
  });
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "USES"});
  graph.edges.push_back(cgraph::Edge{.source = "d", .target = "e", .relation = "USES"});

  cgraph::semantic_dedup(graph);

  if (graph.nodes.size() != 2) {
    std::cerr << "expected 2 nodes after dedup, got " << graph.nodes.size() << '\n';
    for (const auto& node : graph.nodes) {
      std::cerr << node.id << ":" << node.label << '\n';
    }
    return 1;
  }
  for (const auto& edge : graph.edges) {
    if (edge.source == "b" || edge.target == "b" || edge.source == "d" || edge.target == "d" ||
        edge.source == "e" || edge.target == "e") {
      std::cerr << "edge still references duplicate id: " << edge.source << " -> " << edge.target << '\n';
      return 1;
    }
  }
  bool saw_low_entropy = false;
  for (const auto& node : graph.nodes) {
    if (node.id == "c") {
      saw_low_entropy = true;
    }
  }
  if (!saw_low_entropy) {
    std::cerr << "low entropy node was incorrectly deduplicated\n";
    return 1;
  }

  // Regression: a component and its props interface in the same file share a
  // prefix ("MessageBubble" / "MessageBubbleProps"). Jaro-Winkler's prefix
  // bonus scores them high, but they are distinct symbols — merging them
  // deletes the function node and mis-attributes its calls to the type. A
  // prefix-extension pair must never merge. Likewise a name shared across two
  // files ("Helper" in two modules) is two symbols, not one.
  cgraph::GraphSnapshot guard;
  guard.nodes.push_back(cgraph::Node{.id = "mb", .label = "MessageBubbleProps", .source_file = "mb.tsx", .kind = "type"});
  guard.nodes.push_back(cgraph::Node{.id = "mf", .label = "MessageBubble", .source_file = "mb.tsx", .kind = "function"});
  guard.nodes.push_back(cgraph::Node{.id = "h1", .label = "HelperWidget", .source_file = "one.tsx", .kind = "function"});
  guard.nodes.push_back(cgraph::Node{.id = "h2", .label = "HelperWidget", .source_file = "two.tsx", .kind = "function"});
  // Two same-label declarations in ONE file are a genuine duplicate and merge.
  guard.nodes.push_back(cgraph::Node{.id = "d1", .label = "ConfigLoader", .source_file = "dup.tsx", .kind = "function"});
  guard.nodes.push_back(cgraph::Node{.id = "d2", .label = "ConfigLoader", .source_file = "dup.tsx", .kind = "function"});
  // Two sibling files with near-identical path-tail labels are DISTINCT files
  // and must never merge (a file's identity is its path). Merging one away would
  // destroy every import/contains edge that referenced it.
  guard.nodes.push_back(cgraph::Node{.id = "f1", .label = "viewers/compiq-viewer.tsx", .source_file = "viewers/compiq-viewer.tsx", .kind = "file"});
  guard.nodes.push_back(cgraph::Node{.id = "f2", .label = "viewers/compiq-viewer-states.tsx", .source_file = "viewers/compiq-viewer-states.tsx", .kind = "file"});

  cgraph::semantic_dedup(guard);

  bool saw_props = false;
  bool saw_func = false;
  std::size_t helper_widgets = 0;
  std::size_t config_loaders = 0;
  std::size_t file_nodes = 0;
  for (const auto& node : guard.nodes) {
    saw_props = saw_props || node.label == "MessageBubbleProps";
    saw_func = saw_func || node.label == "MessageBubble";
    helper_widgets += node.label == "HelperWidget" ? 1 : 0;
    config_loaders += node.label == "ConfigLoader" ? 1 : 0;
    file_nodes += node.kind == "file" ? 1 : 0;
  }
  if (file_nodes != 2) {
    std::cerr << "sibling file nodes were incorrectly merged\n";
    return 1;
  }
  if (!saw_props || !saw_func) {
    std::cerr << "prefix-extension pair was incorrectly merged\n";
    return 1;
  }
  if (helper_widgets != 2) {
    std::cerr << "cross-file identical labels were incorrectly merged\n";
    return 1;
  }
  if (config_loaders != 1) {
    std::cerr << "same-file duplicate labels were not merged\n";
    return 1;
  }
  return 0;
}
