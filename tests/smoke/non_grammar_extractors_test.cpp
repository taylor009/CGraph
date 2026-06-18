#include "cgraph/non_grammar_extractors.hpp"

int main() {
  const auto msbuild = cgraph::extract_msbuild(cgraph::ExtractionContext{
      .source_file = "app.csproj",
      .source = R"xml(<Project><Target Name="Build"/><PackageReference Include="Newtonsoft.Json"/></Project>)xml",
  });
  if (msbuild.fragment.nodes.size() < 3) {
    return 1;
  }

  const auto delphi = cgraph::extract_delphi_form(cgraph::ExtractionContext{
      .source_file = "Form1.dfm",
      .source = "object Form1: TForm1\nend\nprocedure TForm1.Save();",
  });
  if (delphi.fragment.nodes.size() < 2) {
    return 1;
  }

  const auto apex = cgraph::extract_apex(cgraph::ExtractionContext{
      .source_file = "AccountTrigger.trigger",
      .source = "trigger AccountTrigger on Account (before insert) {}\npublic class Worker { public void run() {} }",
  });
  if (apex.fragment.nodes.size() < 2) {
    return 1;
  }

  const auto mcp = cgraph::extract_mcp_config(cgraph::ExtractionContext{
      .source_file = "mcp.json",
      .source = R"json({"mcpServers":{"filesystem":{"command":"npx"}}})json",
  });
  if (mcp.fragment.nodes.size() != 2 || !mcp.fragment.warnings.empty()) {
    return 1;
  }

  const auto bad_mcp = cgraph::extract_mcp_config(cgraph::ExtractionContext{
      .source_file = "mcp.json",
      .source = "{",
  });
  if (bad_mcp.fragment.warnings.empty()) {
    return 1;
  }

  // A .sql file produces exactly one file-level node (kind sql_file), no symbols,
  // no edges -- the contents are not parsed.
  const auto sql = cgraph::extract_sql(cgraph::ExtractionContext{
      .source_file = "prisma/migrations/20201214_baseline/migration.sql",
      .source = "CREATE TYPE \"Role\" AS ENUM ('USER','ADMIN');\nCREATE TABLE \"Thread\" (id TEXT);",
  });
  if (sql.fragment.nodes.size() != 1 || !sql.fragment.edges.empty()) {
    return 1;
  }
  if (sql.fragment.nodes[0].kind != "sql_file" ||
      sql.fragment.nodes[0].label != "migration.sql" ||
      sql.fragment.nodes[0].source_file != "prisma/migrations/20201214_baseline/migration.sql") {
    return 1;
  }

  return 0;
}
