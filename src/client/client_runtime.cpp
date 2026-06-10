#include "cgraph/client_runtime.hpp"

#include "cgraph/daemon_endpoint.hpp"
#include "cgraph/daemon_server.hpp"
#include "cgraph/protocol.hpp"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cgraph {
namespace {

std::mutex& lock_map_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, std::weak_ptr<std::mutex>>& lock_map() {
  static std::unordered_map<std::string, std::weak_ptr<std::mutex>> locks;
  return locks;
}

std::shared_ptr<std::mutex> spawn_lock_for(const std::string& root_hash) {
  std::scoped_lock guard(lock_map_mutex());
  auto& weak = lock_map()[root_hash];
  auto lock = weak.lock();
  if (lock == nullptr) {
    lock = std::make_shared<std::mutex>();
    weak = lock;
  }
  return lock;
}

std::chrono::milliseconds backoff_for(const ClientRequest& request, int attempt) {
  const auto multiplier = 1 << std::min(attempt, 10);
  return request.initial_backoff * multiplier;
}

}  // namespace

ClientRuntimeHooks default_client_runtime_hooks(const ClientRequest& request) {
  ClientRuntimeHooks hooks;
  hooks.connect = [](const DaemonIdentity& identity, const nlohmann::json& frame) -> std::optional<nlohmann::json> {
    return request_over_unix_socket(unix_socket_path(identity), frame);
  };
  hooks.spawn = [daemon_path = request.daemon_path](const DaemonIdentity& identity) {
    if (daemon_path.empty()) {
      return false;
    }
#ifdef _WIN32
    (void)identity;
    return false;
#else
    const auto pid = ::fork();
    if (pid < 0) {
      return false;
    }
    if (pid == 0) {
      // Detach the daemon from the client: a new session (no controlling
      // terminal) and stdio pointed at /dev/null, so the resident daemon never
      // holds the client's pipes open (which would make the client's caller
      // hang waiting for EOF) and outlives this short-lived client cleanly.
      ::setsid();
      if (const int devnull = ::open("/dev/null", O_RDWR); devnull >= 0) {
        ::dup2(devnull, STDIN_FILENO);
        ::dup2(devnull, STDOUT_FILENO);
        ::dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) {
          ::close(devnull);
        }
      }
      const auto daemon = daemon_path.string();
      const auto root = identity.project_root.string();
      ::execl(daemon.c_str(), daemon.c_str(), "--root", root.c_str(), nullptr);
      std::_Exit(127);
    }
    return true;
#endif
  };
  hooks.sleep = [](std::chrono::milliseconds delay) {
    std::this_thread::sleep_for(delay);
  };
  return hooks;
}

ClientResult send_thin_client_request(const ClientRequest& request, ClientRuntimeHooks hooks) {
  ClientResult result;
  if (request.operation.empty()) {
    result.error = "missing operation";
    return result;
  }
  if (request.max_connect_attempts <= 0) {
    result.error = "max_connect_attempts must be positive";
    return result;
  }
  if (!hooks.connect) {
    result.error = "missing connect hook";
    return result;
  }
  if (!hooks.spawn) {
    result.error = "missing spawn hook";
    return result;
  }
  if (!hooks.sleep) {
    hooks.sleep = [](std::chrono::milliseconds delay) {
      std::this_thread::sleep_for(delay);
    };
  }

  const auto identity = daemon_identity_for(request.project_root);
  const auto frame = make_request(request.operation, request.params);

  ++result.connect_attempts;
  if (auto response = hooks.connect(identity, frame); response.has_value()) {
    result.response = std::move(response);
    return result;
  }

  const auto spawn_lock = spawn_lock_for(identity.root_hash);
  {
    std::scoped_lock guard(*spawn_lock);
    ++result.connect_attempts;
    if (auto response = hooks.connect(identity, frame); response.has_value()) {
      result.response = std::move(response);
      return result;
    }
    result.spawned = hooks.spawn(identity);
    if (!result.spawned) {
      result.error = "failed to spawn daemon";
      return result;
    }
  }

  for (int attempt = 0; attempt < request.max_connect_attempts; ++attempt) {
    hooks.sleep(backoff_for(request, attempt));
    ++result.connect_attempts;
    if (auto response = hooks.connect(identity, frame); response.has_value()) {
      result.response = std::move(response);
      return result;
    }
  }

  std::ostringstream error;
  error << "daemon did not accept connections after " << result.connect_attempts << " attempts";
  result.error = error.str();
  return result;
}

}  // namespace cgraph
