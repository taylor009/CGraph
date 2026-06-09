#pragma once

#include "cgraph/types.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace cgraph {

[[nodiscard]] std::string confidence_to_string(Confidence confidence);
[[nodiscard]] bool parse_confidence(std::string_view value, Confidence& confidence);

[[nodiscard]] nlohmann::json to_json(const SourceLocation& location);
[[nodiscard]] nlohmann::json to_json(const Node& node);
[[nodiscard]] nlohmann::json to_json(const Edge& edge);
[[nodiscard]] nlohmann::json to_json(const Hyperedge& hyperedge);
[[nodiscard]] nlohmann::json to_json(const Fragment& fragment);

[[nodiscard]] bool parse_fragment(const nlohmann::json& value, Fragment& fragment, std::vector<std::string>& errors);

}  // namespace cgraph
