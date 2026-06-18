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

  // A .sql migration yields: the file-level node, name-keyed table/enum nodes, and
  // a foreign-key `references` edge between the owning and referenced tables.
  const auto find = [](const cgraph::Fragment& f, const std::string& kind,
                       const std::string& label) -> const cgraph::Node* {
    for (const auto& node : f.nodes) {
      if (node.kind == kind && node.label == label) {
        return &node;
      }
    }
    return nullptr;
  };
  const auto sql = cgraph::extract_sql(cgraph::ExtractionContext{
      .source_file = "prisma/migrations/20201214_baseline/migration.sql",
      .source = "CREATE TYPE \"Role\" AS ENUM ('USER','ADMIN');\n"
                "CREATE TABLE \"Brand\" (id TEXT);\n"
                "CREATE TABLE \"Project\" (id TEXT);\n"
                "ALTER TABLE \"Brand\" ADD FOREIGN KEY(\"projectId\")REFERENCES \"Project\"(\"id\");",
  });
  const auto* file_node = find(sql.fragment, "sql_file", "migration.sql");
  const auto* brand = find(sql.fragment, "sql_table", "Brand");
  const auto* project = find(sql.fragment, "sql_table", "Project");
  const auto* role = find(sql.fragment, "sql_enum", "Role");
  if (file_node == nullptr || brand == nullptr || project == nullptr || role == nullptr) {
    return 1;
  }
  // Exactly one references edge, Brand -> Project, with endpoints matching the table node ids.
  if (sql.fragment.edges.size() != 1 || sql.fragment.edges[0].relation != "references" ||
      sql.fragment.edges[0].source != brand->id || sql.fragment.edges[0].target != project->id) {
    return 1;
  }

  // Name-keyed merge intent: the same table CREATEd in a DIFFERENT file has the same
  // id (path-independent), so the graph builder collapses them to one node.
  const auto sql2 = cgraph::extract_sql(cgraph::ExtractionContext{
      .source_file = "prisma/migrations/20210101_other/migration.sql",
      .source = "CREATE TABLE \"Brand\" (id TEXT);",
  });
  const auto* brand2 = find(sql2.fragment, "sql_table", "Brand");
  if (brand2 == nullptr || brand2->id != brand->id) {
    return 1;
  }

  return 0;
}
