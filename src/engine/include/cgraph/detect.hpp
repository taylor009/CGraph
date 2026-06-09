#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cgraph {

enum class DetectedLanguage {
  Unknown,
  Apex,
  C,
  Cpp,
  CSharp,
  Delphi,
  Go,
  Groovy,
  Java,
  JavaScript,
  Kotlin,
  McpConfig,
  MsBuild,
  PhpBlade,
  Python,
  Ruby,
  Scala,
  TypeScript,
  Tsx,
  Xml,
};

struct DetectedFile {
  std::filesystem::path path;
  DetectedLanguage language = DetectedLanguage::Unknown;
};

[[nodiscard]] DetectedLanguage detect_language(const std::filesystem::path& path);
[[nodiscard]] std::vector<DetectedFile> detect_project_files(const std::filesystem::path& root);

}  // namespace cgraph
