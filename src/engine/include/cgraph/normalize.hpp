#pragma once

#include <string>
#include <string_view>

namespace cgraph {

[[nodiscard]] std::string make_id(std::string_view input);

}  // namespace cgraph
