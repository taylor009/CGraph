#pragma once

#include "cgraph/detect.hpp"
#include "cgraph/extractor.hpp"

#include <optional>

namespace cgraph {

[[nodiscard]] std::optional<ExtractionResult> extract_non_grammar_language(
    DetectedLanguage language,
    const ExtractionContext& context);

// Whether extract_non_grammar_language handles this language (without running
// an extraction). Kept in sync with its dispatch switch.
[[nodiscard]] bool handles_non_grammar_language(DetectedLanguage language);

[[nodiscard]] ExtractionResult extract_msbuild(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_delphi_form(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_apex(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_mcp_config(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_sql(const ExtractionContext& context);

}  // namespace cgraph
