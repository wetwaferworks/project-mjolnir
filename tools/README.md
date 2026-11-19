# tools/

Command-line tools built on the M0 library. Built into `build/` (or `build/Release/` on Windows).

| Tool | Usage | What it does |
|---|---|---|
| `mj_nap` | `mj_nap --dry-run --store DIR --ledger FILE --id DREAM_ID [--grader NAME] [--seed-toy]` | One dream now ("take a nap"). `--seed-toy` creates a toy store if none exists. Refuses without `--dry-run` and refuses `--live`. |
| `mj_nightly` | `mj_nightly --dry-run --store DIR --ledger FILE --id DREAM_ID` | The nightly dream, then retention and a store re-check. |
| `mj_ledger_verify` | `mj_ledger_verify FILE` | Recomputes the ledger chain from the start; prints `OK n records` and the head, or the first bad line. |
| `mj_store_verify` | `mj_store_verify STORE [--max-blob-mb N]` | Re-checks every version (manifest, size, CRC-32, SHA-256) and HEAD. Read-only. Exit 0 good, 1 bad, 2 usage. |
| `mj_real` | `mj_real register\|apply\|verify\|status\|delta-info\|delta-repack ...` | The real-weight path without model code: make the working copy, apply any stored version (`--version base` rolls back), check a folder against a sums file, show the working copy's state, and work out a low-rank edit from captured keys and gradients (`lowrank`). Same files as the Python `real-*` commands. Exit 0 good, 1 mismatch or dirty, 2 refused. |
| `mj_m1` | `mj_m1 replays\|replays-json\|label\|grade\|grades\|toy-seed\|toy-nap ...` | The stage M1 commands of `python -m mjolnir` in C: list the replays in run folders (or dump them as JSON), serve the labeling page, add and list grades, seed the toy store, and run a toy nap from replays. Same `--data` layout and files as the Python commands. Exit 0 good, 2 refused. |
| `mj_hemi` | `mj_hemi pipeline\|classroom\|swap --data DIR [--classroom-rounds N] [--rounds N] [--scenario complement\|ternary] [--seed S] [--gpu]`, `mj_hemi show\|replay FILE`, `mj_hemi bench`, `mj_hemi gate --data DIR --kind actor\|watcher\|swap --round N --lesson FILE... --holdout FILE... --parent SHA --candidate SHA [--other SHA] [--note TEXT]` | The left and right hemispheres on toy policies: the classroom, then the hemisphere swap, each round journaled in `DIR/hemispheres.mjl`; show or replay an MJHEMI2 sample set; time the CPU and GPU scorers; `gate` decides keep (exit 0) or roll back (exit 3) for a real model's round from its lesson and holdout sets, working out every score itself and refusing (exit 2) sets that are not bound to the models named, and journals the round. See [hemispheres](../docs/hemispheres.md). |

`mj_cli_common.h` holds the shared argument parsing.
