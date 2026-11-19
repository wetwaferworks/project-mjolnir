# tests/fuzz/

- `mj_fuzz.c`: the built-in fuzzer. Deterministic mutation from valid seeds, no outside library, so it runs the same on Windows and Linux and inside ctest (`fuzz_builtin`). Usage: `mj_fuzz [iterations-per-target] [seed]`, or `mj_fuzz --write-seeds DIR` to write seed corpora.
- `mj_fuzz_targets.h`: the targets (ledger, manifest, toy weights, score input, safetensors header, MJDELTA1, JSON run-log lines, labeling-page HTTP requests), shared by both fuzzers.
- `mj_libfuzzer.c`: the libFuzzer entry point, built per target when `-DMJ_LIBFUZZER=ON`.

Build with `-DMJ_SANITIZE=ON` for the strongest check.
