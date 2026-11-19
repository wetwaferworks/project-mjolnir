# Build and test

## Build options

| Option | Default | Effect |
|---|---|---|
| `MJ_CUDA` | `OFF` | Build the GPU paths for applying real-weight edits, for the low-rank edit's big products, and for the hemisphere holdout scorer (`src/mj_apply_cuda.cu`, `src/mj_lowrank_cuda.cu` with cuBLAS, `src/mj_hemi_cuda.cu`; CUDA toolkit needed; `CMAKE_CUDA_ARCHITECTURES` defaults to 86). Without it the CPU path runs, with the same bytes. |
| `MJ_SANITIZE` | `OFF` | AddressSanitizer everywhere, plus UndefinedBehaviorSanitizer on GCC and Clang. With MSVC, the ASan runtime DLL is copied beside the test programs. Use a Debug (or RelWithDebInfo) tree on Windows. |
| `MJ_LIBFUZZER` | `OFF` | Also build libFuzzer targets (`mj_libfuzz_ledger`, `_manifest`, `_toy`, `_score`, `_safet`, `_delta`, `_json`) when the compiler supports `-fsanitize=fuzzer`. |
| `MJ_FUZZ_ITERATIONS` | `100000` | Inputs per target for the built-in fuzz test. |
| `MJ_PYTHON_TESTS` | `ON` | Add the Python tests to ctest. |
| `MJ_PYTHON` | (found) | The interpreter for the Python tests. Point it at one with `torch` and `numpy` to run the GPU and real-weight tests. |

On Windows the usual generator is `Visual Studio 17 2022` with `-A x64`. On Linux, set `-DCMAKE_BUILD_TYPE=Release` (or `Debug` for a sanitizer tree). The C library is C17, compiled as the static library `mjolnir`.

A typical Windows Release tree with CUDA:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMJ_CUDA=ON
cmake --build build --config Release
ctest --test-dir build -C Release
```

A typical sanitizer tree (26 tests, Python tests off):

```
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DMJ_SANITIZE=ON -DMJ_PYTHON_TESTS=OFF
cmake --build build-asan --config Debug
ctest --test-dir build-asan -C Debug
```

## The test bench (34 ctest cases, or 26 without Python)

| Test | What it checks |
|---|---|
| `test_hash` | SHA-256 and CRC-32 against known vectors. |
| `test_store` | Content addressing, no overwrite ever, atomic writes and read-back, rollback re-checks, retention, ceilings, refusal of model files. |
| `test_ledger` | The hash chain: tamper, deletion, reordering and torn writes are caught at the exact line, and appends are refused until the chain is whole. |
| `test_ledger_writer` | One writer at a time: the token file, a live writer refused, a stale token, torn-tail repair, and `--expect`. |
| `test_score` | Partial credit for mazes (per segment) and circuits (per element), in basis points. |
| `test_update` | The toy update rule improves without forgetting, rejects and rolls back, and holds the step cap. |
| `test_dream` | The nightly driver and the nap entry point end to end, dry run only. |
| `test_safet` | The safetensors header reader: offsets inside the file, sizes matching shapes, no overlaps, no name twice, strict JSON. |
| `test_delta` | MJDELTA1 pack and unpack, each refusal, and the scale written as Python's `repr()` writes it. |
| `test_lowrank` | The low-rank edit against textbook formulas on small sizes, the truncation error, refusals, and cuBLAS against the CPU. |
| `test_replay` | The JSON reader against Python's `json` rules (refusals, `==`, the dump format), replays from run logs (labels, scores, every refusal, the folder scan), and the grades ledger. |
| `test_labeler` | The labeling page without a socket: the order of steps, grade, skip and back, unknown labels, and the HTTP answers (host check, request marker, size limits, bad requests). |
| `test_toytrain` | The toy nap: features, deterministic training, the four gates, dry run against integrate, refusals, ledger fields. |
| `test_workcopy` | The real-weight path in C on a small synthetic model: register, apply, roll back byte for byte, the dirty marker, refusals, and the CPU and GPU apply giving the same bytes. |
| `test_hemi` | The classroom and the hemisphere swap on toy policies, the MJHEMI2 sets, the real-model gate, and the journal. |
| `fuzz_builtin` | The built-in deterministic fuzzer over the ledger, manifest, toy, score, safetensors-header, delta and JSON parsers and the labeling page's HTTP requests. |
| `tool_clean`, `tool_nap`, `tool_nightly`, `tool_ledger_verify` | The tools end to end on a scratch store. |
| `tool_refuses_live`, `tool_refuses_no_dry_run` | The tools refuse `--live` and refuse to run without `--dry-run` (expected to fail, so they pass when refused). |
| `tool_hemi_clean`, `tool_hemi` | The `mj_hemi pipeline` tool end to end on a scratch folder, then the journal check. |
| `tool_hemi_gate_no_scores`, `tool_hemi_gate_refuses` | The real-model gate keeps a sound set and refuses a mismatched or corrupt one, without taking scores on the command line. |
| `py_ledger`, `py_store`, `py_replay`, `py_labeler`, `py_toytrain`, `py_real`, `py_c_port`, `py_c_port_m1` | The Python modules, including the C tools reading Python-written stores and ledgers; `py_c_port` compares the C port against Python (delta bytes, store, state and ledger records, the edited weights, the low-rank edit); `py_c_port_m1` compares replays, grades, the labeling page and the toy nap. `py_toytrain` and `py_real` need `torch` and `numpy` and are skipped without them. The real-weight tests use small synthetic models, never a real download. |

That is 15 C unit tests, the built-in fuzzer, 10 tool tests, and 8 Python modules: 34 in all. Turning Python tests off, or building the sanitizer tree the same way, leaves 26.

Test scratch files go in `build/test-tmp/` (set by `MJ_TEST_TMP`).

## Running the Python tests directly

```
cd python
python -m unittest -v tests.test_ledger tests.test_store tests.test_replay tests.test_labeler tests.test_toytrain tests.test_real tests.test_c_port tests.test_c_port_m1
```

Set `MJ_TOOLS_DIR` to the folder holding the built C tools to include the cross-checks against C; without it those checks are skipped.

## The fuzzer

```
mj_fuzz [iterations-per-target] [seed]
mj_fuzz --write-seeds DIR
```

The built-in fuzzer mutates valid seeds deterministically and needs no outside library, so it runs the same on Windows and Linux and inside ctest. `--write-seeds` writes seed corpora for the libFuzzer targets. Build with `MJ_SANITIZE=ON` for the strongest check.
