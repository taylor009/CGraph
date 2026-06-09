#include "cgraph/configured_extractors.hpp"

int main() {
  const auto languages = {
      cgraph::DetectedLanguage::C,
      cgraph::DetectedLanguage::Cpp,
      cgraph::DetectedLanguage::Groovy,
      cgraph::DetectedLanguage::Java,
      cgraph::DetectedLanguage::JavaScript,
      cgraph::DetectedLanguage::Kotlin,
      cgraph::DetectedLanguage::Python,
      cgraph::DetectedLanguage::Ruby,
      cgraph::DetectedLanguage::Scala,
      cgraph::DetectedLanguage::TypeScript,
      cgraph::DetectedLanguage::Tsx,
  };

  for (const auto language : languages) {
    auto config = cgraph::config_for_language(language);
    if (!config.has_value()) {
      return 1;
    }
    if (config->name.empty() || config->grammar_name.empty() || config->extensions.empty()) {
      return 1;
    }
    if (config->function_node_types.empty() && config->class_node_types.empty()) {
      return 1;
    }
  }

  if (cgraph::config_for_language(cgraph::DetectedLanguage::McpConfig).has_value()) {
    return 1;
  }

  return 0;
}
