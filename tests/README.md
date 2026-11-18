# tests/

C unit tests for the M0 library, run by ctest. `mj_test.h` is the small shared test helper. Scratch files go in the folder named by `MJ_TEST_TMP` (set by CMake to `build/test-tmp/`).

| Test | Checks |
|---|---|
| `test_hash.c` | SHA-256 and CRC-32. |
| `test_store.c` | The version store: no overwrite, atomic writes, rollback, retention, ceilings, model-file refusal. |
| `test_ledger.c` | The hash chain: tamper, delete, reorder and torn writes caught at the line. |
| `test_score.c` | Partial credit for mazes and circuits. |
| `test_update.c` | The update rule improves without forgetting, rejects and rolls back. |
| `test_dream.c` | The nightly driver and nap end to end, dry run only. |

The fuzzer is in [`fuzz/`](fuzz/README.md). The full test list, including tool and Python tests, is in [build and test](../docs/guide/build-and-test.md).
