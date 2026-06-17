#include "cgraph/daemon_ops.hpp"

#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/fragment_json.hpp"
#include "cgraph/graph_builder.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/semantic_fragment_validation.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

int main() {
  namespace fs = std::filesystem;
  // A real source file so the explain snippet exercises actual file reads, not a
  // stub. Node "a" points at line 2 (the class), so the snippet must start there.
  const auto src = fs::temp_directory_path() / "cgraph-daemon-ops-test" / "alpha.cpp";
  fs::remove_all(src.parent_path());
  fs::create_directories(src.parent_path());
  std::ofstream(src, std::ios::binary)
      << "// header line\n"
      << "class Alpha {\n"
      << "  void run();\n"
      << "};\n";

  cgraph::DaemonState state;
  state.pid = 123;

  // Before any build publishes, the daemon serves the default Empty snapshot;
  // read ops must say so, or an agent can't tell "no match" from "still building".
  const auto during_build = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "Alpha"}}));
  if (during_build["result"].value("graph_state", std::string{}) != "building") {
    return 1;
  }
  const auto empty_status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (empty_status["result"].value("build_state", std::string{}) != "building") {
    return 1;
  }

  cgraph::GraphSnapshot graph;
  graph.build_state = cgraph::BuildState::DeterministicReady;
  // "a" is the central hub (centrality 1.0, a god node); "AlphaLeaf" also matches
  // the "Alpha" query but is peripheral. Edges c -> a -> b form a chain so the
  // blast radius has two depths to walk.
  graph.nodes.push_back(cgraph::Node{
      .id = "a",
      .label = "Alpha",
      .source_file = src.generic_string(),
      .source_location = cgraph::SourceLocation{.start_line = 2, .start_column = 0, .end_line = 4, .end_column = 1},
      .kind = "class",
      .properties = {{"degree_centrality", "1.000000"}, {"god_node", "true"}}});
  graph.nodes.push_back(cgraph::Node{.id = "b", .label = "Beta", .kind = "function"});
  graph.nodes.push_back(cgraph::Node{
      .id = "c", .label = "AlphaLeaf", .kind = "function", .properties = {{"degree_centrality", "0.100000"}}});
  // "d" carries a full-signature label, the shape real extractors emit.
  graph.nodes.push_back(cgraph::Node{.id = "d", .label = "gamma_run(int x, bool dry)", .kind = "function"});
  graph.edges.push_back(cgraph::Edge{.source = "a", .target = "b", .relation = "CALLS"});
  graph.edges.push_back(cgraph::Edge{.source = "c", .target = "a", .relation = "CALLS"});
  cgraph::publish_graph_snapshot(state, std::move(graph));

  const auto status = cgraph::handle_daemon_request(state, cgraph::make_request("status"));
  if (!status["ok"].get<bool>() || status["result"]["node_count"] != 4 ||
      status["result"].value("build_state", std::string{}) != "ready") {
    return 1;
  }

  // A bare symbol name resolves against the label's leading token, so an agent
  // never needs the full signature ("gamma_run" -> "gamma_run(int x, bool dry)").
  const auto by_symbol = cgraph::handle_daemon_request(state, cgraph::make_request("explain", {{"id", "gamma_run"}}));
  if (by_symbol["result"].value("id", std::string{}) != "d") {
    return 1;
  }

  // Matching is case-insensitive: "alpha" finds "Alpha" and "AlphaLeaf". A ready
  // graph carries no "building" annotation.
  const auto query = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "alpha"}}));
  const auto& query_result = query["result"];
  const auto& query_nodes = query_result["nodes"];
  // Both "Alpha" and "AlphaLeaf" match; total reflects the full match count.
  if (query_nodes.size() != 2 || query_result.value("total", 0U) != 2U || query_result.contains("graph_state")) {
    return 1;
  }
  // Ranked by centrality: the hub "a" must lead, carrying file:line, its
  // centrality, and the god_node flag so the agent can open and prioritize it.
  const auto& hit = query_nodes[0];
  if (hit.value("source_file", std::string{}) != src.generic_string() || hit.value("line", 0U) != 2U ||
      hit.value("centrality", 0.0) < 0.99 || !hit.value("god_node", false)) {
    return 1;
  }
  if (query_nodes[1].value("label", std::string{}) != "AlphaLeaf") {
    return 1;  // peripheral node ranked below the hub
  }

  // kind/file filters narrow the match set.
  const auto by_kind = cgraph::handle_daemon_request(
      state, cgraph::make_request("query", {{"q", "alpha"}, {"kind", "class"}}));
  if (by_kind["result"]["nodes"].size() != 1 ||
      by_kind["result"]["nodes"][0].value("id", std::string{}) != "a") {
    return 1;
  }
  const auto by_file = cgraph::handle_daemon_request(
      state, cgraph::make_request("query", {{"q", "alpha"}, {"file", "alpha.cpp"}}));
  if (by_file["result"]["nodes"].size() != 1 ||
      by_file["result"]["nodes"][0].value("id", std::string{}) != "a") {
    return 1;
  }

  // A near-miss ("Alpa") returns did-you-mean suggestions instead of a bare
  // empty result, so an agent can self-correct.
  const auto miss = cgraph::handle_daemon_request(state, cgraph::make_request("query", {{"q", "Alpa"}}));
  if (miss["result"].value("total", 1U) != 0U || miss["result"]["suggestions"].empty() ||
      miss["result"]["suggestions"][0].value("label", std::string{}) != "Alpha") {
    return 1;
  }

  // Blast radius: what breaks if "b" changes? Its dependents are "a" (depth 1,
  // calls b) then "c" (depth 2, calls a). Ordered by depth.
  const auto impact = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "b"}, {"direction", "dependents"}, {"max_depth", 3}}));
  const auto& impact_nodes = impact["result"]["nodes"];
  if (impact["result"].value("total", 0U) != 2U || impact_nodes.size() != 2) {
    return 1;
  }
  if (impact_nodes[0].value("id", std::string{}) != "a" || impact_nodes[0].value("depth", 0) != 1 ||
      impact_nodes[1].value("id", std::string{}) != "c" || impact_nodes[1].value("depth", 0) != 2) {
    return 1;
  }
  // Depth bound is honored: max_depth 1 reaches only the direct dependent.
  const auto shallow = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "b"}, {"direction", "dependents"}, {"max_depth", 1}}));
  if (shallow["result"]["nodes"].size() != 1 || shallow["result"]["nodes"][0].value("id", std::string{}) != "a") {
    return 1;
  }
  // Opposite direction: what does "c" depend on? a (depth 1) then b (depth 2).
  // The seed is given by label ("AlphaLeaf") and the echo carries the canonical id.
  const auto deps = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "AlphaLeaf"}, {"direction", "dependencies"}, {"max_depth", 3}}));
  const auto& deps_nodes = deps["result"]["nodes"];
  if (deps["result"].value("id", std::string{}) != "c" || deps_nodes.size() != 2 ||
      deps_nodes[0].value("id", std::string{}) != "a" || deps_nodes[1].value("id", std::string{}) != "b") {
    return 1;
  }
  // An unknown seed reports found:false plus suggestions, not a silent empty.
  const auto impact_miss = cgraph::handle_daemon_request(
      state, cgraph::make_request("impact", {{"id", "AlphaLeef"}}));
  if (impact_miss["result"].value("found", true) || impact_miss["result"]["suggestions"].empty()) {
    return 1;
  }

  // Endpoints resolve by label too ("Alpha" -> id "a").
  const auto path = cgraph::handle_daemon_request(state, cgraph::make_request("path", {{"source", "Alpha"}, {"target", "b"}}));
  if (path["result"]["path"].size() != 2) {
    return 1;
  }
  // path_nodes enriches each hop; the first hop is "a" with its label + line.
  const auto& path_nodes = path["result"]["path_nodes"];
  if (path_nodes.size() != 2 || path_nodes[0].value("label", std::string{}) != "Alpha" ||
      path_nodes[0].value("line", 0U) != 2U) {
    return 1;
  }
  // A missing endpoint is flagged with suggestions; an empty path alone is
  // indistinguishable from "no route exists".
  const auto path_miss = cgraph::handle_daemon_request(
      state, cgraph::make_request("path", {{"source", "a"}, {"target", "Betb"}}));
  if (!path_miss["result"]["path"].empty() || path_miss["result"].value("target_found", true) ||
      path_miss["result"]["target_suggestions"].empty() || path_miss["result"].contains("source_found")) {
    return 1;
  }

  const auto explain = cgraph::handle_daemon_request(state, cgraph::make_request("explain", {{"id", "a"}}));
  const auto& explain_result = explain["result"];
  if (explain_result["neighbors"].empty()) {
    return 1;
  }
  // The focal node carries a location block and a snippet read from disk; the
  // snippet must begin at the class declaration (start_line 2), not the header.
  if (explain_result.value("line", 0U) != 2U || !explain_result.contains("location") ||
      explain_result["location"].value("start_line", 0U) != 2U) {
    return 1;
  }
  const auto snippet = explain_result.value("snippet", std::string{});
  if (snippet.rfind("class Alpha {", 0) != 0 || snippet.find("header line") != std::string::npos) {
    return 1;
  }
  // Neighbors are ordered most-central-first: AlphaLeaf (0.1) before Beta (no
  // centrality), and each entry describes the node on the other end with a
  // direction, so the agent can navigate without a second lookup.
  if (explain_result.value("neighbor_count", 0U) != 2U) {
    return 1;
  }
  const auto& neighbor = explain_result["neighbors"][0];
  if (neighbor.value("direction", std::string{}) != "in" ||
      neighbor["node"].value("label", std::string{}) != "AlphaLeaf") {
    return 1;
  }
  // direction filter keeps only the requested side; limit caps and flags.
  const auto outgoing = cgraph::handle_daemon_request(
      state, cgraph::make_request("explain", {{"id", "a"}, {"direction", "out"}}));
  if (outgoing["result"]["neighbors"].size() != 1 ||
      outgoing["result"]["neighbors"][0].value("direction", std::string{}) != "out" ||
      outgoing["result"]["neighbors"][0]["node"].value("label", std::string{}) != "Beta") {
    return 1;
  }
  const auto capped = cgraph::handle_daemon_request(
      state, cgraph::make_request("explain", {{"id", "a"}, {"limit", 1}}));
  if (capped["result"]["neighbors"].size() != 1 || !capped["result"].value("truncated", false) ||
      capped["result"].value("neighbor_count", 0U) != 2U) {
    return 1;
  }
  // Unknown symbol: found:false plus suggestions ("Alpah" ~ "Alpha").
  const auto explain_miss = cgraph::handle_daemon_request(
      state, cgraph::make_request("explain", {{"id", "Alpah"}}));
  if (explain_miss["result"].value("found", true) || explain_miss["result"]["suggestions"].empty() ||
      explain_miss["result"]["suggestions"][0].value("label", std::string{}) != "Alpha") {
    return 1;
  }

  // Context packing: a generous budget bundles the focal node (with its snippet)
  // plus its neighbors, staying within budget.
  const auto context = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"id", "a"}, {"budget", 5000}, {"max_depth", 2}}));
  const auto& ctx = context["result"];
  if (ctx["focus"].value("label", std::string{}) != "Alpha" ||
      ctx["focus"].value("snippet", std::string{}).rfind("class Alpha {", 0) != 0) {
    return 1;  // focal node leads, carrying its source
  }
  // The default (greedy/fixed) response self-describes its mode, and carries no
  // adaptive reach summary.
  if (ctx.value("gather", std::string{}) != "fixed" || ctx.value("packing", std::string{}) != "greedy") {
    return 1;
  }
  if (ctx.contains("reach")) {
    return 1;  // reach is adaptive-only
  }
  if (ctx.value("tokens_used", 0U) == 0U || ctx.value("tokens_used", 0U) > 5000U) {
    return 1;  // packed something, and stayed within budget
  }
  // Both neighbors (b via a->b, c via c->a) are reachable undirected at depth 1.
  if (ctx["included"].size() != 2) {
    return 1;
  }
  for (const auto& item : ctx["included"]) {
    if (item.value("depth", 0) != 1) {
      return 1;
    }
  }

  // A budget too small for any neighbor still returns the focal node and flags
  // truncation rather than dropping everything.
  const auto tight = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"id", "a"}, {"budget", 1}}));
  const auto& tight_ctx = tight["result"];
  if (tight_ctx["focus"].value("label", std::string{}) != "Alpha" ||
      !tight_ctx.value("truncated", false) || !tight_ctx["included"].empty()) {
    return 1;
  }

  // Query-based focal resolution picks the highest-centrality match ("a").
  const auto by_query = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"query", "Alpha"}, {"budget", 5000}}));
  if (by_query["result"]["focus"].value("id", std::string{}) != "a") {
    return 1;
  }
  // An unresolvable focus comes back with suggestions, not just a null focus.
  const auto context_miss = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"id", "Alpah"}}));
  if (!context_miss["result"]["focus"].is_null() || context_miss["result"]["suggestions"].empty()) {
    return 1;
  }

  // Lexical focal fallback: a natural-language query that is NOT a substring of any
  // label still resolves, via query-term overlap. "gamma" overlaps only
  // "gamma_run" ("d"); the old substring-only match would have returned focus:null.
  const auto nl_context = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"q", "compute the gamma value"}, {"budget", 5000}}));
  if (nl_context["result"]["focus"].is_null() ||
      nl_context["result"]["focus"].value("id", std::string{}) != "d") {
    return 1;
  }

  // Off-topic query (shares no lexical term with any label) stays an honest zero
  // hit — null focus, not a misleading bundle forced by the fallback.
  const auto offtopic = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"q", "xylophone zebra quux"}, {"budget", 5000}}));
  if (!offtopic["result"]["focus"].is_null()) {
    return 1;
  }

  // Multi-seed gather: "alpha gamma" is no label's substring, so the lexical
  // fallback seeds from the top matches (a/c via "alpha", d via "gamma"). "d" is
  // disconnected (no edges) and reachable ONLY as its own seed, so its presence in
  // the bundle proves the gather unions several ego graphs, not just the focal's.
  const auto multi = cgraph::handle_daemon_request(
      state, cgraph::make_request("context", {{"q", "alpha gamma"}, {"budget", 50000}}));
  if (multi["result"]["focus"].is_null()) {
    return 1;
  }
  bool multi_found_d = false;
  for (const auto& item : multi["result"]["included"]) {
    if (item.value("id", std::string{}) == "d") {
      multi_found_d = true;
    }
  }
  if (!multi_found_d) {
    return 1;
  }

  fs::remove_all(src.parent_path());

  const auto update = cgraph::handle_daemon_request(state, cgraph::make_request("update"));
  if (!update["result"]["accepted"].get<bool>()) {
    return 1;
  }

  const auto shutdown = cgraph::handle_daemon_request(state, cgraph::make_request("shutdown"));
  if (!shutdown["result"]["shutdown"].get<bool>() || !state.shutdown_requested) {
    return 1;
  }

  auto bad = cgraph::make_request("status");
  bad["protocol_version"] = 999;
  if (cgraph::handle_daemon_request(state, bad)["ok"].get<bool>()) {
    return 1;
  }

  // --- operation stats: counts, latency, zero-hit rate, uptime, window ---
  {
    cgraph::DaemonState s;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{.id = "x", .label = "Widget", .kind = "class"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    for (int i = 0; i < 3; ++i) {
      cgraph::handle_daemon_request(s, cgraph::make_request("query", {{"q", "widget"}}));  // 3 hits
    }
    for (int i = 0; i < 2; ++i) {
      cgraph::handle_daemon_request(s, cgraph::make_request("query", {{"q", "zzznomatch"}}));  // 2 zero-hit
    }

    const auto st = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"];
    const auto& ops = st["ops"];
    if (ops["query_count"] != 5 || ops["query_zero_hits"] != 2) {
      return 30;
    }
    if (std::fabs(ops["query_zero_hit_rate"].get<double>() - 0.4) > 1e-9) {
      return 31;
    }
    if (ops["lifetime"]["query"]["mean_ms"].get<double>() <= 0.0) {
      return 32;  // a served op took measurable time
    }
    if (ops["recent_window"]["size"].get<std::size_t>() > ops["recent_window"]["capacity"].get<std::size_t>()) {
      return 33;
    }
    if (st["uptime_seconds"].get<double>() <= 0.0) {
      return 34;
    }
    // Uptime advances on a later query.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto st2 = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"];
    if (st2["uptime_seconds"].get<double>() <= st["uptime_seconds"].get<double>()) {
      return 35;
    }
  }

  // --- enrichment running is observable in-flight and clears after ---
  {
    cgraph::DaemonState s;
    cgraph::publish_graph_snapshot(s, cgraph::GraphSnapshot{});
    {
      const cgraph::EnrichmentRunningScope running(s, 3);
      const auto in_flight = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"];
      if (in_flight["enrichment_running"] != 3 ||
          in_flight.value("enrichment_state", std::string{}) != "running") {
        return 40;
      }
    }
    const auto after = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"];
    if (after["enrichment_running"] != 0 || after.value("enrichment_state", std::string{}) == "running") {
      return 41;  // both the count and the running state clear
    }
  }

  // --- status reports semantic connectivity ---
  {
    cgraph::DaemonState s;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{.id = "code:fn", .label = "doThing", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "doc:guide", .label = "Guide", .kind = "document"});
    g.edges.push_back(cgraph::Edge{.source = "doc:guide", .target = "code:fn", .relation = "DOCUMENTS"});
    cgraph::publish_graph_snapshot(s, std::move(g));
    const auto sem = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"]["semantic"];
    if (sem["doc_nodes"] != 1 || sem["connected_docs"] != 1 || sem["doc_code_edges"] != 1) {
      return 50;
    }
    if (sem["connectivity_rate"].get<double>() <= 0.0) {
      return 51;
    }

    // A pure-code snapshot reports zero semantic nodes.
    cgraph::DaemonState s2;
    cgraph::GraphSnapshot g2;
    g2.nodes.push_back(cgraph::Node{.id = "code:a", .label = "a", .kind = "function"});
    cgraph::publish_graph_snapshot(s2, std::move(g2));
    const auto sem2 = cgraph::handle_daemon_request(s2, cgraph::make_request("status"))["result"]["semantic"];
    if (sem2["doc_nodes"] != 0 || sem2["concept_nodes"] != 0 ||
        sem2["connectivity_rate"].get<double>() != 0.0) {
      return 52;
    }
  }

  // --- context zero-hit + adaptive usage are observable in status ops ---
  {
    cgraph::DaemonState s;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{.id = "k", .label = "Kettle", .kind = "class"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    // Resolved focus -> not a zero hit.
    cgraph::handle_daemon_request(s, cgraph::make_request("context", {{"id", "k"}, {"budget", 5000}}));
    // Unresolvable focus -> zero hit (the id matched nothing).
    cgraph::handle_daemon_request(s, cgraph::make_request("context", {{"id", "nope"}, {"budget", 5000}}));
    // Adaptive call -> counted distinctly from fixed.
    cgraph::handle_daemon_request(
        s, cgraph::make_request("context", {{"id", "k"}, {"q", "kettle"}, {"gather", "adaptive"}, {"budget", 5000}}));

    const auto ops = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"]["ops"];
    if (ops.value("context_zero_hits", 0) != 1) {
      return 36;  // exactly the unresolved-focus call is a context zero hit
    }
    if (ops.value("context_adaptive_count", 0) != 1) {
      return 37;  // exactly the gather=adaptive call is counted
    }
  }

  // --- adaptive relevance-gated gather: the 3rd hop is taken only from a query-
  //     relevant depth-2 node; the 2-hop core is always preserved ---
  {
    cgraph::DaemonState s;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    // F -> n1 -> {rel "PaymentValidator", irr "LoggingHelper"} -> {d3a, d3b}.
    // Only "rel" matches the query "Payment", so adaptive should reach d3a (via rel)
    // but gate out d3b (via irr).
    g.nodes.push_back(cgraph::Node{.id = "F", .label = "Root", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "n1", .label = "Mid", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "rel", .label = "PaymentValidator", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "irr", .label = "LoggingHelper", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "d3a", .label = "ChargeCard", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "d3b", .label = "FormatString", .kind = "function"});
    g.edges.push_back(cgraph::Edge{.source = "F", .target = "n1", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "n1", .target = "rel", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "n1", .target = "irr", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "rel", .target = "d3a", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "irr", .target = "d3b", .relation = "CALLS"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    const auto ids_of = [](const nlohmann::json& result) {
      std::set<std::string> ids;
      for (const auto& item : result["included"]) {
        ids.insert(item.value("id", std::string{}));
      }
      return ids;
    };

    // Fixed gather at depth 3 (knapsack) reaches BOTH third-hop nodes.
    const auto fixed = cgraph::handle_daemon_request(
        s, cgraph::make_request(
               "context", {{"id", "F"}, {"q", "Payment"}, {"packing", "knapsack"}, {"max_depth", 3}, {"budget", 100000}}));
    const auto fixed_ids = ids_of(fixed["result"]);
    if (fixed_ids.count("d3a") == 0 || fixed_ids.count("d3b") == 0) {
      return 60;  // fixed 3-hop must reach both third-hop nodes
    }
    if (fixed["result"].value("gather", std::string{}) != "fixed") {
      return 61;
    }
    if (fixed["result"].contains("reach")) {
      return 66;  // reach summary is adaptive-only
    }

    // Adaptive keeps the 2-hop core (rel, irr) but expands the 3rd hop ONLY from the
    // query-relevant depth-2 node -> d3a present (via PaymentValidator), d3b absent.
    const auto adaptive = cgraph::handle_daemon_request(
        s, cgraph::make_request(
               "context", {{"id", "F"}, {"q", "Payment"}, {"gather", "adaptive"}, {"gather_theta", 0.5}, {"budget", 100000}}));
    const auto& ares = adaptive["result"];
    const auto adaptive_ids = ids_of(ares);
    if (adaptive_ids.count("rel") == 0 || adaptive_ids.count("irr") == 0) {
      return 62;  // 2-hop core preserved regardless of relevance
    }
    if (adaptive_ids.count("d3a") == 0) {
      return 63;  // relevant third hop reached
    }
    if (adaptive_ids.count("d3b") != 0) {
      return 64;  // irrelevant third hop gated out
    }
    if (ares.value("gather", std::string{}) != "adaptive") {
      return 65;
    }
    // Reach summary: the gate admitted the relevant third hop (d3a, depth 3) and
    // rejected the irrelevant depth-2 frontier node (irr).
    const auto& reach = ares["reach"];
    if (reach.value("expanded_past_core", 0) < 1) {
      return 67;  // relevant third hop was reached
    }
    if (reach.value("gated_at_core", 0) < 1) {
      return 68;  // an irrelevant frontier node was gated
    }
    if (reach.value("candidates", 0) < 1) {
      return 69;
    }

    // A query that matches no depth-2 frontier node collapses adaptive to the
    // 2-hop core: nothing expands past the core, but the core is still gathered.
    const auto none = cgraph::handle_daemon_request(
        s, cgraph::make_request(
               "context",
               {{"id", "F"}, {"q", "zzznomatch"}, {"gather", "adaptive"}, {"gather_theta", 0.5}, {"budget", 100000}}));
    const auto& nres = none["result"];
    if (nres["reach"].value("expanded_past_core", -1) != 0) {
      return 70;  // gate found nothing relevant past 2 hops -> no expansion
    }
    const auto none_ids = ids_of(nres);
    if (none_ids.count("rel") == 0 || none_ids.count("irr") == 0) {
      return 71;  // 2-hop core preserved even when nothing is relevant
    }
    if (none_ids.count("d3a") != 0 || none_ids.count("d3b") != 0) {
      return 72;  // no third hop taken
    }
  }

  // --- session memory: remember writes a sandboxed checkpoint; recall returns
  //     checkpoints newest-first with body + linked code briefs; memory nodes are
  //     inert to code query/context ---
  {
    const auto memdir = fs::temp_directory_path() / "cgraph-memory-test";
    fs::remove_all(memdir);

    cgraph::DaemonState s;
    s.memory_dir = memdir;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{
        .id = "fn:charge", .label = "charge_card", .kind = "function",
        .properties = {{"degree_centrality", "0.500000"}}});
    g.nodes.push_back(cgraph::Node{.id = "fn:log", .label = "log_event", .kind = "function"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    // A daemon without a memory_dir rejects remember rather than writing anywhere.
    {
      cgraph::DaemonState off;
      cgraph::publish_graph_snapshot(off, cgraph::GraphSnapshot{});
      const auto r = cgraph::handle_daemon_request(
          off, cgraph::make_request("remember", {{"title", "x"}, {"body", "y"}}));
      if (r["ok"].get<bool>()) {
        return 90;
      }
    }

    // First checkpoint: one resolvable touch (charge_card) + one unresolved.
    const auto rem = cgraph::handle_daemon_request(
        s, cgraph::make_request("remember", {{"title", "Refactor charge"},
                                             {"body", "did X; next Y"},
                                             {"touches", {"charge_card", "nope_missing"}},
                                             {"tags", {"auth"}}}));
    const auto& rr = rem["result"];
    if (!rem["ok"].get<bool>() || rr.value("written", false) != true ||
        rr.value("id", std::string{}).rfind("memory:checkpoint:", 0) != 0) {
      return 91;
    }
    if (rr.value("concerns", 0) != 1 || rr["unresolved"].size() != 1 ||
        rr["unresolved"][0].get<std::string>() != "nope_missing") {
      return 92;
    }
    // Body file landed inside the sandbox dir; the node points at it.
    const auto body_path = fs::path(rr.value("source_file", std::string{}));
    if (body_path.parent_path() != memdir || !fs::exists(body_path)) {
      return 93;
    }
    // The checkpoint node + a concerns edge to the resolved node ONLY are in the graph.
    {
      const auto snap = cgraph::read_graph_snapshot(s);
      bool has_checkpoint = false;
      bool concerns_resolved = false;
      bool concerns_unresolved = false;
      for (const auto& node : snap->nodes) {
        if (node.id == rr["id"].get<std::string>()) {
          has_checkpoint = node.kind == "checkpoint";
        }
      }
      for (const auto& edge : snap->edges) {
        if (edge.relation == "concerns" && edge.target == "fn:charge") {
          concerns_resolved = true;
        }
        if (edge.relation == "concerns" && edge.target == "nope_missing") {
          concerns_unresolved = true;
        }
      }
      if (!has_checkpoint || !concerns_resolved || concerns_unresolved) {
        return 94;
      }
    }

    // An oversized body is rejected with no node added.
    const auto count_before = cgraph::read_graph_snapshot(s)->nodes.size();
    const auto oversize = cgraph::handle_daemon_request(
        s, cgraph::make_request("remember", {{"title", "big"}, {"body", std::string(20000, 'x')}}));
    if (oversize["ok"].get<bool>() || cgraph::read_graph_snapshot(s)->nodes.size() != count_before) {
      return 95;
    }

    // Second checkpoint (later timestamp) so recall ordering is observable.
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    cgraph::handle_daemon_request(
        s, cgraph::make_request("remember", {{"title", "Second task"}, {"body", "later work"}}));

    // Recall: newest-first, body snippet readable, linked code brief present.
    const auto rec = cgraph::handle_daemon_request(s, cgraph::make_request("recall", {}));
    const auto& cps = rec["result"]["checkpoints"];
    if (cps.size() != 2 || cps[0].value("label", std::string{}) != "Second task" ||
        cps[1].value("label", std::string{}) != "Refactor charge") {
      return 96;  // newest-first
    }
    if (cps[1].value("snippet", std::string{}).find("did X") == std::string::npos) {
      return 97;  // body is snippet-readable through source_file
    }
    if (cps[1]["concerns"].size() != 1 ||
        cps[1]["concerns"][0].value("label", std::string{}) != "charge_card") {
      return 97;  // linked code brief
    }
    // query filter over title narrows recall.
    const auto rec_q = cgraph::handle_daemon_request(s, cgraph::make_request("recall", {{"query", "second"}}));
    if (rec_q["result"]["checkpoints"].size() != 1) {
      return 98;
    }

    // Memory is inert to code retrieval: a checkpoint title is not a code query
    // match, and a checkpoint is never packed into code context.
    const auto code_q = cgraph::handle_daemon_request(s, cgraph::make_request("query", {{"q", "Refactor"}}));
    if (code_q["result"].value("total", 1U) != 0U) {
      return 99;
    }
    const auto ctx = cgraph::handle_daemon_request(
        s, cgraph::make_request("context", {{"id", "charge_card"}, {"budget", 5000}}));
    for (const auto& item : ctx["result"]["included"]) {
      if (cgraph::is_memory_node_id(item.value("id", std::string{}))) {
        return 99;
      }
    }

    fs::remove_all(memdir);
  }

  // --- session-memory overlay: the on-disk sidecar is the durable source of
  //     truth; it survives a snapshot rebuild via re-overlay, is idempotent,
  //     graph.json omits memory, and recall tolerates a dangling concerns edge ---
  {
    const auto memdir = fs::temp_directory_path() / "cgraph-memory-overlay-test";
    fs::remove_all(memdir);

    cgraph::DaemonState s;
    s.memory_dir = memdir;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{.id = "fn:charge", .label = "charge_card", .kind = "function"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    const auto rem = cgraph::handle_daemon_request(
        s, cgraph::make_request("remember",
                                {{"title", "Refund"}, {"body", "wip refund"}, {"touches", {"charge_card"}}}));
    const auto cp_id = rem["result"].value("id", std::string{});
    const auto body_path = fs::path(rem["result"].value("source_file", std::string{}));
    const auto sidecar = fs::path(body_path).replace_extension(".json");

    // Sidecar exists beside the body and parses into a fragment with the
    // checkpoint node + its concerns edge.
    if (!fs::exists(sidecar)) {
      return 100;
    }
    {
      std::ifstream in(sidecar);
      const auto parsed = nlohmann::json::parse(in, nullptr, false);
      cgraph::Fragment frag;
      std::vector<std::string> errs;
      if (parsed.is_discarded() || !cgraph::parse_fragment(parsed, frag, errs)) {
        return 101;
      }
      bool has_node = false;
      bool has_edge = false;
      for (const auto& node : frag.nodes) {
        if (node.id == cp_id) {
          has_node = true;
        }
      }
      for (const auto& edge : frag.edges) {
        if (edge.relation == "concerns" && edge.target == "fn:charge") {
          has_edge = true;
        }
      }
      if (!has_node || !has_edge) {
        return 101;
      }
    }

    // The daemon-persisted graph.json omits memory nodes (sidecar is sole truth).
    const auto gpath = memdir / "graph.json";
    if (!cgraph::persist_graph_snapshot(s, gpath)) {
      return 102;
    }
    {
      std::ifstream in(gpath);
      const auto j = nlohmann::json::parse(in, nullptr, false);
      for (const auto& node : j["nodes"]) {
        if (cgraph::is_memory_node_id(node.value("id", std::string{}))) {
          return 102;
        }
      }
    }

    // Re-overlay from the sidecar = the ingest_all_memory mechanism.
    const auto overlay = [&]() {
      auto validation = cgraph::validate_semantic_fragment_file(sidecar);
      if (!validation.valid) {
        return false;
      }
      cgraph::mutate_graph_snapshot(
          s, [&](cgraph::GraphSnapshot& graph) { cgraph::merge_fragment(graph, validation.fragment); });
      return true;
    };

    // Simulate a rebuild: publish a fresh extraction-only snapshot (no memory).
    {
      cgraph::GraphSnapshot rebuilt;
      rebuilt.build_state = cgraph::BuildState::DeterministicReady;
      rebuilt.nodes.push_back(cgraph::Node{.id = "fn:charge", .label = "charge_card", .kind = "function"});
      cgraph::publish_graph_snapshot(s, std::move(rebuilt));
    }
    if (cgraph::handle_daemon_request(s, cgraph::make_request("recall", {}))["result"].value("total", 1) != 0) {
      return 103;  // the rebuild dropped the checkpoint
    }
    if (!overlay()) {
      return 104;
    }
    const auto rec = cgraph::handle_daemon_request(s, cgraph::make_request("recall", {}))["result"];
    if (rec.value("total", 0) != 1 || rec["checkpoints"][0].value("label", std::string{}) != "Refund" ||
        rec["checkpoints"][0]["concerns"].size() != 1) {
      return 104;  // re-overlay restores the checkpoint and its resolvable link
    }

    // Idempotent: a second overlay duplicates nothing.
    const auto before = cgraph::read_graph_snapshot(s);
    const auto nodes_before = before->nodes.size();
    const auto edges_before = before->edges.size();
    overlay();
    const auto after = cgraph::read_graph_snapshot(s);
    if (after->nodes.size() != nodes_before || after->edges.size() != edges_before) {
      return 105;
    }

    // Dangling concerns: rebuild WITHOUT the code target, re-overlay, recall skips
    // the missing link without erroring.
    {
      cgraph::GraphSnapshot no_target;
      no_target.build_state = cgraph::BuildState::DeterministicReady;  // charge_card gone
      cgraph::publish_graph_snapshot(s, std::move(no_target));
    }
    overlay();
    const auto rec2 = cgraph::handle_daemon_request(s, cgraph::make_request("recall", {}))["result"];
    if (rec2.value("total", 0) != 1 || rec2["checkpoints"][0]["concerns"].size() != 0) {
      return 106;
    }

    fs::remove_all(memdir);
  }

  // --- session-memory observability: status reports the memory inventory, and
  //     recall zero-hits are counted without touching query/context counters ---
  {
    const auto memdir = fs::temp_directory_path() / "cgraph-memory-obs-test";
    fs::remove_all(memdir);
    cgraph::DaemonState s;
    s.memory_dir = memdir;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    g.nodes.push_back(cgraph::Node{.id = "fn:a", .label = "alpha", .kind = "function"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    cgraph::handle_daemon_request(s, cgraph::make_request("remember", {{"title", "one"}, {"body", "b"}}));
    cgraph::handle_daemon_request(s, cgraph::make_request("recall", {}));                  // hit (1 checkpoint)
    cgraph::handle_daemon_request(s, cgraph::make_request("recall", {{"query", "zzz"}}));  // miss

    const auto st = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"];
    const auto& mem = st["memory"];
    if (mem.value("checkpoint_count", 0) != 1 || mem.value("sidecar_count", 0) != 1) {
      return 110;
    }
    if (mem.value("recall_count", 0) != 2 || mem.value("recall_zero_hits", 0) != 1) {
      return 111;
    }
    if (mem.value("last_remember_at", std::string{}).empty() ||
        mem.value("last_recall_at", std::string{}).empty()) {
      return 112;
    }
    // recall zero-hits do not bleed into query/context counters.
    const auto& ops = st["ops"];
    if (ops.value("query_zero_hits", 1) != 0 || ops.value("context_zero_hits", 1) != 0 ||
        ops.value("recall_zero_hits", 0) != 1) {
      return 113;
    }
    // A second checkpoint bumps the inventory.
    cgraph::handle_daemon_request(s, cgraph::make_request("remember", {{"title", "two"}, {"body", "b2"}}));
    const auto mem2 = cgraph::handle_daemon_request(s, cgraph::make_request("status"))["result"]["memory"];
    if (mem2.value("checkpoint_count", 0) != 2) {
      return 114;
    }
    fs::remove_all(memdir);
  }

  // --- typed explain: a relation filter narrows neighbors to one edge type and
  //     composes with direction; an absent relation returns the full set ---
  {
    cgraph::DaemonState s;
    cgraph::GraphSnapshot g;
    g.build_state = cgraph::BuildState::DeterministicReady;
    // Hub "h" is reached by four distinct relation types so a relation filter has
    // something to exclude: an incoming and an outgoing CALLS, an incoming
    // reference, and an incoming import.
    g.nodes.push_back(cgraph::Node{.id = "h", .label = "Hub", .kind = "class"});
    g.nodes.push_back(cgraph::Node{.id = "caller", .label = "Caller", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "callee", .label = "Callee", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "refer", .label = "Referer", .kind = "function"});
    g.nodes.push_back(cgraph::Node{.id = "mod", .label = "Importer", .kind = "module"});
    g.edges.push_back(cgraph::Edge{.source = "caller", .target = "h", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "h", .target = "callee", .relation = "CALLS"});
    g.edges.push_back(cgraph::Edge{.source = "refer", .target = "h", .relation = "references"});
    g.edges.push_back(cgraph::Edge{.source = "mod", .target = "h", .relation = "imports"});
    cgraph::publish_graph_snapshot(s, std::move(g));

    const auto rel_count = [](const nlohmann::json& result, const std::string& want) {
      std::size_t n = 0;
      for (const auto& neighbor : result["neighbors"]) {
        if (neighbor.value("relation", std::string{}) == want) {
          ++n;
        }
      }
      return n;
    };

    // No relation -> every adjacent edge (the parity case): four mixed-relation
    // neighbors, including the lone reference edge.
    const auto all = cgraph::handle_daemon_request(s, cgraph::make_request("explain", {{"id", "h"}}));
    if (all["result"].value("neighbor_count", 0U) != 4U || rel_count(all["result"], "references") != 1) {
      return 80;
    }

    // relation=CALLS -> only the two CALLS edges; the reference and import drop,
    // and no off-relation edge leaks through.
    const auto calls = cgraph::handle_daemon_request(
        s, cgraph::make_request("explain", {{"id", "h"}, {"relation", "CALLS"}}));
    const auto& calls_result = calls["result"];
    if (calls_result["neighbors"].size() != 2 || calls_result.value("neighbor_count", 0U) != 2U) {
      return 81;
    }
    if (rel_count(calls_result, "CALLS") != 2 || rel_count(calls_result, "references") != 0 ||
        rel_count(calls_result, "imports") != 0) {
      return 82;
    }

    // relation + direction compose: incoming CALLS only -> just the caller.
    const auto in_calls = cgraph::handle_daemon_request(
        s, cgraph::make_request("explain", {{"id", "h"}, {"direction", "in"}, {"relation", "CALLS"}}));
    if (in_calls["result"]["neighbors"].size() != 1 ||
        in_calls["result"]["neighbors"][0].value("source", std::string{}) != "caller" ||
        in_calls["result"]["neighbors"][0].value("direction", std::string{}) != "in") {
      return 83;
    }

    // A single non-CALLS relation isolates that edge type.
    const auto refs = cgraph::handle_daemon_request(
        s, cgraph::make_request("explain", {{"id", "h"}, {"relation", "references"}}));
    if (refs["result"]["neighbors"].size() != 1 ||
        refs["result"]["neighbors"][0].value("relation", std::string{}) != "references") {
      return 84;
    }

    // A relation no adjacent edge carries -> a found node with an empty neighbor
    // list, not an error or a not-found result.
    const auto nomatch = cgraph::handle_daemon_request(
        s, cgraph::make_request("explain", {{"id", "h"}, {"relation", "inherits"}}));
    if (nomatch["result"].value("found", true) == false || !nomatch["result"]["neighbors"].empty() ||
        nomatch["result"].value("neighbor_count", 1U) != 0U) {
      return 85;
    }
  }

  return 0;
}
