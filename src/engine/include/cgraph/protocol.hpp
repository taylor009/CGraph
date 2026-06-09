#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cgraph {

constexpr std::uint32_t kProtocolVersion = 1;

[[nodiscard]] std::vector<std::uint8_t> encode_frame(const nlohmann::json& payload);
[[nodiscard]] std::optional<nlohmann::json> decode_frame(const std::vector<std::uint8_t>& frame);
[[nodiscard]] nlohmann::json make_request(std::string op, nlohmann::json params = nlohmann::json::object());
[[nodiscard]] bool protocol_version_matches(const nlohmann::json& message);

}  // namespace cgraph
