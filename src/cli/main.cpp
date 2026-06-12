#include "cgraph/engine.hpp"
#include "cgraph/operation_stats.hpp"
#include "cgraph/pipeline.hpp"
#include "cgraph/semantic_orchestration.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void print_usage() {
  std::cout <<
      "usage:\n"
      "  cgraph [--root PATH] [--out PATH]                build the graph and write exports\n"
      "  cgraph enrich-plan   [--root PATH] [--out PATH] [--drop DIR]\n"
      "        emit a semantic chunk plan + manifest for hosts to enrich\n"
      "  cgraph enrich-ingest [--root PATH] [--out PATH] [--drop DIR]\n"
      "        merge host-dropped chunk_NN.json fragments and re-export\n";
}

struct Args {
  std::filesystem::path root = ".";
  std::filesystem::path output = "cgraph-out";
  std::filesystem::path drop;  // empty -> default_semantic_drop_dir(output)
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
    if (first == "enrich-plan" || first == "enrich-ingest") {
      Args args;
      if (!parse_args(argc, argv, 2, args)) {
        return 2;
      }
      return first == "enrich-plan" ? run_enrich_plan(args) : run_enrich_ingest(args);
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
