# Changelog

## Unreleased

- The real-weight path in C: a safetensors header reader that checks every offset, size and shape against the file before trusting it; MJDELTA1 pack and unpack, byte for byte as Python writes them; the apply on the CPU or, with `-DMJ_CUDA=ON`, the GPU, giving the same bytes either way; the working copy, its state file and checkout; register and apply with the same ledger records; and the `mj_real` tool. Parity tests against Python in `py_c_port`. See [docs/C-PORT.md](docs/C-PORT.md).
- The C apply writes a dirty marker (`MJOLNIR-DIRTY.txt`) before changing the working model file in place and removes it once the writes are read back, so a cut-short write is reported and repaired by the next apply.
- C register refuses a working folder that is the original folder (or inside it, or around it) before anything else; the Python register only checked this when it had to copy.
- Atomic writes in C use a temporary name of their own (`<path>.<pid>.<n>.tmp`, created exclusively), so two writers never share or delete each other's temporary file.
- Fuzz targets for the safetensors header and MJDELTA1 parsers.
- The low-rank edit in C (`mj_lowrank`, `mj_real lowrank`): whitening by the background keys, QR, SVD and truncation in double, with cuBLAS for the big products when built with CUDA. Matches Python's `low_rank` to a few parts in a million.

- Documentation: a guide under `docs/guide/` (overview, architecture, getting started on Windows and Linux, build and test, decisions, safety and rollback, labeling, real-weight edits, glossary), a roadmap, a README in every source folder, and `AGENTS.md`, `CREDITS.md`, `CONTRIBUTING.md`, `SECURITY.md` and `PROVENANCE.md`.

## M1

- Labeling tool, read-only real replays with partial-credit labels, GPU training of the toy policy, and the real-weight edit path on a working copy (register, dream, apply, evaluate, verify).
- `mj_store_verify`, a read-only checker for any version store.
- Model access note: API keys in Windows Credential Manager, no OAuth.

## M0

- Weight version store, hash-chained dream ledger and verifier, partial-credit scorer, reward-weighted update rule with step cap and drift gates, dry-run nightly and nap drivers, and a CMake test bench with fuzzers.
