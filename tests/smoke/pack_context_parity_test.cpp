// Parity gate for the knapsack packing path in pack_context.
//
// Reproduces the offline harness (research/2510.00446) against the SAME graph
// (cgraph-out/graph.json) and eval set (research/eval/queries.jsonl): inject each
// row's grade-2 focal seed, run the C++ `context` op with packing=knapsack at k=3,
// and compare mean packed grade-2 recall to the harness numbers.
//
// IMPORTANT: the engine weights the knapsack by char/4 over the capped source slice
// (step-A "model 4" -- the load-bearing fix), so parity is asserted against the
// MODEL-4 harness recall (0.591 / 0.625 / 0.666 at 2k/4k/8k), NOT the tiktoken
// reference (0.541 / 0.569 / 0.614). Model 4 is the cost model the engine runs.
//
// The graph/eval artifacts are gitignored, so when absent (CI / clean checkout) the
// test SKIPS (exit 0). It is the real gate when run locally where artifacts exist;
// knapsack stays non-default until this passes.

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
  const fs::path root = CGRAPH_REPO_ROOT;
  const fs::path graph_path = root / "cgraph-out" / "graph.json";
  const fs::path eval_path = root / "research" / "eval" / "queries.jsonl";

  if (!fs::exists(graph_path) || !fs::exists(eval_path)) {
    std::cout << "SKIP: parity artifacts absent (" << graph_path << " / " << eval_path
              << "). Run a one-shot build + scripts/bootstrap_eval.py to enable.\n";
    return 0;  // CI-safe; gate runs locally where artifacts exist.
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
      rows.push_back({focal->id, std::move(grade2)});
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

  if (failures != 0) {
    std::cerr << failures << " parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "parity OK\n";
  return 0;
}
