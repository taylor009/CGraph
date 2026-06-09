#include "cgraph/engine.hpp"
#include "cgraph/pipeline.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::filesystem::path root = ".";
  std::filesystem::path output = "graphify-out";

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if ((arg == "--root" || arg == "-r") && index + 1 < argc) {
      root = argv[++index];
      continue;
    }
    if ((arg == "--out" || arg == "-o") && index + 1 < argc) {
      output = argv[++index];
      continue;
    }
    if (arg == "--version") {
      const auto info = cgraph::build_info();
      std::cout << info.name << " " << info.version << '\n';
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: cgraph [--root PATH] [--out PATH]\n";
      return 0;
    }
  }

  const auto result = cgraph::run_one_shot(root);
  cgraph::write_exports(result.graph, output);
  std::cerr << "processed " << result.file_count << " files, wrote exports to " << output << '\n';
  for (const auto& warning : result.warnings) {
    std::cerr << "warning: " << warning << '\n';
  }

  return 0;
}

int version_main() {
  const auto info = cgraph::build_info();
  std::cout << info.name << " " << info.version << '\n';
  return 0;
}
