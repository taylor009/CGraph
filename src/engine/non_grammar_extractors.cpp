#include "cgraph/non_grammar_extractors.hpp"

#include "cgraph/normalize.hpp"

#include <nlohmann/json.hpp>

#include <regex>
#include <string>
#include <string_view>

namespace cgraph {
namespace {

[[nodiscard]] SourceLocation line_location(std::string_view source, std::size_t offset) {
  std::uint32_t line = 1;
  std::uint32_t column = 0;
  for (std::size_t index = 0; index < offset && index < source.size(); ++index) {
    if (source[index] == '\n') {
      ++line;
      column = 0;
    } else {
      ++column;
    }
  }
  return SourceLocation{
      .start_line = line,
      .start_column = column,
      .end_line = line,
      .end_column = column,
  };
}

void add_node(
    Fragment& fragment,
    const ExtractionContext& context,
    std::string label,
    std::string kind,
    std::size_t offset = 0) {
  if (label.empty()) {
    return;
  }

  fragment.nodes.push_back(Node{
      .id = make_id(context.source_file + ":" + kind + ":" + label),
      .label = std::move(label),
      .source_file = context.source_file,
      .source_location = line_location(context.source, offset),
      .kind = std::move(kind),
      .confidence = Confidence::Extracted,
  });
}

void add_regex_matches(
    Fragment& fragment,
    const ExtractionContext& context,
    const std::regex& pattern,
    std::string kind) {
  const auto begin = std::cregex_iterator(context.source.data(), context.source.data() + context.source.size(), pattern);
  const auto end = std::cregex_iterator();
  for (auto iter = begin; iter != end; ++iter) {
    const auto match = *iter;
    if (match.size() > 1) {
      add_node(fragment, context, match[1].str(), kind, static_cast<std::size_t>(match.position(1)));
    }
  }
}

}  // namespace

ExtractionResult extract_msbuild(const ExtractionContext& context) {
  ExtractionResult result;
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(<\s*(Project|Target|ItemGroup|PropertyGroup)\b[^>]*>)", std::regex::icase},
      "msbuild_element");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(<\s*Target\b[^>]*\bName\s*=\s*["']([^"']+)["'])", std::regex::icase},
      "target");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(<\s*PackageReference\b[^>]*\bInclude\s*=\s*["']([^"']+)["'])", std::regex::icase},
      "package_reference");
  return result;
}

ExtractionResult extract_delphi_form(const ExtractionContext& context) {
  ExtractionResult result;
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"((?:object|inherited)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:)", std::regex::icase},
      "form_object");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(\bprocedure\s+([A-Za-z_][A-Za-z0-9_.]*)\s*\()", std::regex::icase},
      "procedure");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(\bfunction\s+([A-Za-z_][A-Za-z0-9_.]*)\s*\()", std::regex::icase},
      "function");
  return result;
}

ExtractionResult extract_apex(const ExtractionContext& context) {
  ExtractionResult result;
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(\b(?:public|private|global|protected)?\s*(?:with\s+sharing|without\s+sharing|virtual|abstract)?\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\b)"},
      "class");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(\btrigger\s+([A-Za-z_][A-Za-z0-9_]*)\s+on\s+([A-Za-z_][A-Za-z0-9_]*)\b)"},
      "trigger");
  add_regex_matches(
      result.fragment,
      context,
      std::regex{R"(\b(?:public|private|global|protected|static|virtual|override|\s)+\s+[A-Za-z_][A-Za-z0-9_<>,\[\]]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()"},
      "method");
  return result;
}

ExtractionResult extract_mcp_config(const ExtractionContext& context) {
  ExtractionResult result;
  try {
    const auto json = nlohmann::json::parse(context.source);
    const auto servers = json.find("mcpServers");
    if (servers != json.end() && servers->is_object()) {
      for (const auto& [name, server] : servers->items()) {
        add_node(result.fragment, context, name, "mcp_server");
        if (server.is_object()) {
          if (const auto command = server.find("command"); command != server.end() && command->is_string()) {
            add_node(result.fragment, context, command->get<std::string>(), "mcp_command");
          }
        }
      }
    }
  } catch (const nlohmann::json::exception& error) {
    result.fragment.warnings.push_back(std::string{"failed to parse MCP config JSON: "} + error.what());
  }
  return result;
}

std::optional<ExtractionResult> extract_non_grammar_language(
    DetectedLanguage language,
    const ExtractionContext& context) {
  switch (language) {
    case DetectedLanguage::MsBuild:
    case DetectedLanguage::Xml:
      return extract_msbuild(context);
    case DetectedLanguage::Delphi:
      return extract_delphi_form(context);
    case DetectedLanguage::Apex:
      return extract_apex(context);
    case DetectedLanguage::McpConfig:
      return extract_mcp_config(context);
    default:
      return std::nullopt;
  }
}

}  // namespace cgraph
