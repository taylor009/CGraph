#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/extractor.hpp"
#include "cgraph/language_config.hpp"

#include <optional>

namespace cgraph {

[[nodiscard]] const TSLanguage* tree_sitter_language_for(DetectedLanguage language);
[[nodiscard]] std::optional<LanguageConfig> config_for_language(DetectedLanguage language);
[[nodiscard]] std::optional<ExtractionResult> extract_configured_language(
    DetectedLanguage language,
    const ExtractionContext& context);

}  // namespace cgraph
