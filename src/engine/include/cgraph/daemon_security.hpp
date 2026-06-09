#pragma once

#include <cstdint>

namespace cgraph {

struct PeerCredentials {
  bool available = false;
  std::uint64_t user_id = 0;
};

[[nodiscard]] std::uint64_t current_user_id();
[[nodiscard]] bool peer_is_authorized(PeerCredentials peer, std::uint64_t server_user_id = current_user_id());

}  // namespace cgraph
