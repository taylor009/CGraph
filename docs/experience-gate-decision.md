# Experience Gate Decision

Date: 2026-06-09

Decision: proceed to long-tail language, exporter, and host integration fan-out.

## Basis

- Parity and compatibility tests pass in the default suite, including extractor goldens, graph parity, NetworkX node-link loader compatibility, and CLI one-shot output.
- Daemon and client tests pass, including endpoint identity, protocol handling, lifecycle behavior, hardening, and benchmark-query CLI behavior.
- Incremental and semantic ingest tests pass, including edit/rename/delete/touch/overflow coverage, semantic cache behavior, fragment validation, ingest, and status states.
- CI now runs default and sanitizer matrices, plus a Linux Clang fuzz-smoke job for the libFuzzer targets.
- Benchmarks in `docs/experience-gate-benchmarks.md` show native median performance ahead of Python Graphify for both gates:
  - Cold query path: 20.54x faster.
  - Time to first deterministic graph: 19.62x faster.

## Caveat

Local macOS verification with Apple Command Line Tools Clang cannot link libFuzzer because that toolchain does not ship `libclang_rt.fuzzer_osx.a`. The CMake fuzz preset detects this during configure and reports the required upstream LLVM/Clang runtime. The CI fuzz-smoke job is configured for Linux Clang.

## Next Scope

Proceed with fan-out in separate changes, keeping the same gates in place:

- Add long-tail deterministic language extractors only with golden fixtures and graph parity coverage.
- Add exporter variants only with loader or snapshot compatibility tests.
- Add host integrations only through the host-agnostic contract and daemon/MCP surfaces.
