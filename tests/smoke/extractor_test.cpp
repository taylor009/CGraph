#include "cgraph/extractor.hpp"

extern "C" const TSLanguage* tree_sitter_c();

int main() {
  auto config = cgraph::LanguageConfig{
      .name = "c",
      .grammar_name = "tree-sitter-c",
      .extensions = {".c", ".h"},
      .function_node_types = {"function_definition"},
      .call_node_types = {"call_expression"},
      .name_fields = {"declarator"},
      .call_accessor_fields = {"function"},
  };
  cgraph::intern_node_symbols(config, tree_sitter_c());

  constexpr auto source = R"c(
int helper(void) { return 0; }
int main(void) { return helper(); }
)c";

  const auto result = cgraph::extract_with_config(
      tree_sitter_c(),
      config,
      cgraph::ExtractionContext{.source_file = "main.c", .source = source});

  // One file node plus the two functions it contains.
  if (result.fragment.nodes.size() != 3) {
    return 1;
  }
  std::size_t file_nodes = 0;
  for (const auto& node : result.fragment.nodes) {
    if (node.kind == "file") {
      ++file_nodes;
    }
  }
  if (file_nodes != 1) {
    return 1;
  }
  // The file should contain both functions via `contains` edges.
  std::size_t contains_edges = 0;
  for (const auto& edge : result.fragment.edges) {
    if (edge.relation == "contains") {
      ++contains_edges;
    }
  }
  if (contains_edges != 2) {
    return 1;
  }
  if (result.raw_calls.empty()) {
    return 1;
  }
  return 0;
}
