#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cgraph {

// Layer A substrate: timings and counters measured at the pipeline and daemon
// orchestration seams. Layer B (the derivation functions at the bottom of this
// header) only interprets these numbers; it never measures anything itself and
// never invents a counterfactual.

using StatsClock = std::chrono::steady_clock;

// RAII scoped timer: writes elapsed milliseconds into *out on destruction.
// Monotonic clock; the wall clock is never read.
class ScopedTimer {
 public:
  explicit ScopedTimer(double* out) : out_(out), start_(StatsClock::now()) {}
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;
  ScopedTimer(ScopedTimer&&) = delete;
  ScopedTimer& operator=(ScopedTimer&&) = delete;
  ~ScopedTimer() {
    if (out_ != nullptr) {
      const std::chrono::duration<double, std::milli> elapsed = StatsClock::now() - start_;
      *out_ = elapsed.count();
    }
  }

 private:
  double* out_;
  StatsClock::time_point start_;
};

// Per-phase timings and counters for one deterministic (re)build. Populated by
// run_one_shot and the daemon rescan.
struct BuildStats {
  double extract_ms = 0.0;
  double merge_ms = 0.0;
  double resolve_ms = 0.0;
  double dedup_ms = 0.0;
  double communities_ms = 0.0;
  double analyze_ms = 0.0;
  std::size_t files_total = 0;
  std::size_t files_extracted = 0;  // re-parsed this build
  std::size_t files_cache_hit = 0;  // reused from a warm index
  std::size_t nodes = 0;
  std::size_t edges = 0;

  [[nodiscard]] double total_ms() const {
    return extract_ms + merge_ms + resolve_ms + dedup_ms + communities_ms + analyze_ms;
  }
};

// The daemon request types, in dispatch order. Count is the array sentinel.
enum class DaemonOp { Query, Path, Explain, Impact, Context, Update, Status, Shutdown, Count };

inline constexpr std::size_t kDaemonOpCount = static_cast<std::size_t>(DaemonOp::Count);

[[nodiscard]] const char* daemon_op_name(DaemonOp op);
[[nodiscard]] std::optional<DaemonOp> daemon_op_from_string(std::string_view name);

// Fixed-capacity ring buffer of the most recent operations. O(1) insert, no
// allocation after construction. Answers "useful right now" alongside the
// lifetime totals, and holds individual latencies so a percentile is derivable
// (the lifetime totals only give a mean).
struct RecentOp {
  DaemonOp op = DaemonOp::Status;
  double latency_ms = 0.0;
  bool zero_hit = false;
};

inline constexpr std::size_t kRecentWindowCapacity = 256;

class RollingWindow {
 public:
  void record(RecentOp sample) {
    if (buffer_.size() < kRecentWindowCapacity) {
      buffer_.push_back(sample);
    } else {
      buffer_[next_] = sample;
    }
    next_ = (next_ + 1) % kRecentWindowCapacity;
  }

  [[nodiscard]] std::size_t size() const { return buffer_.size(); }
  [[nodiscard]] const std::vector<RecentOp>& samples() const { return buffer_; }

 private:
  std::vector<RecentOp> buffer_;
  std::size_t next_ = 0;
};

// Since-boot lifetime totals plus the rolling recent window.
struct DaemonOpStats {
  std::array<std::size_t, kDaemonOpCount> count{};
  std::array<double, kDaemonOpCount> total_ms{};
  std::size_t query_zero_hits = 0;
  RollingWindow recent;

  void record(DaemonOp op, double latency_ms, bool zero_hit) {
    const auto idx = static_cast<std::size_t>(op);
    count[idx] += 1;
    total_ms[idx] += latency_ms;
    if (op == DaemonOp::Query && zero_hit) {
      query_zero_hits += 1;
    }
    recent.record(RecentOp{op, latency_ms, zero_hit});
  }
};

// --- Layer B: pure derivation over the substrate above. No I/O, no clocks. ---

// Modeled cache saving: files_cache_hit x mean(per-file extract time). Returns
// nullopt when no estimate can be honestly formed (no reuse, or no file was
// extracted this build so there is no per-file mean). Never fabricated.
[[nodiscard]] std::optional<double> modeled_cache_saved_ms(std::size_t files_cache_hit,
                                                           std::size_t files_extracted,
                                                           double extract_total_ms);

// Reused / total. 0 when total is 0.
[[nodiscard]] double cache_hit_rate(std::size_t files_cache_hit, std::size_t files_total);

// Linear-interpolated percentile of a latency sample (p in [0,1]). 0 for empty.
[[nodiscard]] double percentile(std::vector<double> values, double p);

// Zero-result queries / total queries. 0 when no queries. ("revealed" stat.)
[[nodiscard]] double query_zero_hit_rate(std::size_t query_count, std::size_t query_zero_hits);

// stats.json body for a one-shot build, including the modeled saving when one
// can be formed.
[[nodiscard]] nlohmann::json build_stats_json(const BuildStats& stats);

// One-line human-readable summary for stderr (file/node/edge counts + total time
// in human units).
[[nodiscard]] std::string build_stats_summary(const BuildStats& stats);

// Daemon op-stats body for the status payload: lifetime totals, recent window
// (count + p50/mean latency), and the query zero-hit rate.
[[nodiscard]] nlohmann::json op_stats_json(const DaemonOpStats& stats);

}  // namespace cgraph
