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

File formats are specified in [docs/FORMATS.md](../docs/FORMATS.md).
