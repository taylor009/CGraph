#pragma once

#include "cgraph/extractor.hpp"

namespace cgraph {

[[nodiscard]] LanguageConfig python_language_config();
[[nodiscard]] ExtractionResult extract_python(const ExtractionContext& context);

}  // namespace cgraph
