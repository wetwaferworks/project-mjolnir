# Build and test

## Build options

| Option | Default | Effect |
|---|---|---|
| `MJ_SANITIZE` | `OFF` | AddressSanitizer everywhere, plus UndefinedBehaviorSanitizer on GCC and Clang. With MSVC, the ASan runtime DLL is copied beside the test programs. |
| `MJ_LIBFUZZER` | `OFF` | Also build libFuzzer targets (`mj_libfuzz_ledger`, `_manifest`, `_toy`, `_score`) when the compiler supports `-fsanitize=fuzzer`. |
| `MJ_FUZZ_ITERATIONS` | `100000` | Inputs per target for the built-in fuzz test. |
| `MJ_PYTHON_TESTS` | `ON` | Add the Python tests to ctest. |
| `MJ_PYTHON` | (found) | The interpreter for the Python tests. Point it at one with `torch` and `numpy` to run the GPU and real-weight tests. |

## The test bench (19 ctest cases)

| Test | What it checks |
|---|---|
| `test_hash` | SHA-256 and CRC-32 against known vectors. |
| `test_store` | Content addressing, no overwrite ever, atomic writes and read-back, rollback re-checks, retention, ceilings, refusal of model files. |
| `test_ledger` | The hash chain: tamper, deletion, reordering and torn writes are caught at the exact line, and appends are refused until the chain is whole. |
| `test_score` | Partial credit for mazes (per segment) and circuits (per element), in basis points. |
| `test_update` | The toy update rule improves without forgetting, rejects and rolls back, and holds the step cap. |
| `test_dream` | The nightly driver and the nap entry point end to end, dry run only. |
| `fuzz_builtin` | The built-in deterministic fuzzer over the ledger, manifest, toy and score parsers. |
| `tool_clean`, `tool_nap`, `tool_nightly`, `tool_ledger_verify` | The tools end to end on a scratch store. |
| `tool_refuses_live`, `tool_refuses_no_dry_run` | The tools refuse `--live` and refuse to run without `--dry-run` (expected to fail, so they pass when refused). |
| `py_ledger`, `py_store`, `py_replay`, `py_labeler`, `py_toytrain`, `py_real` | The Python modules, 37 tests in all, including the C tools reading Python-written stores and ledgers. `py_toytrain` and `py_real` need `torch` and `numpy` and are skipped without them. The real-weight tests use small synthetic models, never a real download. |

Test scratch files go in `build/test-tmp/` (set by `MJ_TEST_TMP`).

## Running the Python tests directly

```
cd python
python -m unittest -v tests.test_ledger tests.test_store tests.test_replay tests.test_labeler tests.test_toytrain tests.test_real
```

Set `MJ_TOOLS_DIR` to the folder holding the built C tools to include the cross-checks against C; without it those checks are skipped.

## The fuzzer

```
mj_fuzz [iterations-per-target] [seed]
mj_fuzz --write-seeds DIR
```

The built-in fuzzer mutates valid seeds deterministically and needs no outside library, so it runs the same on Windows and Linux and inside ctest. `--write-seeds` writes seed corpora for the libFuzzer targets. Build with `MJ_SANITIZE=ON` for the strongest check.
