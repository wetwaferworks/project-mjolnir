# Changelog

## Unreleased

- The left and right hemispheres on toy policies (`mj_hemi`, `mj_classroom`, the `mj_hemi` tool), as a two-stage pipeline. Stage 1, the classroom: the left acts, the right watches and grades, the actor learns from its errors and the watcher from its misses, each kept only if better on holdout worlds. Stage 2, the hemisphere swap: each side plays while the other watches, both sample sets combine into one child, kept only if it beats both parents. The answer key is the final judge throughout, every change is on a copy with SHA-checked rollback, and every lesson and round is journaled. The MJHEMI1 sample-set format with a fuzz target, a CUDA holdout scorer that matches the CPU bit for bit, and the [hemispheres page](docs/hemispheres.md).
- `mj_hemi gate`: the keep-or-roll-back decision for real models. A world outside Mjolnir (PCHIE) plays and grades the rounds, writes the MJHEMI1 sample sets and passes the holdout scores and the SHA-256 of the parent and the candidate. The gate checks every set's seal and bounds, applies the actor, watcher or swap rule, and appends the round to the hemispheres journal. Exit 0 keeps, 3 rolls back.
- The real-weight path in C: a safetensors header reader that checks every offset, size and shape against the file before trusting it; MJDELTA1 pack and unpack, byte for byte as Python writes them; the apply on the CPU or, with `-DMJ_CUDA=ON`, the GPU, giving the same bytes either way; the working copy, its state file and checkout; register and apply with the same ledger records; and the `mj_real` tool. Parity tests against Python in `py_c_port`. See [docs/C-PORT.md](docs/C-PORT.md).
- The C apply writes a dirty marker (`MJOLNIR-DIRTY.txt`) before changing the working model file in place and removes it once the writes are read back, so a cut-short write is reported and repaired by the next apply.
- C register refuses a working folder that is the original folder (or inside it, or around it) before anything else; the Python register only checked this when it had to copy.
- Atomic writes in C use a temporary name of their own (`<path>.<pid>.<n>.tmp`, created exclusively), so two writers never share or delete each other's temporary file.
- Fuzz targets for the safetensors header and MJDELTA1 parsers.
- Replays, grades and the toy nap in C (`mj_replay`, `mj_grades`, `mj_toytrain`, the `mj_m1` tool), with a small JSON reader that follows Python's rules. The same run folders give byte-identical replay dumps; grades and toy-nap ledgers are shared both ways. Parity tests in `py_c_port_m1`; a JSON fuzz target.
- The labeling page in C (`mj_labeler`, `mj_m1 label`): the same page, order of steps, answers and grades ledger as the Python tool, on 127.0.0.1 only. Stalled connections are dropped after 5 seconds and the port is bound exclusively on Windows. Side-by-side parity test against the Python session; an HTTP fuzz target.
- The low-rank edit in C (`mj_lowrank`, `mj_real lowrank`): whitening by the background keys, QR, SVD and truncation in double, with cuBLAS for the big products when built with CUDA. Matches Python's `low_rank` to a few parts in a million.

- Documentation: a guide under `docs/guide/` (overview, architecture, getting started on Windows and Linux, build and test, decisions, safety and rollback, labeling, real-weight edits, glossary), a roadmap, a README in every source folder, and `AGENTS.md`, `CREDITS.md`, `CONTRIBUTING.md`, `SECURITY.md` and `PROVENANCE.md`.

## M1

- Labeling tool, read-only real replays with partial-credit labels, GPU training of the toy policy, and the real-weight edit path on a working copy (register, dream, apply, evaluate, verify).
- `mj_store_verify`, a read-only checker for any version store.
- Model access note: API keys in Windows Credential Manager, no OAuth.

## M0

- Weight version store, hash-chained dream ledger and verifier, partial-credit scorer, reward-weighted update rule with step cap and drift gates, dry-run nightly and nap drivers, and a CMake test bench with fuzzers.
