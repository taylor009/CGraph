#include "cgraph/file_extraction.hpp"

#include "cgraph/configured_extractors.hpp"

#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace cgraph {
namespace {

constexpr std::uintmax_t kMaxExtractionFileBytes = 8 * 1024 * 1024;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::error_code file_size_error;
  const auto size = std::filesystem::file_size(path, file_size_error);
  if (!file_size_error && size > kMaxExtractionFileBytes) {
    throw std::runtime_error("file too large for extraction");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open file");
  }

  std::string contents;
  if (!file_size_error) {
    contents.reserve(static_cast<std::size_t>(size));
  }

  char buffer[64 * 1024];
  while (input) {
    input.read(buffer, sizeof(buffer));
    const auto count = input.gcount();
    if (count <= 0) {
      break;
    }
    if (contents.size() + static_cast<std::size_t>(count) > kMaxExtractionFileBytes) {
      throw std::runtime_error("file too large for extraction");
    }
    contents.append(buffer, static_cast<std::size_t>(count));
  }

  return contents;
}

}  // namespace

ExtractionResult extract_detected_file(const DetectedFile& file) {
  ExtractionResult empty;
  try {
    const auto source = read_file(file.path);
    auto result = extract_configured_language(
        file.language,
        ExtractionContext{
            .source_file = file.path.generic_string(),
            .source = source,
        });
    if (result.has_value()) {
      return *result;
    }

    empty.fragment.warnings.push_back("no extractor registered for detected language");
    return empty;
  } catch (const std::exception& error) {
    empty.fragment.warnings.push_back(std::string{"failed to extract "} + file.path.generic_string() + ": " + error.what());
  } catch (...) {
    empty.fragment.warnings.push_back(std::string{"failed to extract "} + file.path.generic_string() + ": unknown error");
  }

  return empty;
}

}  // namespace cgraph
