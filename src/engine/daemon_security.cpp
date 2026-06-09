#include "cgraph/daemon_security.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace cgraph {

std::uint64_t current_user_id() {
#ifdef _WIN32
  return 0;
#else
  return static_cast<std::uint64_t>(::getuid());
#endif
}

bool peer_is_authorized(PeerCredentials peer, std::uint64_t server_user_id) {
  if (!peer.available) {
    return true;
  }
  return peer.user_id == server_user_id;
}

}  // namespace cgraph
