# src/

The M0 library, plain C17 with no outside libraries, built as the static library `mjolnir`. One file per header in [`include/mjolnir/`](../include/README.md):

`mj_common.c`, `mj_hash.c`, `mj_fs.c`, `mj_store.c`, `mj_ledger.c`, `mj_score.c`, `mj_toy.c`, `mj_synth.c`, `mj_dream.c`.

Rules for this folder: single-threaded, no locks; every input size-checked before use; malformed input refused; every write atomic and read back; failures leave the caller's state unchanged. See [architecture](../docs/guide/architecture.md).
