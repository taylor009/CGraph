#include "cgraph/python_extractor.hpp"

#include "cgraph/normalize.hpp"

#include <string>
#include <string_view>

namespace cgraph {
namespace {

extern "C" const TSLanguage* tree_sitter_python();

[[nodiscard]] std::string node_text(const TSNode& node, std::string_view source) {
  const auto start = ts_node_start_byte(node);
  const auto end = ts_node_end_byte(node);
  if (start >= end || end > source.size()) {
    return {};
  }
  return std::string(source.substr(start, end - start));
}

[[nodiscard]] SourceLocation source_location(const TSNode& node) {
  const auto start = ts_node_start_point(node);
  const auto end = ts_node_end_point(node);
  return SourceLocation{
      .start_line = start.row + 1,
      .start_column = start.column,
      .end_line = end.row + 1,
      .end_column = end.column,
  };
}

void python_import_handler(const TSNode& node, const ExtractionContext& context, Fragment& fragment) {
  auto label = node_text(node, context.source);
  if (label.empty()) {
    return;
  }

  fragment.nodes.push_back(Node{
      .id = make_id(context.source_file + ":" + label),
      .label = std::move(label),
      .source_file = context.source_file,
      .source_location = source_location(node),
      .kind = "import",
      .confidence = Confidence::Extracted,
  });
}

}  // namespace

LanguageConfig python_language_config() {
  return LanguageConfig{
      .name = "python",
      .grammar_name = "tree-sitter-python",
      .extensions = {".py", ".pyw"},
      .class_node_types = {"class_definition"},
      .function_node_types = {"function_definition"},
      .import_node_types = {"import_statement", "import_from_statement"},
      .call_node_types = {"call"},
      .name_fields = {"name"},
      .body_fields = {"body"},
      .call_accessor_fields = {"function"},
      .import_handler = python_import_handler,
  };
}

ExtractionResult extract_python(const ExtractionContext& context) {
  auto config = python_language_config();
  intern_node_symbols(config, tree_sitter_python());
  return extract_with_config(tree_sitter_python(), config, context);
}

}  // namespace cgraph
