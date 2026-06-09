#pragma once

#include "cgraph/language_config.hpp"

#include <tree_sitter/api.h>

#include <string_view>

namespace cgraph {

struct ExtractionResult {
  Fragment fragment;
  std::vector<RawCall> raw_calls;
  std::vector<RawRelation> raw_relations;
};

[[nodiscard]] ExtractionResult extract_with_config(
    const TSLanguage* language,
    const LanguageConfig& config,
    const ExtractionContext& context);

}  // namespace cgraph
