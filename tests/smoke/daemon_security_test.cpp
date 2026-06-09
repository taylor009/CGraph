#include "cgraph/daemon_security.hpp"

#include <cstdint>

int main() {
  const auto current = cgraph::current_user_id();
  if (!cgraph::peer_is_authorized(cgraph::PeerCredentials{.available = true, .user_id = current}, current)) {
    return 1;
  }

  if (cgraph::peer_is_authorized(cgraph::PeerCredentials{.available = true, .user_id = current + 1U}, current)) {
    return 1;
  }

  if (!cgraph::peer_is_authorized(cgraph::PeerCredentials{.available = false, .user_id = 0U}, current)) {
    return 1;
  }

  return 0;
}
