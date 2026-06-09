#include "cgraph/parser_pool.hpp"

#include <memory>
#include <unordered_map>

namespace cgraph {
namespace {

struct ParserDeleter {
  void operator()(TSParser* parser) const noexcept {
    ts_parser_delete(parser);
  }
};

using ParserPtr = std::unique_ptr<TSParser, ParserDeleter>;

[[nodiscard]] std::unordered_map<const TSLanguage*, ParserPtr>& parser_map() {
  thread_local std::unordered_map<const TSLanguage*, ParserPtr> parsers;
  return parsers;
}

}  // namespace

TSParser* ThreadParserCache::parser_for(const TSLanguage* language) {
  auto& parsers = parser_map();

  const auto existing = parsers.find(language);
  if (existing != parsers.end()) {
    return existing->second.get();
  }

  ParserPtr parser(ts_parser_new());
  if (parser == nullptr) {
    return nullptr;
  }
  if (!ts_parser_set_language(parser.get(), language)) {
    return nullptr;
  }

  auto* raw = parser.get();
  parsers.emplace(language, std::move(parser));
  return raw;
}

void ThreadParserCache::clear() {
  parser_map().clear();
}

ThreadParserCache& thread_parser_cache() {
  thread_local ThreadParserCache cache;
  return cache;
}

}  // namespace cgraph
