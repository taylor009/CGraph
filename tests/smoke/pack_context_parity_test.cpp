// Parity gate for the knapsack packing path in pack_context.
//
// Reproduces the offline harness (research/2510.00446) against a COMMITTED fixture
// pair (tests/fixtures/pack_context_parity/{graph.json,queries.jsonl}): inject each
// row's grade-2 focal seed, run the C++ `context` op with packing=knapsack at k=3,
// and compare mean packed grade-2 recall to the harness numbers.
//
// IMPORTANT: the engine weights the knapsack by char/4 over the capped source slice
// (step-A "model 4" -- the load-bearing fix), so parity is asserted against the
// MODEL-4 harness recall (0.591 / 0.625 / 0.666 at 2k/4k/8k), NOT the tiktoken
// reference (0.541 / 0.569 / 0.614). Model 4 is the cost model the engine runs.
//
// The fixture is a deterministic, code-only graph (no research/ or build/ nodes)
// committed alongside a verbatim eval snapshot, so the gate is reproducible and
// immune to working-tree / daemon drift, and runs on every checkout including CI.
// It does NOT read the mutable cgraph-out/graph.json. The absent-artifact skip
// remains only as a defensive fallback if the fixture is somehow missing.

#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/protocol.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

double centrality_of(const cgraph::Node& node) {
  const auto it = node.properties.find("degree_centrality");
  if (it == node.properties.end()) {
    return 0.0;
  }
  try {
    return std::stod(it->second);
  } catch (const std::exception&) {
    return 0.0;
  }
}

}  // namespace

