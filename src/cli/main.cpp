#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_identity.hpp"
#include "cgraph/daemon_lifecycle.hpp"
#include "cgraph/daemon_ops.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/engine.hpp"
#include "cgraph/export_json.hpp"
#include "cgraph/fragment_json.hpp"
#include "cgraph/operation_stats.hpp"
#include "cgraph/pipeline.hpp"
#include "cgraph/protocol.hpp"
#include "cgraph/seam.hpp"
#include "cgraph/semantic_orchestration.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void print_usage() {
  std::cout <<
      "usage:\n"
      "  cgraph [--root PATH] [--out PATH]                build the graph and write exports\n"
      "  cgraph enrich-plan   [--root PATH] [--out PATH] [--drop DIR]\n"
      "        emit a semantic chunk plan + manifest for hosts to enrich\n"
      "  cgraph enrich-ingest [--root PATH] [--out PATH] [--drop DIR]\n"
      "        merge host-dropped chunk_NN.json fragments and re-export\n"
      "  cgraph stats [--root PATH] [--since today|<ISO8601>|<N>h|<N>d]\n"
      "        roll up the durable op-stats ledger (counts + zero-hit rate) and show live daemon stats\n"
      "  cgraph seam gen --seam SPEC.json --graphs NAME=graph.json [--graphs ...] --out DROPDIR\n"
      "        resolve a cross-service seam spec against consumer graphs into a contract fragment\n"
      "  cgraph seam fuse --seam SEAM.json --graph NAME=graph.json [--graph ...] --out DIR\n"
      "        merge a seam fragment + service graphs into a clustered graph.json + graph.html view\n"
      "  cgraph seam query --graph FUSED.json <query|path|explain|impact|context> [PARAMS_JSON]\n"
      "        run a read op against a fused seam graph (cross-service); read-only\n";
}

struct Args {
  std::filesystem::path root = ".";
  std::filesystem::path output = "cgraph-out";
  std::filesystem::path drop;  // empty -> default_semantic_drop_dir(output)
  std::string since = "today";  // `stats` window: today | <ISO8601> | <N>h | <N>d
};

// Parses shared flags starting at `start`. Returns false on a malformed flag.
[[nodiscard]] bool parse_args(int argc, char** argv, int start, Args& args) {
  for (int index = start; index < argc; ++index) {
    const std::string arg = argv[index];
    if ((arg == "--root" || arg == "-r") && index + 1 < argc) {
      args.root = argv[++index];
    } else if ((arg == "--out" || arg == "-o") && index + 1 < argc) {
      args.output = argv[++index];
    } else if (arg == "--drop" && index + 1 < argc) {
      args.drop = argv[++index];
    } else if (arg == "--since" && index + 1 < argc) {
      args.since = argv[++index];
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return false;
    }
  }
  if (args.drop.empty()) {
    args.drop = cgraph::default_semantic_drop_dir(args.output);
  }
  return true;
}

int run_build(const Args& args) {
  const auto result = cgraph::run_one_shot(args.root);
  cgraph::write_exports(result.graph, args.output);

  // Sidecar stats.json (durable, diffable) deliberately kept out of graph.json
  // so the Graphify node-link parity golden stays byte-identical.
  std::filesystem::create_directories(args.output);
  std::ofstream stats_out(args.output / "stats.json", std::ios::binary);
  stats_out << cgraph::build_stats_json(result.stats).dump(2);

  std::cerr << "processed " << result.file_count << " files, wrote exports to " << args.output << '\n';
  std::cerr << "build: " << cgraph::build_stats_summary(result.stats) << '\n';
  for (const auto& warning : result.warnings) {
    std::cerr << "warning: " << warning << '\n';
  }
  return 0;
}

int run_enrich_plan(const Args& args) {
  const auto plan = cgraph::plan_enrichment(args.root, args.drop);
  std::cerr << "semantic plan: " << plan.plan.chunks.size() << " chunk(s), "
            << plan.inputs_to_enrich << " input(s) to enrich, " << plan.plan.cache_hits
            << " cache hit(s), " << plan.plan.stale_inputs << " stale\n";
  std::cerr << "stat cache: " << plan.plan.files_hashed << " file(s) hashed, "
            << plan.plan.files_stat_reused << " reused (unchanged)\n";
  std::cerr << "drop computed fragments into: " << plan.drop_dir << '\n';
  std::cerr << "manifest: " << plan.manifest_path << '\n';
  return 0;
}

