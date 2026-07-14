#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/extractor.hpp"
#include "cgraph/language_config.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>

namespace cgraph {

[[nodiscard]] const TSLanguage* tree_sitter_language_for(DetectedLanguage language);
[[nodiscard]] std::optional<LanguageConfig> config_for_language(DetectedLanguage language);
[[nodiscard]] std::optional<ExtractionResult> extract_configured_language(
    DetectedLanguage language,
    const ExtractionContext& context);

// Whether any extractor (tree-sitter or non-grammar) is registered for this
// language. A detected file whose language has none falls through to an empty
// fragment; the coverage maps below make that visible instead of silent.
[[nodiscard]] bool has_registered_extractor(DetectedLanguage language);

// Per-language count of detected files that no registered extractor handles
// (language_name -> file count). Empty when coverage is total. Surfaced in the
// daemon `status` payload and the one-shot stats.json.
[[nodiscard]] std::map<std::string, std::size_t> unextracted_counts(std::span<const DetectedFile> files);

}  // namespace cgraph
