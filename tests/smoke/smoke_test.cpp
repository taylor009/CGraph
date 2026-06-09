#include "cgraph/engine.hpp"

#include <string_view>

int main() {
  const auto info = cgraph::build_info();
  if (info.name != std::string_view{"cgraph-native"}) {
    return 1;
  }
  if (info.version.empty()) {
    return 1;
  }
  return 0;
}