int main() {
  // Committed fixture pair (deterministic code-only graph + verbatim eval snapshot),
  // NOT the mutable cgraph-out/graph.json -- so the gate is drift-immune and runs in CI.
  const fs::path fixture = CGRAPH_PARITY_FIXTURE_DIR;
  const fs::path graph_path = fixture / "graph.json";
  const fs::path eval_path = fixture / "queries.jsonl";

  if (!fs::exists(graph_path) || !fs::exists(eval_path)) {
    std::cout << "SKIP: parity fixture absent (" << graph_path << " / " << eval_path
              << "). Regenerate per openspec stabilize-parity-gate-fixture.\n";
    return 0;  // Defensive fallback; the fixture is committed, so this should not trigger.
  }

  cgraph::DaemonState state;
  if (!cgraph::load_graph_snapshot(state, graph_path)) {
    std::cerr << "FAIL: could not load " << graph_path << "\n";
    return 1;
  }
  const auto snapshot = cgraph::read_graph_snapshot(state);

  std::unordered_map<std::string, const cgraph::Node*> by_id;
  for (const auto& node : snapshot->nodes) {
    by_id[node.id] = &node;
  }

  // Parse symbol-granularity eval rows; focal = highest-centrality grade-2 symbol
  // present in the graph (ties by id) -- mirrors research/metric.py::resolve_focal.
  struct Row {
    std::string focal;
    std::string query;  // needed for the adaptive gather gate (query-term overlap)
    std::set<std::string> grade2;
  };
  std::vector<Row> rows;
  std::ifstream eval_file(eval_path);
  std::string line;
  while (std::getline(eval_file, line)) {
    if (line.empty()) {
      continue;
    }
    const auto row = nlohmann::json::parse(line, nullptr, false);
    if (row.is_discarded() || row.value("granularity", std::string{"symbol"}) != "symbol") {
      continue;
    }
    std::set<std::string> grade2;
    for (const auto& rel : row.value("relevant", nlohmann::json::array())) {
      if (rel.value("grade", 0) == 2) {
        grade2.insert(rel.value("node_id", std::string{}));
      }
    }
    const cgraph::Node* focal = nullptr;
    for (const auto& id : grade2) {
      const auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      const auto* node = it->second;
      if (focal == nullptr || centrality_of(*node) > centrality_of(*focal) ||
          (centrality_of(*node) == centrality_of(*focal) && node->id > focal->id)) {
        focal = node;
      }
    }
    if (focal != nullptr) {
      rows.push_back({focal->id, row.value("query", std::string{}), std::move(grade2)});
    }
  }

  const auto mean_recall = [&](const std::string& packing, int budget) {
    double sum = 0.0;
    int n = 0;
    for (const auto& row : rows) {
      const nlohmann::json params{
          {"id", row.focal}, {"budget", budget}, {"packing", packing}, {"max_depth", 3}};
      const auto response = cgraph::handle_daemon_request(state, cgraph::make_request("context", params));
      const auto& result = response["result"];
      std::set<std::string> selected;
      if (result.contains("focus") && result["focus"].is_object()) {
        selected.insert(result["focus"].value("id", std::string{}));
      }
      for (const auto& entry : result.value("included", nlohmann::json::array())) {
        selected.insert(entry.value("id", std::string{}));
      }
      std::size_t hit = 0;
      for (const auto& id : row.grade2) {
        if (selected.count(id) != 0) {
          ++hit;
        }
      }
      if (!row.grade2.empty()) {
        sum += static_cast<double>(hit) / static_cast<double>(row.grade2.size());
        ++n;
      }
    }
    return n != 0 ? sum / static_cast<double>(n) : 0.0;
  };

  struct Target {
    int budget;
    double expect;  // model-4 harness recall
    bool gated;     // 8k is neutral by design (packing ~moot once everything fits)
  };
  const std::vector<Target> targets = {{2000, 0.591, true}, {4000, 0.625, true}, {8000, 0.666, false}};
  constexpr double kTol = 0.03;

  std::cout << "pack_context knapsack parity  (N=" << rows.size() << " symbol rows, k=3)\n";
  std::cout << "budget    greedy   knapsack   model4-target   |delta|   gate\n";
  int failures = 0;
  for (const auto& t : targets) {
    const double greedy = mean_recall("greedy", t.budget);
    const double knapsack = mean_recall("knapsack", t.budget);
    const double delta = std::fabs(knapsack - t.expect);
    bool ok = true;
    if (t.gated) {
      ok = delta <= kTol && knapsack + 1e-9 >= greedy;  // parity AND no regression vs greedy
    }
    if (!ok) {
      ++failures;
    }
    std::cout << "  " << t.budget << "    " << greedy << "    " << knapsack << "    " << t.expect
              << "    " << delta << "    " << (t.gated ? (ok ? "PASS" : "FAIL") : "neutral") << "\n";
  }

  // --- Adaptive gather revalidation (in-engine, this graph) -------------------
  // The Python harness (research/recall_lever.py) showed adaptive gather beats the
  // k=2 greedy baseline at a fraction of k=3's candidate cost. That result is
  // EVIDENCE; this block is the in-engine revalidation under the engine's own
  // accounting. Adaptive must (a) beat the true greedy@k=2 baseline materially and
  // (b) stay at/below full k=3 recall while gathering far fewer candidates.
  const auto measure = [&](const std::string& packing, const std::string& gather, int max_depth,
                           int budget) -> std::pair<double, double> {
    double rsum = 0.0;
    double csum = 0.0;
    int n = 0;
    for (const auto& row : rows) {
      nlohmann::json params{{"id", row.focal}, {"budget", budget}, {"packing", packing}, {"max_depth", max_depth}};
      if (gather == "adaptive") {
        params["gather"] = "adaptive";
        params["gather_theta"] = 0.05;
        params["q"] = row.query;  // the gate needs the query terms
      }
      const auto result = cgraph::handle_daemon_request(state, cgraph::make_request("context", params))["result"];
      std::set<std::string> selected;
      if (result.contains("focus") && result["focus"].is_object()) {
        selected.insert(result["focus"].value("id", std::string{}));
      }
      const auto included = result.value("included", nlohmann::json::array());
      for (const auto& entry : included) {
        selected.insert(entry.value("id", std::string{}));
      }
      const double cand = static_cast<double>(included.size()) + result.value("omitted", 0);
      std::size_t hit = 0;
      for (const auto& id : row.grade2) {
        if (selected.count(id) != 0) {
          ++hit;
        }
      }
      if (!row.grade2.empty()) {
        rsum += static_cast<double>(hit) / static_cast<double>(row.grade2.size());
        csum += cand;
        ++n;
      }
    }
    return {n != 0 ? rsum / n : 0.0, n != 0 ? csum / n : 0.0};
  };

  std::cout << "\nadaptive gather revalidation (in-engine, N=" << rows.size() << ")\n";
  std::cout << "budget   greedy@k2   adaptive   knap@k3   d(adp-k2)   cand k2/adp/k3   gate\n";
  for (const int budget : {2000, 4000}) {  // 8k neutral: the ego graph mostly fits
    const auto [r_k2, c_k2] = measure("greedy", "fixed", 2, budget);
    const auto [r_adp, c_adp] = measure("knapsack", "adaptive", 3, budget);
    const auto [r_k3, c_k3] = measure("knapsack", "fixed", 3, budget);
    const double delta = r_adp - r_k2;
    // Material gain over the true baseline, no worse than full k3 recall, and a
    // strictly smaller candidate pool than k3 (the recall/cost win, in-engine).
    const bool ok = delta >= 0.03 && r_adp <= r_k3 + 0.02 && c_adp < c_k3;
    if (!ok) {
      ++failures;
    }
    std::cout << "  " << budget << "   " << r_k2 << "   " << r_adp << "   " << r_k3 << "   " << delta << "   "
              << c_k2 << "/" << c_adp << "/" << c_k3 << "   " << (ok ? "PASS" : "FAIL") << "\n";
  }

  if (failures != 0) {
    std::cerr << failures << " parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "parity OK\n";
  return 0;
}
