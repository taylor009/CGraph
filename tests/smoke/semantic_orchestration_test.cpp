#include "cgraph/semantic_orchestration.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, std::string contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

[[nodiscard]] bool has_label(const cgraph::GraphSnapshot& graph, const std::string& label) {
  for (const auto& node : graph.nodes) {
    if (node.label == label) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "cgraph_semantic_orchestration_test";
  fs::remove_all(root);
  fs::create_directories(root);

  // A real little project: one code file (deterministic graph) and one doc (a
  // semantic input a host would enrich).
  write_file(root / "src" / "service.ts", "export function runService() { return 1; }\n");
  write_file(root / "docs" / "overview.md", "# Overview\nThe service runs the pipeline.\n");

  const auto drop = cgraph::default_semantic_drop_dir(root / "out");

  // Plan: the doc is uncached, so it must appear as a chunk; the code file must
  // not (code extraction stays deterministic).
  const auto plan = cgraph::plan_enrichment(root, drop);
  if (plan.plan.chunks.size() != 1 || plan.inputs_to_enrich != 1) {
    return 1;
  }
  bool doc_planned = false;
  for (const auto& input : plan.plan.chunks.front().inputs) {
    doc_planned = doc_planned || input.path.filename() == "overview.md";
  }
  if (!doc_planned || !fs::exists(plan.manifest_path)) {
    return 1;
  }

  // Host authors the fragment for chunk 0 and drops it.
  const auto fragment = drop / cgraph::fragment_filename_for_chunk(plan.plan.chunks.front().index);
  write_file(fragment, R"({
    "nodes": [
      {"id": "doc:overview", "label": "Overview", "type": "document", "source_file": "docs/overview.md"},
      {"id": "concept:pipeline", "label": "Pipeline", "type": "concept", "source_file": "docs/overview.md"}
    ],
    "edges": [
      {"source": "doc:overview", "target": "concept:pipeline", "relation": "DESCRIBES"}
    ]
  })");

  // Ingest: deterministic graph gains the two semantic nodes + edge.
  const auto ingest = cgraph::ingest_enrichment(root, drop);
  if (ingest.fragments_ingested != 1 || ingest.fragments_rejected != 0) {
    return 1;
  }
  if (ingest.graph.nodes.size() != ingest.deterministic_nodes + 2) {
    return 1;
  }
  if (!has_label(ingest.graph, "Overview") || !has_label(ingest.graph, "Pipeline") ||
      !has_label(ingest.graph, "runService")) {
    return 1;
  }

  // Re-plan: the now-cached doc is skipped (cache hit, no chunks).
  const auto replan = cgraph::plan_enrichment(root, drop);
  if (!replan.plan.chunks.empty() || replan.plan.cache_hits != 1) {
    return 1;
  }

  // A malformed fragment is rejected and leaves the graph unchanged.
  write_file(drop / cgraph::fragment_filename_for_chunk(0), R"({"nodes":[{"id":"no-label"}],"edges":[]})");
  const auto bad = cgraph::ingest_enrichment(root, drop);
  if (bad.fragments_rejected != 1 || bad.fragments_ingested != 0 || bad.errors.empty()) {
    return 1;
  }
  if (bad.graph.nodes.size() != bad.deterministic_nodes) {
    return 1;  // rejected fragment must not mutate the graph
  }

  fs::remove_all(root);
  return 0;
}
