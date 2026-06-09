#pragma once

#include "cgraph/extractor.hpp"

namespace cgraph {

[[nodiscard]] LanguageConfig javascript_language_config();
[[nodiscard]] LanguageConfig typescript_language_config();
[[nodiscard]] LanguageConfig tsx_language_config();

[[nodiscard]] ExtractionResult extract_javascript(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_typescript(const ExtractionContext& context);
[[nodiscard]] ExtractionResult extract_tsx(const ExtractionContext& context);

}  // namespace cgraph
