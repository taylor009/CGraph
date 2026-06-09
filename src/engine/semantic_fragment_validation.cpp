#include "cgraph/semantic_fragment_validation.hpp"

#include "cgraph/fragment_json.hpp"

#include <fstream>

namespace cgraph {

SemanticFragmentValidationResult validate_semantic_fragment_json(const nlohmann::json& value) {
  SemanticFragmentValidationResult result;
  result.valid = parse_fragment(value, result.fragment, result.errors);
  return result;
}

SemanticFragmentValidationResult validate_semantic_fragment_file(const std::filesystem::path& path) {
  SemanticFragmentValidationResult result;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    result.errors.push_back("failed to open semantic fragment: " + path.generic_string());
    return result;
  }

  const auto value = nlohmann::json::parse(input, nullptr, false);
  if (value.is_discarded()) {
    result.errors.push_back("semantic fragment is malformed JSON: " + path.generic_string());
    return result;
  }

  return validate_semantic_fragment_json(value);
}

}  // namespace cgraph
