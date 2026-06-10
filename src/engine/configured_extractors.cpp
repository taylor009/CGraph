#include "cgraph/configured_extractors.hpp"

#include "cgraph/cpp_extractor.hpp"
#include "cgraph/javascript_extractor.hpp"
#include "cgraph/non_grammar_extractors.hpp"
#include "cgraph/python_extractor.hpp"

namespace cgraph {
namespace {

extern "C" const TSLanguage* tree_sitter_c();
extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_groovy();
extern "C" const TSLanguage* tree_sitter_java();
extern "C" const TSLanguage* tree_sitter_javascript();
extern "C" const TSLanguage* tree_sitter_kotlin();
extern "C" const TSLanguage* tree_sitter_python();
extern "C" const TSLanguage* tree_sitter_ruby();
extern "C" const TSLanguage* tree_sitter_scala();
extern "C" const TSLanguage* tree_sitter_typescript();
extern "C" const TSLanguage* tree_sitter_tsx();

[[nodiscard]] LanguageConfig c_config() {
  LanguageConfig config{
      .name = "c",
      .grammar_name = "tree-sitter-c",
      .extensions = {".c", ".h"},
      .class_node_types = {"struct_specifier", "union_specifier", "enum_specifier"},
      .function_node_types = {"function_definition"},
      .import_node_types = {"preproc_include"},
      .call_node_types = {"call_expression"},
      .name_fields = {"name", "declarator"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
  };
  // `#include` -> imports, struct members -> defines, member/param/return types
  // -> references. cpp_relation_handler also emits inherits, which is a no-op for
  // C (no base classes). Shared by the C and C++ configs.
  config.import_handler = cpp_import_handler;
  config.relation_handler = cpp_relation_handler;
  config.extra_walk = cpp_field_walk;
  return config;
}

[[nodiscard]] LanguageConfig cpp_config() {
  auto config = c_config();
  config.name = "cpp";
  config.grammar_name = "tree-sitter-cpp";
  config.extensions = {".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx"};
  config.class_node_types.push_back("class_specifier");
  config.class_node_types.push_back("namespace_definition");
  return config;
}

[[nodiscard]] LanguageConfig java_config() {
  return LanguageConfig{
      .name = "java",
      .grammar_name = "tree-sitter-java",
      .extensions = {".java"},
      .class_node_types = {"class_declaration", "interface_declaration", "enum_declaration", "record_declaration"},
      .function_node_types = {"method_declaration", "constructor_declaration"},
      .import_node_types = {"import_declaration"},
      .call_node_types = {"method_invocation", "object_creation_expression"},
      .name_fields = {"name"},
      .body_fields = {"body"},
      .call_accessor_fields = {"name"},
  };
}

[[nodiscard]] LanguageConfig ruby_config() {
  return LanguageConfig{
      .name = "ruby",
      .grammar_name = "tree-sitter-ruby",
      .extensions = {".rb"},
      .class_node_types = {"class", "module"},
      .function_node_types = {"method", "singleton_method"},
      .import_node_types = {"call"},
      .call_node_types = {"call", "command"},
      .name_fields = {"name", "method"},
      .body_fields = {"body"},
      .call_accessor_fields = {"method", "name"},
  };
}

[[nodiscard]] LanguageConfig kotlin_config() {
  return LanguageConfig{
      .name = "kotlin",
      .grammar_name = "tree-sitter-kotlin",
      .extensions = {".kt", ".kts"},
      .class_node_types = {"class_declaration", "object_declaration", "interface_declaration"},
      .function_node_types = {"function_declaration"},
      .import_node_types = {"import_header"},
      .call_node_types = {"call_expression"},
      .name_fields = {"name"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
  };
}

[[nodiscard]] LanguageConfig scala_config() {
  return LanguageConfig{
      .name = "scala",
      .grammar_name = "tree-sitter-scala",
      .extensions = {".scala", ".sc"},
      .class_node_types = {"class_definition", "object_definition", "trait_definition"},
      .function_node_types = {"function_definition"},
      .import_node_types = {"import_declaration"},
      .call_node_types = {"call_expression"},
      .name_fields = {"name"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
  };
}

[[nodiscard]] LanguageConfig groovy_config() {
  return LanguageConfig{
      .name = "groovy",
      .grammar_name = "tree-sitter-groovy",
      .extensions = {".groovy", ".gvy", ".gradle"},
      .class_node_types = {"class_definition"},
      .function_node_types = {"function_declaration", "function_definition"},
      .import_node_types = {"groovy_import"},
      .call_node_types = {"function_call", "juxt_function_call"},
      .name_fields = {"name", "function"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
  };
}

}  // namespace

const TSLanguage* tree_sitter_language_for(DetectedLanguage language) {
  switch (language) {
    case DetectedLanguage::C:
      return tree_sitter_c();
    case DetectedLanguage::Cpp:
      return tree_sitter_cpp();
    case DetectedLanguage::Groovy:
      return tree_sitter_groovy();
    case DetectedLanguage::Java:
      return tree_sitter_java();
    case DetectedLanguage::JavaScript:
      return tree_sitter_javascript();
    case DetectedLanguage::Kotlin:
      return tree_sitter_kotlin();
    case DetectedLanguage::Python:
      return tree_sitter_python();
    case DetectedLanguage::Ruby:
      return tree_sitter_ruby();
    case DetectedLanguage::Scala:
      return tree_sitter_scala();
    case DetectedLanguage::TypeScript:
      return tree_sitter_typescript();
    case DetectedLanguage::Tsx:
      return tree_sitter_tsx();
    default:
      return nullptr;
  }
}

std::optional<LanguageConfig> config_for_language(DetectedLanguage language) {
  switch (language) {
    case DetectedLanguage::C:
      return c_config();
    case DetectedLanguage::Cpp:
      return cpp_config();
    case DetectedLanguage::Groovy:
      return groovy_config();
    case DetectedLanguage::Java:
      return java_config();
    case DetectedLanguage::JavaScript:
      return javascript_language_config();
    case DetectedLanguage::Kotlin:
      return kotlin_config();
    case DetectedLanguage::Python:
      return python_language_config();
    case DetectedLanguage::Ruby:
      return ruby_config();
    case DetectedLanguage::Scala:
      return scala_config();
    case DetectedLanguage::TypeScript:
      return typescript_language_config();
    case DetectedLanguage::Tsx:
      return tsx_language_config();
    default:
      return std::nullopt;
  }
}

std::optional<ExtractionResult> extract_configured_language(
    DetectedLanguage language,
    const ExtractionContext& context) {
  if (auto result = extract_non_grammar_language(language, context); result.has_value()) {
    return result;
  }

  const auto* grammar = tree_sitter_language_for(language);
  auto config = config_for_language(language);
  if (grammar == nullptr || !config.has_value()) {
    return std::nullopt;
  }

  intern_node_symbols(*config, grammar);
  if (language == DetectedLanguage::Python) {
    return extract_python(context);
  }
  if (language == DetectedLanguage::JavaScript) {
    return extract_javascript(context);
  }
  if (language == DetectedLanguage::TypeScript) {
    return extract_typescript(context);
  }
  if (language == DetectedLanguage::Tsx) {
    return extract_tsx(context);
  }
  return extract_with_config(grammar, *config, context);
}

}  // namespace cgraph
