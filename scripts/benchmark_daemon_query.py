#!/usr/bin/env python3
"""Compare resident-daemon warm query latency against Python Graphify.

cgraph keeps the graph resident in a per-project daemon, so repeated queries pay
only an IPC round-trip plus an in-memory search. Graphify has no resident
daemon: every `graphify query` reloads graph.json from disk before answering.
This benchmark measures that architectural difference end to end -- the
user-facing latency of asking the graph a question -- and separately reports
cgraph's one-time daemon build cost (paid once, amortized over the session).

Note: the two query commands are not identical operations. cgraph's daemon
`query` op searches node labels; Graphify's `query` runs a BFS traversal that
answers a question. Both represent "ask the graph about X"; the latency compared
is the command's wall-clock round trip.
"""

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
        "elapsed_seconds": elapsed,
        "returncode": completed.returncode,
        "stdout": completed.stdout[-2000:],
        "stderr": completed.stderr[-2000:],
    }


def summarize(results: list[dict[str, object]]) -> dict[str, object]:
    elapsed = [float(result["elapsed_seconds"]) for result in results]
    return {
        "runs": len(results),
        "min_ms": min(elapsed) * 1000.0,
        "median_ms": statistics.median(elapsed) * 1000.0,
        "mean_ms": statistics.fmean(elapsed) * 1000.0,
        "max_ms": max(elapsed) * 1000.0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--graphd", default="build/default/src/daemon/graphd")
    parser.add_argument("--client", default="build/default/src/client/cgraph-client")
    parser.add_argument("--graphify", default=shutil.which("graphify") or "graphify")
    parser.add_argument("--root", required=True, help="project root the daemon serves")
    parser.add_argument("--graph", default=None, help="graph.json for graphify (default <root>/graphify-out/graph.json)")
    parser.add_argument("--query", default="Button")
    parser.add_argument("--runs", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--out", default="build/default/benchmark-daemon-query.json")
    args = parser.parse_args()

    repo = Path.cwd()
    graphd = (repo / args.graphd).resolve()
    client = (repo / args.client).resolve()
    root = Path(args.root).resolve()
    graph = Path(args.graph).resolve() if args.graph else root / "graphify-out" / "graph.json"
    output_path = (repo / args.out).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    client_query = [str(client), "--root", str(root), "--daemon", str(graphd), "query", json.dumps({"q": args.query})]
    client_status = [str(client), "--root", str(root), "--daemon", str(graphd), "status"]
    client_shutdown = [str(client), "--root", str(root), "--daemon", str(graphd), "shutdown"]
    graphify_query = [args.graphify, "query", args.query, "--graph", str(graph)]

    # Spawn the resident daemon and time how long until it serves (graph build).
    subprocess.run(client_shutdown, cwd=repo, capture_output=True, text=True, check=False)
    time.sleep(0.5)
    daemon = subprocess.Popen(
        [str(graphd), "--root", str(root), "--idle-timeout", "600"],
        cwd=repo, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    build_start = time.perf_counter()
    ready = False
    deadline = build_start + 120.0
    while time.perf_counter() < deadline:
        probe = subprocess.run(client_status, cwd=repo, capture_output=True, text=True, check=False)
        if probe.returncode == 0 and '"ok": true' in probe.stdout:
            ready = True
            break
        time.sleep(0.2)
    daemon_build_seconds = time.perf_counter() - build_start
    if not ready:
        daemon.terminate()
        raise SystemExit("daemon did not become ready within 120s")

    try:
        for _ in range(args.warmup):
            subprocess.run(client_query, cwd=repo, capture_output=True, text=True, check=False)
        native_results = [timed(client_query, repo) for _ in range(args.runs)]
        graphify_results = [timed(graphify_query, repo) for _ in range(args.runs)]
    finally:
        subprocess.run(client_shutdown, cwd=repo, capture_output=True, text=True, check=False)

    native = summarize(native_results)
    graphify = summarize(graphify_results)
    result = {
        "root": str(root),
        "graph": str(graph),
        "query": args.query,
        "daemon_build_seconds": daemon_build_seconds,
        "cgraph_warm_daemon_query": native,
        "graphify_cold_query": graphify,
        "comparison": {
            "cgraph_warm_median_ms": native["median_ms"],
            "graphify_median_ms": graphify["median_ms"],
            "warm_speedup_vs_graphify": graphify["median_ms"] / native["median_ms"] if native["median_ms"] else None,
        },
    }
    output_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))

    if any(int(item["returncode"]) != 0 for item in native_results):
        return 1
    if args.query not in native_results[-1]["stdout"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
