#pragma once

#include <tree_sitter/api.h>

namespace cgraph {

class ThreadParserCache {
 public:
  [[nodiscard]] TSParser* parser_for(const TSLanguage* language);
  void clear();
};

[[nodiscard]] ThreadParserCache& thread_parser_cache();

}  // namespace cgraph
