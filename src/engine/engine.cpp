#include "cgraph/engine.hpp"

namespace cgraph {

BuildInfo build_info() noexcept {
  return BuildInfo{
      .name = "cgraph-native",
      .version = "0.1.0",
  };
}

}  // namespace cgraph
