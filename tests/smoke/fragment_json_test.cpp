#include "cgraph/fragment_json.hpp"

#include <nlohmann/json.hpp>

#include <vector>

int main() {
  const auto input = nlohmann::json{
      {"nodes",
       nlohmann::json::array({
           {
               {"id", "foo"},
               {"label", "Foo"},
               {"source_file", "src/foo.cpp"},
               {"source_location",
                {
                    {"start_line", 1},
                    {"start_column", 2},
                    {"end_line", 3},
                    {"end_column", 4},
                }},
               {"type", "function"},
               {"confidence", "EXTRACTED"},
           },
       })},
      {"edges",
       nlohmann::json::array({
           {
               {"source", "foo"},
               {"target", "bar"},
               {"relation", "CALLS"},
               {"confidence", "INFERRED"},
               {"confidence_score", 0.75},
           },
       })},
      {"hyperedges", nlohmann::json::array()},
  };

  cgraph::Fragment fragment;
  std::vector<std::string> errors;
  if (!cgraph::parse_fragment(input, fragment, errors)) {
    return 1;
  }
  if (fragment.nodes.size() != 1 || fragment.edges.size() != 1) {
    return 1;
  }
  if (fragment.nodes[0].id != "foo" || fragment.edges[0].confidence != cgraph::Confidence::Inferred) {
    return 1;
  }

  const auto output = cgraph::to_json(fragment);
  if (output["nodes"][0]["confidence"] != "EXTRACTED") {
    return 1;
  }
  if (output["edges"][0]["confidence"] != "INFERRED") {
    return 1;
  }

  const auto invalid = nlohmann::json{
      {"nodes", nlohmann::json::array({{{"id", "missing-label"}}})},
      {"edges", nlohmann::json::array({{{"source", "foo"}, {"target", "bar"}}})},
  };
  if (cgraph::parse_fragment(invalid, fragment, errors)) {
    return 1;
  }
  if (errors.empty()) {
    return 1;
  }

  return 0;
}
