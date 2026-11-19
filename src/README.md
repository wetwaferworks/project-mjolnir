# src/

The Mjolnir library, plain C17, built as the static library `mjolnir`. CUDA files are compiled only with `-DMJ_CUDA=ON`. One file per header in [`include/mjolnir/`](../include/README.md).

The M0 core: `mj_common.c`, `mj_hash.c`, `mj_fs.c`, `mj_store.c`, `mj_ledger.c`, `mj_score.c`, `mj_toy.c`, `mj_synth.c`, `mj_dream.c`.

The real-weight path and the rest of the C port (see [the C port](../docs/C-PORT.md)): `mj_file.c`, `mj_safet.c`, `mj_delta.c`, `mj_apply.c` with `mj_apply_cuda.cu` or `mj_apply_nocuda.c`, `mj_workcopy.c`, `mj_session.c`, `mj_lowrank.c` with `mj_lowrank_cuda.cu` or `mj_lowrank_nocuda.c`, `mj_par.c`, `mj_json.c`, `mj_replay.c`, `mj_grades.c`, `mj_toytrain.c`, and `mj_labeler.c`, `mj_labeler_page.c` and `mj_labeler_net.c`.

The hemispheres (see [the hemispheres](../docs/hemispheres.md)): `mj_hemi.c` with `mj_hemi_cuda.cu` or `mj_hemi_nocuda.c`, `mj_classroom.c` and `mj_hemi_gate.c`.

Rules for this folder: single-threaded, except that `mj_apply.c` and `mj_lowrank.c` split their loops across threads and join them before returning; no locks; every input size-checked before use; malformed input refused; every write atomic and read back; failures leave the caller's state unchanged. See [architecture](../docs/guide/architecture.md).
