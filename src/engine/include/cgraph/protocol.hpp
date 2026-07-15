#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cgraph {

constexpr std::uint32_t kProtocolVersion = 1;

// Hard cap on a single length-prefixed frame's body, shared by both the send
// (encode) and receive (read_frame) paths. A raw uint32 length read off the
// socket can claim up to ~4 GB; capping the declared length before allocating
// stops a hostile or corrupt header from forcing a giant allocation. 64 MiB is
// far above any real graph query/response and well below a DoS-sized claim.
constexpr std::uint32_t kMaxFrameBodyBytes = 64U * 1024U * 1024U;

[[nodiscard]] std::vector<std::uint8_t> encode_frame(const nlohmann::json& payload);
[[nodiscard]] std::optional<nlohmann::json> decode_frame(const std::vector<std::uint8_t>& frame);
[[nodiscard]] nlohmann::json make_request(std::string op, nlohmann::json params = nlohmann::json::object());
[[nodiscard]] bool protocol_version_matches(const nlohmann::json& message);

}  // namespace cgraph
