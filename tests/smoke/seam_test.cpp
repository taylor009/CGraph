#include "cgraph/seam.hpp"

#include "cgraph/fragment_json.hpp"
#include "cgraph/semantic_fragment_validation.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

void write_json(const fs::path& path, const json& value) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << value.dump(2);
}

const cgraph::Node* find_node(const cgraph::Fragment& frag, const std::string& id) {
  for (const auto& node : frag.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

bool has_edge(const cgraph::Fragment& frag, const std::string& src, const std::string& tgt,
              const std::string& rel) {
  for (const auto& edge : frag.edges) {
    if (edge.source == src && edge.target == tgt && edge.relation == rel) {
      return true;
    }
  }
  return false;
}

const cgraph::Node* find_in(const cgraph::GraphSnapshot& graph, const std::string& id) {
  for (const auto& node : graph.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

bool has_snapshot_edge(const cgraph::GraphSnapshot& graph, const std::string& src,
                       const std::string& tgt, const std::string& rel) {
  for (const auto& edge : graph.edges) {
    if (edge.source == src && edge.target == tgt && edge.relation == rel) {
      return true;
    }
  }
  return false;
}

// The canonical spec used by the happy-path and (mutated) error-path tests.
json make_spec() {
  return json{
      {"provider", "ml-api"},
      {"api_version", "v3"},
      {"error_codes", {400, 500}},
      {"services",
       {{{"name", "ml-api"}, {"role", "provider"}, {"owned", false}},
        {{"name", "backend"}, {"role", "consumer"}, {"owned", true}}}},
      {"schemas", {{{"name", "ScoreResult"}, {"canonical", "ml-api/src/schemas/score.ts"}}}},
      {"endpoints",
       {{{"method", "POST"},
         {"path", "/v3/score"},
         {"response_schema", "ScoreResult"},
         {"path_params", {"modelId"}},
         {"query_params", {"verbose"}}}}},
      {"consumes",
       {{{"service", "backend"},
         {"method", "POST"},
         {"path", "/v3/score"},
         {"call_site", {{"graph", "backend"}, {"file", "src/score.ts"}, {"line", 42}}}}}},
      {"mirrors",
       {{{"schema", "ScoreResult"},
         {"graph", "backend"},
         {"file", "src/types.ts"},
         {"line", 10}}}}};
}

}  // namespace

int main() {
  const auto root = fs::temp_directory_path() / "cgraph-seam-test";
  fs::remove_all(root);

  // Backend consumer graph. The call site at score.ts:42 falls inside BOTH a
  // file-spanning function (1-100) and the precise scoreModel function (40-50);
  // resolution must pick the smaller span. The mirror type lives at types.ts:10.
  const auto backend_graph = root / "backend.json";
  write_json(backend_graph,
             json{{"nodes",
                   {{{"id", "backend::module"},
                     {"label", "score module"},
                     {"type", "file"},
                     {"source_file", "backend/src/score.ts"},
                     {"source_location", {{"start_line", 1}, {"end_line", 100}}}},
                    {{"id", "backend::outer"},
                     {"label", "outer"},
                     {"type", "function"},
                     {"source_file", "backend/src/score.ts"},
                     {"source_location", {{"start_line", 1}, {"end_line", 100}}}},
                    {{"id", "backend::scoreModel"},
                     {"label", "scoreModel"},
                     {"type", "function"},
                     {"source_file", "backend/src/score.ts"},
                     {"source_location", {{"start_line", 40}, {"end_line", 50}}}},
                    {{"id", "backend::ScoreResult"},
                     {"label", "ScoreResult"},
                     {"type", "interface"},
                     {"source_file", "backend/src/types.ts"},
                     {"source_location", {{"start_line", 8}, {"end_line", 12}}}}}}});

  std::unordered_map<std::string, fs::path> graphs{{"backend", backend_graph}};

  // ---- happy path ----
  auto res = cgraph::generate_seam(make_spec(), graphs);
  if (!res.ok || !res.errors.empty()) {
    return 1;
  }
  const auto& frag = res.fragment;

  // Contract nodes present with the expected ids/kinds.
  const auto* provider = find_node(frag, "service:ml-api");
  const auto* endpoint = find_node(frag, "endpoint:ml-api:POST /v3/score");
  const auto* schema = find_node(frag, "schema:ml-api:v3:ScoreResult");
  if (provider == nullptr || provider->kind != "service" ||
      find_node(frag, "service:backend") == nullptr || endpoint == nullptr ||
      endpoint->kind != "endpoint" || schema == nullptr || schema->kind != "schema") {
    return 1;
  }

  // Anchor resolution picked the smallest-span node (scoreModel 40-50, not outer 1-100),
  // and the shadow code-ref carries the consumer node's REAL id.
  const auto* call_ref = find_node(frag, "backend::scoreModel");
  const auto* mirror_ref = find_node(frag, "backend::ScoreResult");
  if (call_ref == nullptr || call_ref->kind != "code-ref" || mirror_ref == nullptr ||
      mirror_ref->kind != "code-ref") {
    return 1;
  }
  if (find_node(frag, "backend::outer") != nullptr) {
    return 1;  // the larger-span node must NOT have been chosen
  }

  // The five contract edges.
  if (!has_edge(frag, "endpoint:ml-api:POST /v3/score", "service:ml-api", "SERVED_BY") ||
      !has_edge(frag, "endpoint:ml-api:POST /v3/score", "schema:ml-api:v3:ScoreResult",
                "RESPONDS_WITH") ||
      !has_edge(frag, "service:backend", "endpoint:ml-api:POST /v3/score", "CONSUMES") ||
      !has_edge(frag, "endpoint:ml-api:POST /v3/score", "backend::scoreModel", "CONSUMED_AT") ||
      !has_edge(frag, "schema:ml-api:v3:ScoreResult", "backend::ScoreResult", "MIRRORED_BY")) {
    return 1;
  }

  // The emitted fragment must be ingestable through the existing validation path.
  if (!cgraph::validate_semantic_fragment_json(cgraph::to_json(frag)).valid) {
    return 1;
  }

  // Byte-stable: regenerating from the same spec + graphs yields an identical
  // serialization (emission order is deterministic, not hash-ordered).
  auto res2 = cgraph::generate_seam(make_spec(), graphs);
  if (!res2.ok || cgraph::to_json(res2.fragment).dump() != cgraph::to_json(frag).dump()) {
    return 1;
  }

  // ---- fail-loud error paths: each must return ok=false and an empty fragment ----
  auto expect_fail = [&](json spec, std::unordered_map<std::string, fs::path> g) -> bool {
    auto r = cgraph::generate_seam(spec, g);
    return !r.ok && !r.errors.empty() && r.fragment.nodes.empty() && r.fragment.edges.empty();
  };

  // Unresolved anchor (no node spans line 999).
  json bad_anchor = make_spec();
  bad_anchor["consumes"][0]["call_site"]["line"] = 999;
  if (!expect_fail(bad_anchor, graphs)) {
    return 1;
  }
  // consumes references an endpoint that was never declared.
  json bad_endpoint = make_spec();
  bad_endpoint["consumes"][0]["path"] = "/v3/nope";
  if (!expect_fail(bad_endpoint, graphs)) {
    return 1;
  }
  // mirror references an undeclared schema.
  json bad_schema = make_spec();
  bad_schema["mirrors"][0]["schema"] = "Ghost";
  if (!expect_fail(bad_schema, graphs)) {
    return 1;
  }
  // call_site names a graph that was not supplied.
  if (!expect_fail(make_spec(), {})) {
    return 1;
  }
  // malformed spec: missing a required top-level field.
  json malformed = make_spec();
  malformed.erase("provider");
  if (!expect_fail(malformed, graphs)) {
    return 1;
  }

  // ---- fuse: merge the seam fragment with the real service graph into a view ----
  // The backend service graph carries the REAL nodes the seam's shadows reference.
  cgraph::GraphSnapshot backend;
  backend.nodes.push_back(cgraph::Node{.id = "backend::scoreModel", .label = "scoreModel",
                                       .kind = "function"});
  backend.nodes.push_back(cgraph::Node{.id = "backend::ScoreResult", .label = "ScoreResult",
                                       .kind = "interface"});
  backend.nodes.push_back(cgraph::Node{.id = "backend::helper", .label = "helper",
                                       .kind = "function"});
  backend.edges.push_back(
      cgraph::Edge{.source = "backend::scoreModel", .target = "backend::helper", .relation = "CALLS"});

  auto fused = cgraph::fuse_seam(frag, {{"backend", backend}});
  if (!fused.ok) {
    return 1;
  }
  // Every backend node is tagged with its service community; the call site is the
  // REAL node (kind function), not a dropped code-ref shadow.
  const auto* score = find_in(fused.graph, "backend::scoreModel");
  if (score == nullptr || score->kind != "function" ||
      score->properties.at("community") != "backend") {
    return 1;
  }
  // No code-ref shadow survives the fuse.
  for (const auto& node : fused.graph.nodes) {
    if (node.kind == "code-ref") {
      return 1;
    }
  }
  // Seam contract nodes cluster with their service / provider.
  const auto* svc = find_in(fused.graph, "service:backend");
  const auto* ep = find_in(fused.graph, "endpoint:ml-api:POST /v3/score");
  if (svc == nullptr || svc->properties.at("community") != "backend" || ep == nullptr ||
      ep->properties.at("community") != "ml-api") {
    return 1;
  }
  // The CONSUMED_AT contract edge now binds to the real backend node, and the
  // backend's own CALLS edge survived the merge.
  if (!has_snapshot_edge(fused.graph, "endpoint:ml-api:POST /v3/score", "backend::scoreModel",
                         "CONSUMED_AT") ||
      !has_snapshot_edge(fused.graph, "backend::scoreModel", "backend::helper", "CALLS")) {
    return 1;
  }

  // Fail loud: omitting the backend service graph leaves CONSUMED_AT/MIRRORED_BY
  // edges dangling -> no fused graph.
  auto dangling = cgraph::fuse_seam(frag, {});
  if (dangling.ok || dangling.errors.empty() || !dangling.graph.nodes.empty()) {
    return 1;
  }

  fs::remove_all(root);
  return 0;
}
