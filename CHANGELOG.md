# Changelog

## Unreleased

- Documentation: a guide under `docs/guide/` (overview, architecture, getting started on Windows and Linux, build and test, decisions, safety and rollback, labeling, real-weight edits, glossary), a roadmap, a README in every source folder, and `AGENTS.md`, `CREDITS.md`, `CONTRIBUTING.md`, `SECURITY.md` and `PROVENANCE.md`.

## M1

- Labeling tool, read-only real replays with partial-credit labels, GPU training of the toy policy, and the real-weight edit path on a working copy (register, dream, apply, evaluate, verify).
- `mj_store_verify`, a read-only checker for any version store.
- Model access note: API keys in Windows Credential Manager, no OAuth.

## M0

- Weight version store, hash-chained dream ledger and verifier, partial-credit scorer, reward-weighted update rule with step cap and drift gates, dry-run nightly and nap drivers, and a CMake test bench with fuzzers.