int run_enrich_ingest(const Args& args) {
  const auto ingest = cgraph::ingest_enrichment(args.root, args.drop);
  cgraph::write_exports(ingest.graph, args.output);
  std::cerr << "enrichment: " << ingest.fragments_ingested << " fragment(s) merged, "
            << ingest.fragments_rejected << " rejected\n";
  std::cerr << "nodes: " << ingest.deterministic_nodes << " deterministic -> "
            << ingest.graph.nodes.size() << " after enrichment\n";
  std::cerr << "wrote exports to " << args.output << '\n';
  for (const auto& error : ingest.errors) {
    std::cerr << "rejected: " << error << '\n';
  }
  return 0;
}

// --since lower bound: today (start of current UTC day) | <ISO8601> | <N>h | <N>d.
[[nodiscard]] std::optional<cgraph::WallClock::time_point> parse_since(const std::string& spec) {
  if (spec == "today") {
    const std::time_t now = cgraph::WallClock::to_time_t(cgraph::WallClock::now());
    std::tm tm{};
    ::gmtime_r(&now, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return cgraph::WallClock::from_time_t(::timegm(&tm));
  }
  if (spec.size() > 1 && (spec.back() == 'h' || spec.back() == 'd')) {
    try {
      const long n = std::stol(spec.substr(0, spec.size() - 1));
      const auto hours = std::chrono::hours(spec.back() == 'd' ? n * 24 : n);
      return cgraph::WallClock::now() - hours;
    } catch (...) {
      return std::nullopt;
    }
  }
  return cgraph::parse_iso8601_utc(spec);
}

int run_stats(const Args& args) {
  const auto since = parse_since(args.since);
  if (!since) {
    std::cerr << "stats: bad --since '" << args.since << "' (use: today | <ISO8601> | <N>h | <N>d)\n";
    return 2;
  }

  // Read the per-service ledger tolerantly: a torn trailing line (crash mid-append)
  // is skipped, every well-formed line is kept.
  const auto ledger_path = args.root / "cgraph-out" / "op-stats-ledger.jsonl";
  std::vector<nlohmann::json> lines;
  {
    std::ifstream in(ledger_path);
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) {
        continue;
      }
      auto parsed = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
      if (!parsed.is_discarded()) {
        lines.push_back(std::move(parsed));
      }
    }
  }
  const auto roll = cgraph::aggregate_op_stats_ledger(lines, *since);

  const auto pct = [](double rate) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f%%", rate * 100.0);
    return std::string(buf);
  };
  const auto ms = [](double v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    return std::string(buf);
  };

  // DURABLE: headline is per-op query counts + zero-hit rate; latency secondary.
  std::cout << "DURABLE (ledger: " << ledger_path.string() << ", since " << roll.value("since", args.since)
            << ")  " << roll.value("lifetimes", 0) << " lifetime(s)"
            << (roll.value("mixed_schema_versions", false) ? "  [mixed schema versions: latency merged within v"
                                                                 + std::to_string(cgraph::kLedgerSchemaVersion) + " only]"
                                                           : "")
            << '\n';
  const auto pad = [](std::string name) {
    name.resize(8, ' ');
    return name;
  };
  const auto& q = roll["query"];
  std::cout << "  " << pad("query") << "count " << q.value("count", 0) << "   zero-hit "
            << pct(q.value("zero_hit_rate", 0.0)) << "   p50 ~" << ms(roll["ops"]["query"].value("p50_ms_approx", 0.0))
            << "ms   p90 ~" << ms(roll["ops"]["query"].value("p90_ms_approx", 0.0)) << "ms\n";
  for (const char* op : {"context", "explain", "impact", "path"}) {
    const auto& o = roll["ops"][op];
    if (o.value("count", 0) == 0) {
      continue;
    }
    std::cout << "  " << pad(op) << "count " << o.value("count", 0) << "   p50 ~" << ms(o.value("p50_ms_approx", 0.0))
              << "ms   p90 ~" << ms(o.value("p90_ms_approx", 0.0)) << "ms\n";
  }
  std::cout << "  (counts and mean are exact; p50/p90 are histogram approximations)\n";

  // LIVE (best-effort): if a daemon for this root is up, show its since-boot stats.
  const auto socket_path = cgraph::unix_socket_path(cgraph::daemon_identity_for(args.root));
  const auto live = cgraph::request_over_unix_socket(socket_path, cgraph::make_request("status", nlohmann::json::object()));
  if (live && live->value("ok", false) && live->contains("result")) {
    const auto& ops = (*live)["result"].value("ops", nlohmann::json::object());
    std::cout << "LIVE (this daemon, since boot)\n";
    std::cout << "  query    count " << ops.value("query_count", 0) << "   zero-hit "
              << pct(ops.value("query_zero_hit_rate", 0.0)) << "   recent p50 "
              << ms(ops["recent_window"].value("p50_latency_ms", 0.0)) << "ms\n";
  } else {
    std::cout << "LIVE: no running daemon for this root (durable view only)\n";
  }
  return 0;
}

