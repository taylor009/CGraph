#include "cgraph/operation_stats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace cgraph {
namespace {

constexpr std::array<const char*, kDaemonOpCount> kOpNames = {
    "query", "path", "explain", "impact", "context", "update", "status", "shutdown",
};

// Human-readable duration: ms under a second, seconds under a minute, otherwise
// whole minutes. "340 ms", "1.2 s", "3 minutes" - never "12m 49s".
[[nodiscard]] std::string human_duration(double ms) {
  if (ms < 1000.0) {
    return std::to_string(static_cast<long long>(std::llround(ms))) + " ms";
  }
  const double seconds = ms / 1000.0;
  if (seconds < 60.0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f s", seconds);
    return buf;
  }
  const long long minutes = std::llround(seconds / 60.0);
  return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
}

// Mean latency over a lifetime total (total_ms / count). 0 when count is 0.
[[nodiscard]] double mean_ms(double total, std::size_t count) {
  return count == 0 ? 0.0 : total / static_cast<double>(count);
}

}  // namespace

const char* daemon_op_name(DaemonOp op) {
  const auto idx = static_cast<std::size_t>(op);
  return idx < kDaemonOpCount ? kOpNames[idx] : "unknown";
}

std::optional<DaemonOp> daemon_op_from_string(std::string_view name) {
  for (std::size_t i = 0; i < kDaemonOpCount; ++i) {
    if (name == kOpNames[i]) {
      return static_cast<DaemonOp>(i);
    }
  }
  return std::nullopt;
}

std::optional<double> modeled_cache_saved_ms(std::size_t files_cache_hit,
                                             std::size_t files_extracted,
                                             double extract_total_ms) {
  // No reuse, or no per-file mean to model from this build -> no honest estimate.
  if (files_cache_hit == 0 || files_extracted == 0 || extract_total_ms <= 0.0) {
    return std::nullopt;
  }
  const double mean = extract_total_ms / static_cast<double>(files_extracted);
  return static_cast<double>(files_cache_hit) * mean;
}

double cache_hit_rate(std::size_t files_cache_hit, std::size_t files_total) {
  return files_total == 0 ? 0.0
                          : static_cast<double>(files_cache_hit) / static_cast<double>(files_total);
}

double percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::ranges::sort(values);
  const double clamped = std::clamp(p, 0.0, 1.0);
  const double rank = clamped * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(rank));
  const auto hi = static_cast<std::size_t>(std::ceil(rank));
  if (lo == hi) {
    return values[lo];
  }
  const double frac = rank - static_cast<double>(lo);
  return values[lo] + (values[hi] - values[lo]) * frac;
}

double query_zero_hit_rate(std::size_t query_count, std::size_t query_zero_hits) {
  return query_count == 0 ? 0.0
                          : static_cast<double>(query_zero_hits) / static_cast<double>(query_count);
}

nlohmann::json build_stats_json(const BuildStats& stats) {
  nlohmann::json phases{
      {"extract_ms", stats.extract_ms},
      {"merge_ms", stats.merge_ms},
      {"resolve_ms", stats.resolve_ms},
      {"dedup_ms", stats.dedup_ms},
      {"communities_ms", stats.communities_ms},
      {"analyze_ms", stats.analyze_ms},
  };
  nlohmann::json body{
      {"phase_ms", std::move(phases)},
      {"total_ms", stats.total_ms()},
      {"files_total", stats.files_total},
      {"files_extracted", stats.files_extracted},
      {"files_cache_hit", stats.files_cache_hit},
      {"cache_hit_rate", cache_hit_rate(stats.files_cache_hit, stats.files_total)},
      {"node_count", stats.nodes},
      {"edge_count", stats.edges},
  };
  // Modeled, labeled, and omitted when it cannot be honestly formed.
  if (const auto saved = modeled_cache_saved_ms(stats.files_cache_hit, stats.files_extracted, stats.extract_ms)) {
    body["cache_saved_ms_estimate"] = *saved;
  }
  return body;
}

std::string build_stats_summary(const BuildStats& stats) {
  return std::to_string(stats.files_total) + " files, " + std::to_string(stats.nodes) + " nodes, " +
         std::to_string(stats.edges) + " edges in " + human_duration(stats.total_ms());
}

nlohmann::json op_stats_json(const DaemonOpStats& stats) {
  // Lifetime totals: count + mean latency per op.
  nlohmann::json lifetime = nlohmann::json::object();
  std::size_t total_ops = 0;
  for (std::size_t i = 0; i < kDaemonOpCount; ++i) {
    if (stats.count[i] == 0) {
      continue;
    }
    total_ops += stats.count[i];
    lifetime[daemon_op_name(static_cast<DaemonOp>(i))] = {
        {"count", stats.count[i]},
        {"mean_ms", mean_ms(stats.total_ms[i], stats.count[i])},
    };
  }

  // Recent window: count + p50 latency over the individual recent samples.
  std::vector<double> recent_latencies;
  recent_latencies.reserve(stats.recent.size());
  for (const auto& sample : stats.recent.samples()) {
    recent_latencies.push_back(sample.latency_ms);
  }

  const std::size_t query_count = stats.count[static_cast<std::size_t>(DaemonOp::Query)];

  return {
      {"total_ops", total_ops},
      {"lifetime", std::move(lifetime)},
      {"recent_window",
       {{"size", stats.recent.size()},
        {"capacity", kRecentWindowCapacity},
        {"p50_latency_ms", percentile(recent_latencies, 0.5)}}},
      {"query_count", query_count},
      {"query_zero_hits", stats.query_zero_hits},
      {"query_zero_hit_rate", query_zero_hit_rate(query_count, stats.query_zero_hits)},
  };
}

}  // namespace cgraph
