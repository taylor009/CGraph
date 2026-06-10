#include "cgraph/semantic_orchestration.hpp"

#include <nlohmann/json.hpp>

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

  // Second pass: add another doc. The cached doc is skipped; the new doc is a
  // fresh chunk. Crucially, its fragment name must be OFFSET past the already
  // dropped chunk_00.json, so a second pass cannot overwrite the first pass's
  // fragment — the drop directory accumulates and ingest stays additive.
  write_file(root / "docs" / "architecture.md", "# Architecture\nThe daemon serves queries.\n");
  const auto replan = cgraph::plan_enrichment(root, drop);
  if (replan.plan.chunks.size() != 1 || replan.plan.cache_hits != 1) {
    return 1;  // overview cached, architecture pending
  }
  {
    std::ifstream manifest_file(replan.manifest_path, std::ios::binary);
    const auto manifest = nlohmann::json::parse(manifest_file, nullptr, false);
    const auto fragment_name = manifest["chunks"][0].value("fragment", std::string{});
    if (fragment_name != cgraph::fragment_filename_for_chunk(1)) {
      return 1;  // offset applied: chunk_01.json, not a colliding chunk_00.json
    }
  }
  write_file(drop / cgraph::fragment_filename_for_chunk(1), R"({
    "nodes": [
      {"id": "doc:arch", "label": "ArchDoc", "type": "document"},
      {"id": "concept:daemon", "label": "DaemonConcept", "type": "concept"}
    ],
    "edges": [{"source": "doc:arch", "target": "concept:daemon", "relation": "DESCRIBES"}]
  })");

  // Ingest re-applies BOTH fragments: the first pass's nodes are NOT lost.
  const auto ingest2 = cgraph::ingest_enrichment(root, drop);
  if (ingest2.fragments_ingested != 2 || ingest2.fragments_rejected != 0) {
    return 1;
  }
  if (!has_label(ingest2.graph, "Overview") || !has_label(ingest2.graph, "Pipeline") ||
      !has_label(ingest2.graph, "ArchDoc") || !has_label(ingest2.graph, "DaemonConcept")) {
    return 1;  // both passes coexist
  }
  if (ingest2.graph.nodes.size() != ingest2.deterministic_nodes + 4) {
    return 1;
  }

  // A malformed fragment dropped as a new file is rejected and leaves the graph
  // unchanged; the two valid fragments still merge.
  write_file(drop / cgraph::fragment_filename_for_chunk(2), R"({"nodes":[{"id":"no-label"}],"edges":[]})");
  const auto bad = cgraph::ingest_enrichment(root, drop);
  if (bad.fragments_rejected != 1 || bad.fragments_ingested != 2 || bad.errors.empty()) {
    return 1;
  }
  if (bad.graph.nodes.size() != bad.deterministic_nodes + 4) {
    return 1;  // rejected fragment adds nothing; the valid ones persist
  }

  fs::remove_all(root);
  return 0;
}
