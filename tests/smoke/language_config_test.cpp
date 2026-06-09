#include "cgraph/language_config.hpp"

int main() {
  cgraph::LanguageConfig config{
      .name = "c",
      .grammar_name = "tree-sitter-c",
      .extensions = {".c", ".h"},
      .class_node_types = {"struct_specifier"},
      .function_node_types = {"function_definition"},
      .import_node_types = {"preproc_include"},
      .call_node_types = {"call_expression"},
      .name_fields = {"name", "declarator"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
  };

  if (config.name != "c" || config.function_node_types.empty() || config.call_accessor_fields.empty()) {
    return 1;
  }

  return 0;
}
