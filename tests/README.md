# tests/

C unit tests for the Mjolnir library, run by ctest. `mj_test.h` is the small shared test helper. Scratch files go in the folder named by `MJ_TEST_TMP` (set by CMake to `build/test-tmp/`).

| Test | Checks |
|---|---|
| `test_hash.c` | SHA-256 and CRC-32. |
| `test_store.c` | The version store: no overwrite, atomic writes, rollback, retention, ceilings, model-file refusal. |
| `test_ledger.c` | The hash chain: tamper, delete, reorder and torn writes caught at the line. |
| `test_ledger_writer.c` | One writer at a time: the token file, a live writer refused, a stale token, torn-tail repair, and `--expect`. |
| `test_score.c` | Partial credit for mazes and circuits. |
| `test_update.c` | The update rule improves without forgetting, rejects and rolls back. |
| `test_dream.c` | The nightly driver and nap end to end, dry run only. |
| `test_safet.c` | The safetensors header reader: bounds, sizes, overlaps, duplicate names, strict JSON. |
| `test_delta.c` | MJDELTA1 pack and unpack, every refusal, and Python's float repr for the scale. |
| `test_lowrank.c` | The low-rank edit against textbook formulas on small sizes (explicit inverse, dense product), truncation error, refusals, CPU against GPU. |
| `test_replay.c` | The JSON reader (Python's refusals, `==` and dump format), replays from run logs and their refusals, the folder scan, safe ids, the grades ledger. |
| `test_labeler.c` | The labeling page without a socket: step order, grade, skip and back, and the HTTP answers and refusals. |
| `test_toytrain.c` | The toy nap from replays: features, deterministic training, gates, dry run and integrate, refusals. |
| `mj_runlog.h` | Writes a chained PCHIE run log for the tests, the same shape as `python/tests/util.py`. |
| `test_hemi.c` | The hemispheres: verdicts, MJHEMI2 (bit flips, truncation, resealed bad fields), the combiner and trust, merge and training, the classroom's watcher lessons and per-side gates, whole rounds kept and rolled back, repeatable bit for bit, the gate for real-model rounds (scores worked out from the sets, models bound by SHA-256, the same states, moves that match their distributions, one grade per move, refusals with reasons), and CPU/GPU agreement. |
| `test_workcopy.c` | Register, apply, roll back, the dirty marker, the refusals, unique temporary names, and CPU/GPU agreement on the apply. |

The fuzzer is in [`fuzz/`](fuzz/README.md). The full test list, including tool and Python tests, is in [build and test](../docs/guide/build-and-test.md).
