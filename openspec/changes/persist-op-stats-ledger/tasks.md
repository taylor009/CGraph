## 1. Ledger schema + line builder (Layer B, pure)

- [x] 1.1 `operation_stats_test.cpp` (red): a `DaemonOpStats` with known counts/latencies plus a
      fixed `(boot_wall, shutdown_wall)` produces the expected ledger line — per-op `count`/
      `total_ms`, `schema_version`, derived `boot`, and a histogram that buckets known latencies
      (0.4/1/1.5/4000 ms → bucket 0/1/1/overflow) with counts summing to each op's `count`.
- [x] 1.2 Pin `LEDGER_SCHEMA_VERSION = 1` and `HIST_BUCKET_UPPER_MS = [1,2,4,8,16,32,64,128,256,512,
      1024]` (12 counts incl. overflow) in `operation_stats.hpp`; add a `latency_bucket(ms)` helper
      and an `op_stats_ledger_line(const DaemonOpStats&, boot_wall, shutdown_wall) -> json` in
      `operation_stats.cpp` (pure; no I/O, no clock — timestamps are inputs).
- [x] 1.3 Implement the gate predicate `has_substantive_ops(const DaemonOpStats&)` (≥1 of
      query/path/explain/impact/context) as a pure function, unit-tested both ways.

## 2. Rollup / aggregation (Layer B, pure)

- [x] 2.1 `operation_stats_test.cpp` (red): N ledger lines → summed counts; weighted mean ==
      `Σtotal_ms / Σcount`; merged-histogram p50 matches a hand-computed value; overall query
      zero-hit rate exact; a line with `shutdown` before `--since` is excluded (boundary pinned);
      lines with differing `schema_version` produce the documented "mixed versions" note, not a bad
      merge.
- [x] 2.2 Implement `aggregate_op_stats_ledger(const std::vector<json>& lines, since) -> json` in
      `operation_stats.cpp`: window filter on `shutdown`, sum counts/total_ms, merge same-version
      histograms, approximate p50/p90 by in-bucket interpolation (labeled approximate), exact
      zero-hit rate. Headline fields ordered counts + zero-hit first, latency second.

## 3. Daemon flush on shutdown

- [x] 3.1 `operation_stats_test.cpp` (red): write a line via the append path to a temp dir, read it
      back, re-aggregate, and assert round-trip equality; assert a stats-only `DaemonOpStats`
      writes no line through the gated append helper.
- [x] 3.2 Add a best-effort `append_op_stats_ledger(path, json_line)` (open `std::ios::app`, single
      write + newline, swallow/log errors, never throw). Reader side parses each line tolerantly
      (`json::parse(line, nullptr, false)`, skip discarded).
- [x] 3.3 In `daemon_server.cpp` teardown (the block at ~564-573 that joins the build thread and
      flushes the dirty graph): capture `shutdown_wall = system_clock::now()` once, derive
      `boot_wall` from `StatsClock::now() - state.start_time`, and — gated on `has_substantive_ops`
      — append `op_stats_ledger_line(state.op_stats, boot_wall, shutdown_wall)` to
      `out_dir / "op-stats-ledger.jsonl"`. Wrap so no failure escapes or blocks; ledger flush must
      not affect the graph persist in the same block.

## 4. `cgraph stats --since` subcommand

- [x] 4.1 Test (red): a fixture ledger file + a `--since` window → the CLI prints the expected
      durable rollup (totals, zero-hit headline) and excludes the out-of-window lifetime. (Drive the
      aggregator directly where a full CLI harness is impractical; record the command used.)
- [x] 4.2 Add `cgraph stats [--root PATH] [--since today|<ISO>|<duration>]` in `src/cli/main.cpp`:
      read `<root>/cgraph-out/op-stats-ledger.jsonl`, parse lines tolerantly, call
      `aggregate_op_stats_ledger`, print the DURABLE section (headline = per-op query counts +
      zero-hit rate; latency percentiles secondary, labeled approximate).
- [x] 4.3 When a live daemon for the root is reachable, also fetch its `status` op-stats and print a
      separate, clearly-labeled LIVE (since-boot) section above the DURABLE section.

## 5. Verification

- [x] 5.1 Register any new test target in `tests/smoke/CMakeLists.txt`; run
      `ctest --preset default` and report pass/fail counts.
- [x] 5.2 Confirm the live `status` payload and `graph.json`/`stats.json` outputs are unchanged
      (additive-only): diff a `status` payload and a build's `graph.json` against pre-change output
      for the same input.
- [x] 5.3 Manual end-to-end note (recorded, not unit-tested): run a daemon, issue a few queries,
      trigger `shutdown`, confirm one ledger line appears, then `cgraph stats --since today` shows
      it. Record the commands and output.
