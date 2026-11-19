# include/

Public C headers for the M0 library, under `include/mjolnir/`. Each header has a matching file in [`src/`](../src/README.md).

| Header | What it declares |
|---|---|
| `mj_common.h` | Status codes (`mj_status`), hard limits, and the memory budget every allocating module charges. |
| `mj_hash.h` | SHA-256 and CRC-32. |
| `mj_fs.h` | The thin platform layer (Win32 and POSIX): paths, files, durable writes, the clock. Nothing else talks to the operating system. |
| `mj_store.h` | The weight version store: put, get, HEAD, rollback, pins, retention, ceilings. |
| `mj_ledger.h` | The MJL1 hash-chained dream ledger and its verifier. |
| `mj_score.h` | Partial-credit scoring for mazes (per segment) and circuits (per element), in basis points. |
| `mj_toy.h` | The toy policy (8 inputs, 4 actions, Q16) and the reward-weighted update rule with its gates. |
| `mj_synth.h` | Synthetic dream material for M0 and the tests. |
| `mj_dream.h` | The nightly-dream driver and the nap entry point. |
| `mj_file.h` | Files read and written at an offset, file identity and link count, real paths, the read-only flag, streamed hashing and copying. |
| `mj_safet.h` | The safetensors header reader, with every offset, size and shape checked against the file. |
| `mj_bf16.h` | bfloat16 to and from float32, rounding as torch does. |
| `mj_delta.h` | MJDELTA1 pack and unpack, byte for byte as Python writes it. |
| `mj_apply.h` | Adding a chain of low-rank edits to one bf16 tensor, on the CPU or the GPU, with the same bytes either way. |
| `mj_workcopy.h` | Checksums, the working copy, its state file and dirty marker, and checkout. |
| `mj_session.h` | Register and apply, recorded in the real-weight ledger. |
| `mj_lowrank.h` | The low-rank edit from keys, outputs and gradients: whitening, QR, SVD, truncation; cuBLAS for the big products. |
| `mj_json.h` | A small JSON reader that answers like Python's `json` and `==` (used for PCHIE run logs). |
| `mj_replay.h` | Replays from PCHIE run logs: chain check, steps, automatic labels, folder scan. |
| `mj_grades.h` | Grades in their own hash-chained ledger; the latest grade per step. |
| `mj_labeler.h` | The labeling page: the grading session, HTTP answers, and the local server. |
| `mj_toytrain.h` | The toy nap from replays: features, training, gates, store and ledger. |
| `mj_par.h` | Splitting a loop across threads and joining them, with no locks. |
| `mj_hemi.h` | The classroom and the hemisphere swap on a shared hemisphere interface. |
| `mj_classroom.h` | Stage 1 of the hemisphere pipeline: the actor, the watcher, lessons and holdout gates. |
| `mj_hemi_gate.h` | The keep-or-roll-back decision for real models, from sealed sample sets. |

File formats are specified in [docs/FORMATS.md](../docs/FORMATS.md). The hemispheres are described in [docs/hemispheres.md](../docs/hemispheres.md).
