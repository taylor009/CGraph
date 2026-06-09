# Experience Gate Benchmarks

Date: 2026-06-09

Host: macOS arm64, Debug build from `cmake --build --preset default`

## Commands

```sh
python3 scripts/benchmark_daemon_query.py --runs 7 --out build/default/benchmark-daemon-query.json
python3 scripts/benchmark_one_shot.py --runs 5 --out build/default/benchmark-one-shot.json
```

## Results

| Gate | Native median | Python Graphify median | Native speedup |
| --- | ---: | ---: | ---: |
| Cold query path | 0.011948583 s | 0.245448792 s | 20.54x |
| Time to first deterministic graph | 0.018988708 s | 0.372523166 s | 19.62x |

## Notes

- Cold query compares `graphd --benchmark-query` against `graphify query` on `tests/fixtures/daemon_query/graph.json`.
- Time to first deterministic graph compares native `cgraph --root --out` against `graphify update --no-cluster` on the same generated Python/TypeScript fixture.
- The generated JSON result files are written under `build/default/` and are not source artifacts.
- Both native runs and both Python Graphify reference runs returned success and produced the expected graph/query outputs.
