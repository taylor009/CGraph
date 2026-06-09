#include "cgraph/client_runtime.hpp"

#include "cgraph/protocol.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

namespace {

std::optional<nlohmann::json> ok_response(const char* source) {
  return nlohmann::json{{"ok", true}, {"source", source}};
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  {
    int connect_calls = 0;
    int spawn_calls = 0;
    cgraph::ClientRuntimeHooks hooks;
    hooks.connect = [&](const cgraph::DaemonIdentity&, const nlohmann::json&) {
      ++connect_calls;
      return ok_response("existing");
    };
    hooks.spawn = [&](const cgraph::DaemonIdentity&) {
      ++spawn_calls;
      return true;
    };

    cgraph::ClientRequest request{
        .project_root = std::filesystem::current_path(),
        .operation = "status",
        .max_connect_attempts = 3,
        .initial_backoff = 1ms,
    };
    const auto result = cgraph::send_thin_client_request(request, hooks);
    if (!result.response || (*result.response)["source"] != "existing" || connect_calls != 1 || spawn_calls != 0) {
      return 1;
    }
  }

  {
    int connect_calls = 0;
    int spawn_calls = 0;
    std::vector<std::chrono::milliseconds> sleeps;
    cgraph::ClientRuntimeHooks hooks;
    hooks.connect = [&](const cgraph::DaemonIdentity&, const nlohmann::json&) -> std::optional<nlohmann::json> {
      ++connect_calls;
      if (connect_calls >= 4) {
        return ok_response("spawned");
      }
      return std::nullopt;
    };
    hooks.spawn = [&](const cgraph::DaemonIdentity&) {
      ++spawn_calls;
      return true;
    };
    hooks.sleep = [&](std::chrono::milliseconds delay) {
      sleeps.push_back(delay);
    };

    cgraph::ClientRequest request{
        .project_root = std::filesystem::current_path(),
        .operation = "query",
        .params = nlohmann::json{{"q", "Alpha"}},
        .max_connect_attempts = 4,
        .initial_backoff = 5ms,
    };
    const auto result = cgraph::send_thin_client_request(request, hooks);
    if (!result.response || (*result.response)["source"] != "spawned" || spawn_calls != 1) {
      return 1;
    }
    if (sleeps.size() != 2 || sleeps[0] != 5ms || sleeps[1] != 10ms) {
      return 1;
    }
  }

  {
    std::atomic<int> spawn_calls{0};
    std::atomic<bool> spawned{false};
    cgraph::ClientRuntimeHooks hooks;
    hooks.connect = [&](const cgraph::DaemonIdentity&, const nlohmann::json&) -> std::optional<nlohmann::json> {
      if (spawned.load()) {
        return ok_response("shared");
      }
      return std::nullopt;
    };
    hooks.spawn = [&](const cgraph::DaemonIdentity&) {
      ++spawn_calls;
      std::this_thread::sleep_for(10ms);
      spawned.store(true);
      return true;
    };
    hooks.sleep = [](std::chrono::milliseconds) {};

    cgraph::ClientRequest request{
        .project_root = std::filesystem::current_path(),
        .operation = "status",
        .max_connect_attempts = 4,
        .initial_backoff = 1ms,
    };

    std::vector<std::thread> threads;
    std::atomic<int> successes{0};
    for (int i = 0; i < 4; ++i) {
      threads.emplace_back([&] {
        const auto result = cgraph::send_thin_client_request(request, hooks);
        if (result.response && (*result.response)["source"] == "shared") {
          ++successes;
        }
      });
    }
    for (auto& thread : threads) {
      thread.join();
    }

    if (spawn_calls.load() != 1 || successes.load() != 4) {
      return 1;
    }
  }

  return 0;
}
