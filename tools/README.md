# tools/

Command-line tools built on the M0 library. Built into `build/` (or `build/Release/` on Windows).

| Tool | Usage | What it does |
|---|---|---|
| `mj_nap` | `mj_nap --dry-run --store DIR --ledger FILE --id DREAM_ID [--grader NAME] [--seed-toy]` | One dream now ("take a nap"). `--seed-toy` creates a toy store if none exists. Refuses without `--dry-run` and refuses `--live`. |
| `mj_nightly` | `mj_nightly --dry-run --store DIR --ledger FILE --id DREAM_ID` | The nightly dream, then retention and a store re-check. |
| `mj_ledger_verify` | `mj_ledger_verify FILE` | Recomputes the ledger chain from the start; prints `OK n records` and the head, or the first bad line. |
| `mj_store_verify` | `mj_store_verify STORE [--max-blob-mb N]` | Re-checks every version (manifest, size, CRC-32, SHA-256) and HEAD. Read-only. Exit 0 good, 1 bad, 2 usage. |
| `mj_real` | `mj_real register\|apply\|verify\|status\|delta-info\|delta-repack ...` | The real-weight path without model code: make the working copy, apply any stored version (`--version base` rolls back), check a folder against a sums file, show the working copy's state, and work out a low-rank edit from captured keys and gradients (`lowrank`). Same files as the Python `real-*` commands. Exit 0 good, 1 mismatch or dirty, 2 refused. |

`mj_cli_common.h` holds the shared argument parsing.