// cgraph seam gen --seam SPEC --graphs NAME=path [--graphs ...] --out DROPDIR
int run_seam_gen(int argc, char** argv) {
  std::filesystem::path spec_path;
  std::filesystem::path out_dir;
  std::unordered_map<std::string, std::filesystem::path> graphs;
  for (int index = 3; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--seam" && index + 1 < argc) {
      spec_path = argv[++index];
    } else if (arg == "--out" && index + 1 < argc) {
      out_dir = argv[++index];
    } else if (arg == "--graphs" && index + 1 < argc) {
      const std::string pair = argv[++index];
      const auto eq = pair.find('=');
      if (eq == std::string::npos) {
        std::cerr << "seam gen: --graphs expects NAME=path, got '" << pair << "'\n";
        return 2;
      }
      graphs[pair.substr(0, eq)] = pair.substr(eq + 1);
    } else {
      std::cerr << "seam gen: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }
  if (spec_path.empty() || out_dir.empty()) {
    std::cerr << "seam gen: --seam and --out are required\n";
    return 2;
  }

  std::ifstream spec_input(spec_path);
  if (!spec_input) {
    std::cerr << "seam gen: cannot open spec: " << spec_path << '\n';
    return 1;
  }
  nlohmann::json spec;
  try {
    spec_input >> spec;
  } catch (const nlohmann::json::exception& ex) {
    std::cerr << "seam gen: spec is malformed JSON: " << ex.what() << '\n';
    return 1;
  }

  const auto result = cgraph::generate_seam(spec, graphs);
  if (!result.ok) {
    for (const auto& error : result.errors) {
      std::cerr << "seam gen: ERROR: " << error << '\n';
    }
    return 1;
  }
  for (const auto& line : result.resolution_log) {
    std::cerr << "  " << line << '\n';
  }

  std::filesystem::create_directories(out_dir);
  const auto out_file = out_dir / "chunk_00.json";
  std::ofstream(out_file) << cgraph::to_json(result.fragment).dump(2) << '\n';
  std::cerr << "seam gen: wrote " << out_file << " (" << result.fragment.nodes.size()
            << " nodes, " << result.fragment.edges.size() << " edges)\n";
  return 0;
}

// cgraph seam fuse --seam SEAM --graph NAME=path [--graph ...] --out DIR
int run_seam_fuse(int argc, char** argv) {
  std::filesystem::path seam_path;
  std::filesystem::path out_dir;
  std::vector<std::pair<std::string, std::filesystem::path>> graph_specs;
  for (int index = 3; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--seam" && index + 1 < argc) {
      seam_path = argv[++index];
    } else if (arg == "--out" && index + 1 < argc) {
      out_dir = argv[++index];
    } else if (arg == "--graph" && index + 1 < argc) {
      const std::string pair = argv[++index];
      const auto eq = pair.find('=');
      if (eq == std::string::npos) {
        std::cerr << "seam fuse: --graph expects NAME=path, got '" << pair << "'\n";
        return 2;
      }
      graph_specs.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
    } else {
      std::cerr << "seam fuse: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }
  if (seam_path.empty() || out_dir.empty()) {
    std::cerr << "seam fuse: --seam and --out are required\n";
    return 2;
  }

  std::ifstream seam_input(seam_path);
  if (!seam_input) {
    std::cerr << "seam fuse: cannot open seam: " << seam_path << '\n';
    return 1;
  }
  nlohmann::json seam_json;
  try {
    seam_input >> seam_json;
  } catch (const nlohmann::json::exception& ex) {
    std::cerr << "seam fuse: seam is malformed JSON: " << ex.what() << '\n';
    return 1;
  }
  cgraph::Fragment seam;
  std::vector<std::string> parse_errors;
  if (!cgraph::parse_fragment(seam_json, seam, parse_errors)) {
    for (const auto& error : parse_errors) {
      std::cerr << "seam fuse: " << error << '\n';
    }
    return 1;
  }

  std::vector<std::pair<std::string, cgraph::GraphSnapshot>> services;
  for (const auto& [name, path] : graph_specs) {
    std::ifstream graph_input(path);
    if (!graph_input) {
      std::cerr << "seam fuse: cannot open graph '" << name << "': " << path << '\n';
      return 1;
    }
    nlohmann::json graph_json;
    try {
      graph_input >> graph_json;
    } catch (const nlohmann::json::exception& ex) {
      std::cerr << "seam fuse: graph '" << name << "' is malformed JSON: " << ex.what() << '\n';
      return 1;
    }
    services.emplace_back(name, cgraph::parse_node_link_graph(graph_json));
  }

  const auto fused = cgraph::fuse_seam(seam, services);
  if (!fused.ok) {
    for (const auto& error : fused.errors) {
      std::cerr << "seam fuse: ERROR: " << error << '\n';
    }
    return 1;
  }

  std::filesystem::create_directories(out_dir);
  std::ofstream(out_dir / "graph.json") << cgraph::to_node_link_json(fused.graph).dump(2) << '\n';
  std::ofstream(out_dir / "graph.html") << cgraph::export_graph_html(fused.graph);
  // Marker: tells graphd to serve this dir as a static read-only seam graph.
  std::ofstream(out_dir / cgraph::kSeamMarkerFile) << "cgraph seam fuse output\n";
  std::cerr << "seam fuse: wrote " << (out_dir / "graph.html") << " (" << fused.graph.nodes.size()
            << " nodes, " << fused.graph.edges.size() << " edges)\n";
  return 0;
}

// cgraph seam query --graph FUSED.json <op> [PARAMS_JSON]
int run_seam_query(int argc, char** argv) {
  std::filesystem::path graph_path;
  std::vector<std::string> positionals;
  for (int index = 3; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--graph" && index + 1 < argc) {
      graph_path = argv[++index];
    } else {
      positionals.push_back(arg);
    }
  }
  if (graph_path.empty() || positionals.empty()) {
    std::cerr << "seam query: --graph and an op are required\n";
    return 2;
  }
  const std::string& op = positionals.front();
  // A seam is a read-only derived view: only the read ops are permitted.
  if (op != "query" && op != "path" && op != "explain" && op != "impact" && op != "context") {
    std::cerr << "seam query: op must be one of query|path|explain|impact|context (got '" << op
              << "'); a seam graph is read-only\n";
    return 2;
  }

  cgraph::DaemonState state;
  if (!cgraph::load_graph_snapshot(state, graph_path)) {
    std::cerr << "seam query: failed to load graph: " << graph_path << '\n';
    return 1;
  }
  nlohmann::json params = nlohmann::json::object();
  if (positionals.size() > 1) {
    try {
      params = nlohmann::json::parse(positionals[1]);
    } catch (const nlohmann::json::exception& ex) {
      std::cerr << "seam query: invalid params JSON: " << ex.what() << '\n';
      return 2;
    }
  }
  const auto response = cgraph::handle_daemon_request(state, cgraph::make_request(op, params));
  std::cout << response.dump(2) << '\n';
  return response.value("ok", false) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string first = argv[1];
    if (first == "--version") {
      const auto info = cgraph::build_info();
      std::cout << info.name << " " << info.version << '\n';
      return 0;
    }
    if (first == "--help" || first == "-h") {
      print_usage();
      return 0;
    }
    if (first == "enrich-plan" || first == "enrich-ingest" || first == "stats") {
      Args args;
      if (!parse_args(argc, argv, 2, args)) {
        return 2;
      }
      if (first == "enrich-plan") {
        return run_enrich_plan(args);
      }
      if (first == "enrich-ingest") {
        return run_enrich_ingest(args);
      }
      return run_stats(args);
    }
    if (first == "seam") {
      const std::string sub = argc >= 3 ? argv[2] : "";
      if (sub == "gen") {
        return run_seam_gen(argc, argv);
      }
      if (sub == "fuse") {
        return run_seam_fuse(argc, argv);
      }
      if (sub == "query") {
        return run_seam_query(argc, argv);
      }
      std::cerr << "usage: cgraph seam <gen|fuse|query> ...\n";
      return 2;
    }
  }

  Args args;
  if (!parse_args(argc, argv, 1, args)) {
    return 2;
  }
  return run_build(args);
}

int version_main() {
  const auto info = cgraph::build_info();
  std::cout << info.name << " " << info.version << '\n';
  return 0;
}
