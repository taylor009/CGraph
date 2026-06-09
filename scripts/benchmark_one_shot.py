#!/usr/bin/env python3
"""Compare native one-shot time-to-graph against Python Graphify deterministic update."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def write_fixture(root: Path) -> None:
    src = root / "src"
    src.mkdir(parents=True)
    (src / "main.py").write_text(
        """
import os

class PaymentService:
    def run(self):
        return helper()

def helper():
    return os.getcwd()
""".strip()
        + "\n",
        encoding="utf-8",
    )
    (src / "service.ts").write_text(
        """
export class Worker {
  run() {
    return build();
  }
}

function build() {
  return "ok";
}
""".strip()
        + "\n",
        encoding="utf-8",
    )


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
    parser.add_argument("--native", default="build/default/src/cli/cgraph")
    parser.add_argument("--graphify", default=shutil.which("graphify") or "graphify")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--out", default="build/default/benchmark-one-shot.json")
    args = parser.parse_args()

    if args.runs <= 0:
        raise SystemExit("--runs must be positive")

    repo = Path.cwd()
    native = (repo / args.native).resolve()
    output_path = (repo / args.out).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    native_results: list[dict[str, object]] = []
    graphify_results: list[dict[str, object]] = []
    for run in range(args.runs):
        with tempfile.TemporaryDirectory(prefix=f"cgraph-benchmark-{run}-") as temp:
            temp_root = Path(temp)
            native_root = temp_root / "native"
            graphify_root = temp_root / "graphify"
            write_fixture(native_root)
            write_fixture(graphify_root)

            native_result = timed([str(native), "--root", str(native_root), "--out", str(temp_root / "native-out")], repo)
            native_result["graph_exists"] = (temp_root / "native-out" / "graph.json").exists()
            graphify_result = timed([args.graphify, "update", str(graphify_root), "--no-cluster"], repo)
            graphify_result["graph_exists"] = (graphify_root / "graphify-out" / "graph.json").exists()
            native_results.append(native_result)
            graphify_results.append(graphify_result)

    native_summary = summarize(native_results)
    graphify_summary = summarize(graphify_results)
    native_median = native_summary["median_seconds"]
    graphify_median = graphify_summary["median_seconds"]
    result = {
        "note": "Native one-shot measures deterministic graph availability without semantic enrichment; Graphify reference runs deterministic update with --no-cluster.",
        "native_one_shot": native_summary,
        "graphify_update": graphify_summary,
        "comparison": {
            "native_median_seconds": native_median,
            "graphify_median_seconds": graphify_median,
            "native_speedup_vs_graphify": graphify_median / native_median if native_median else None,
            "native_faster": native_median < graphify_median,
        },
    }

    output_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))

    all_results = native_results + graphify_results
    if any(item["returncode"] != 0 or not item["graph_exists"] for item in all_results):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
