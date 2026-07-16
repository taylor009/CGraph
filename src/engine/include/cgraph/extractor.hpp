#pragma once

#include "cgraph/language_config.hpp"

#include <tree_sitter/api.h>

#include <string>
#include <string_view>

namespace cgraph {

struct ExtractionResult {
  Fragment fragment;
  std::vector<RawCall> raw_calls;
  std::vector<RawRelation> raw_relations;
  // SHA-256 of the exact source buffer parsed by file_extraction. Direct
  // source-string extractor calls leave this empty because they do not own file
  // freshness; incremental graph builds use it to bind a Merkle leaf to the
  // same bytes that produced the fragment.
  std::string source_sha256;
};

[[nodiscard]] ExtractionResult extract_with_config(
    const TSLanguage* language,
    const LanguageConfig& config,
    const ExtractionContext& context);

}  // namespace cgraph
