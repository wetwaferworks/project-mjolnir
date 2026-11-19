# src/

The M0 library, plain C17 with no outside libraries, built as the static library `mjolnir`. One file per header in [`include/mjolnir/`](../include/README.md):

`mj_common.c`, `mj_hash.c`, `mj_fs.c`, `mj_store.c`, `mj_ledger.c`, `mj_score.c`, `mj_toy.c`, `mj_synth.c`, `mj_dream.c`.

The real-weight path in C (see [the C port](../docs/C-PORT.md)): `mj_file.c` (reads and writes at an offset, file identity), `mj_safet.c` (the safetensors header, every bound checked), `mj_delta.c` (MJDELTA1), `mj_apply.c` with `mj_apply_cuda.cu` or `mj_apply_nocuda.c` (adding edits to a bf16 tensor), `mj_workcopy.c` (working copy, state file, checkout) `mj_session.c` (register and apply, with their ledger records), `mj_lowrank.c` with `mj_lowrank_cuda.cu` or `mj_lowrank_nocuda.c` (the low-rank edit) and `mj_par.c` (a parallel loop with a join, no locks), `mj_json.c` (the JSON reader for run logs), `mj_replay.c` (replays), `mj_grades.c` (grades) `mj_toytrain.c` (the toy nap from replays), and `mj_labeler.c`, `mj_labeler_page.c` and `mj_labeler_net.c` (the labeling page: session and HTTP answers, the page itself, and the 127.0.0.1 socket).

Rules for this folder: single-threaded, except that `mj_apply.c` and `mj_lowrank.c` split their loops across threads and join them before returning; no locks; every input size-checked before use; malformed input refused; every write atomic and read back; failures leave the caller's state unchanged. See [architecture](../docs/guide/architecture.md).
