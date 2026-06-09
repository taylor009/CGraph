#!/usr/bin/env python3
"""Compare native daemon cold query-path latency against Python Graphify query."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import time
from pathlib import Path


def timed(command: list[str], cwd: Path) -> dict[str, object]:
    start = time.perf_counter()
    completed = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    elapsed = time.perf_counter() - start
    return {
        "command": command,
        "elapsed_seconds": elapsed,
        "returncode": completed.returncode,
        "stdout": completed.stdout[-2000:],
        "stderr": completed.stderr[-2000:],
    }


def summarize(results: list[dict[str, object]]) -> dict[str, object]:
    elapsed = [float(result["elapsed_seconds"]) for result in results]
    return {
        "runs": len(results),
        "min_seconds": min(elapsed),
        "median_seconds": statistics.median(elapsed),
        "mean_seconds": statistics.fmean(elapsed),
        "max_seconds": max(elapsed),
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--graphd", default="build/default/src/daemon/graphd")
    parser.add_argument("--graphify", default=shutil.which("graphify") or "graphify")
    parser.add_argument("--graph", default="tests/fixtures/daemon_query/graph.json")
    parser.add_argument("--query", default="Alpha")
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--out", default="build/default/benchmark-daemon-query.json")
    args = parser.parse_args()

    if args.runs <= 0:
        raise SystemExit("--runs must be positive")

    repo = Path.cwd()
    graphd = (repo / args.graphd).resolve()
    graph = (repo / args.graph).resolve()
    output_path = (repo / args.out).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    native_command = [
        str(graphd),
        "--benchmark-query",
        "--graph",
        str(graph),
        "--query",
        args.query,
    ]
    graphify_command = [
        args.graphify,
        "query",
        args.query,
        "--graph",
        str(graph),
    ]

    native_results = [timed(native_command, repo) for _ in range(args.runs)]
    graphify_results = [timed(graphify_command, repo) for _ in range(args.runs)]

    result = {
        "note": "Native command measures cold process startup plus graph load plus daemon request-handler query path; full IPC serving is benchmarked after the daemon server lands.",
        "graph": str(graph),
        "query": args.query,
        "native_daemon_query": summarize(native_results),
        "graphify_query": summarize(graphify_results),
    }
    native_median = result["native_daemon_query"]["median_seconds"]
    graphify_median = result["graphify_query"]["median_seconds"]
    result["comparison"] = {
        "native_median_seconds": native_median,
        "graphify_median_seconds": graphify_median,
        "native_speedup_vs_graphify": graphify_median / native_median if native_median else None,
        "native_faster": native_median < graphify_median,
    }
    output_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))

    all_results = native_results + graphify_results
    if any(int(item["returncode"]) != 0 for item in all_results):
        return 1
    if args.query not in native_results[-1]["stdout"] or args.query not in graphify_results[-1]["stdout"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
