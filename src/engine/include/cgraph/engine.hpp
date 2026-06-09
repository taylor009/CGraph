#pragma once

#include <string_view>

namespace cgraph {

struct BuildInfo {
  std::string_view name;
  std::string_view version;
};

[[nodiscard]] BuildInfo build_info() noexcept;

}  // namespace cgraph
