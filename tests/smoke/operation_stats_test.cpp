#include "cgraph/operation_stats.hpp"

#include <cmath>

namespace {

bool approx(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

}  // namespace

int main() {
  using namespace cgraph;

  // --- modeled cache saving = files_cache_hit x mean(per-file extract time) ---
  // 8 files extracted in 80ms -> 10ms/file; 5 reused -> 50ms modeled saving.
  {
    const auto saved = modeled_cache_saved_ms(/*hits=*/5, /*extracted=*/8, /*extract_total_ms=*/80.0);
    if (!saved || !approx(*saved, 50.0)) {
      return 1;
    }
  }

  // --- estimate omitted when there is no reuse or no per-file mean ---
  if (modeled_cache_saved_ms(/*hits=*/0, /*extracted=*/8, 80.0).has_value()) {
    return 2;  // no reuse -> no estimate
  }
  if (modeled_cache_saved_ms(/*hits=*/5, /*extracted=*/0, 0.0).has_value()) {
    return 3;  // full cache hit, no mean available -> no estimate (not zero)
  }
  if (modeled_cache_saved_ms(/*hits=*/5, /*extracted=*/8, 0.0).has_value()) {
    return 4;  // zero measured extract time -> no estimate
  }

  // --- cache_hit_rate, including the divide-by-zero guard ---
  if (!approx(cache_hit_rate(3, 12), 0.25)) {
    return 5;
  }
  if (!approx(cache_hit_rate(0, 0), 0.0)) {
    return 6;  // empty tree must not divide by zero
  }

  // --- query zero-hit rate, including zero-query guard ---
  if (!approx(query_zero_hit_rate(10, 2), 0.2)) {
    return 7;
  }
  if (!approx(query_zero_hit_rate(0, 0), 0.0)) {
    return 8;  // no queries -> no divide by zero
  }

  // --- percentile from a known multiset {1,2,3,4,5}: p50 == 3, p0 == 1 ---
  {
    const std::vector<double> latencies{5.0, 1.0, 3.0, 2.0, 4.0};  // unsorted on purpose
    if (!approx(percentile(latencies, 0.5), 3.0)) {
      return 9;
    }
    if (!approx(percentile(latencies, 0.0), 1.0) || !approx(percentile(latencies, 1.0), 5.0)) {
      return 10;
    }
    if (!approx(percentile({}, 0.5), 0.0)) {
      return 11;  // empty sample -> 0, no UB
    }
  }

  // --- rolling window never exceeds capacity, and records lifetime + zero-hits ---
  {
    DaemonOpStats stats;
    for (std::size_t i = 0; i < kRecentWindowCapacity + 50; ++i) {
      stats.record(DaemonOp::Query, /*latency_ms=*/1.0, /*zero_hit=*/(i % 2 == 0));
    }
    if (stats.recent.size() != kRecentWindowCapacity) {
      return 12;  // ring buffer bounded
    }
    const std::size_t total = kRecentWindowCapacity + 50;
    if (stats.count[static_cast<std::size_t>(DaemonOp::Query)] != total) {
      return 13;  // lifetime total counts every op, not just the window
    }
    // Half were zero-hit (even indices: 0..total-1 -> ceil(total/2)).
    const std::size_t expected_zero = (total + 1) / 2;
    if (stats.query_zero_hits != expected_zero) {
      return 14;
    }
  }

  // --- op_stats_json surfaces lifetime totals, recent window, and zero-hit rate ---
  {
    DaemonOpStats stats;
    stats.record(DaemonOp::Query, 10.0, false);
    stats.record(DaemonOp::Query, 30.0, true);
    stats.record(DaemonOp::Status, 1.0, false);
    const auto json = op_stats_json(stats);
    if (json["query_count"] != 2 || json["query_zero_hits"] != 1) {
      return 15;
    }
    if (!approx(json["query_zero_hit_rate"].get<double>(), 0.5)) {
      return 16;
    }
    if (json["total_ops"] != 3) {
      return 17;
    }
    if (json["recent_window"]["size"] != 3 || json["recent_window"]["capacity"] != kRecentWindowCapacity) {
      return 18;
    }
    // Lifetime mean for query = (10+30)/2 = 20ms.
    if (!approx(json["lifetime"]["query"]["mean_ms"].get<double>(), 20.0)) {
      return 19;
    }
  }

  return 0;
}
