#include "cgraph/parser_pool.hpp"

#include <thread>

extern "C" const TSLanguage* tree_sitter_c();

int main() {
  auto& cache = cgraph::thread_parser_cache();
  auto* first = cache.parser_for(tree_sitter_c());
  auto* second = cache.parser_for(tree_sitter_c());
  if (first == nullptr || second == nullptr || first != second) {
    return 1;
  }

  TSParser* other_thread_parser = nullptr;
  std::thread worker([&other_thread_parser]() {
    other_thread_parser = cgraph::thread_parser_cache().parser_for(tree_sitter_c());
  });
  worker.join();

  if (other_thread_parser == nullptr || other_thread_parser == first) {
    return 1;
  }

  return 0;
}
