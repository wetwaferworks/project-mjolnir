# Guide for automated contributors

This file is for coding assistants and other automated contributors working in this repository. People are welcome to read it too: it is the short, strict version of the rest of the documentation.

## Layout

| Path | Language | Role |
|---|---|---|
| `include/mjolnir/`, `src/` | C17 (one CUDA file) | The library. The reference for every file format. `src/mj_apply_cuda.cu` and `src/mj_lowrank_cuda.cu` are built only with `-DMJ_CUDA=ON`. |
| `tools/` | C17 | `mj_nap`, `mj_nightly`, `mj_ledger_verify`, `mj_store_verify`, `mj_real`, `mj_m1`. |
| `tests/`, `tests/fuzz/` | C17 | Unit tests and the built-in fuzzer. |
| `python/mjolnir/` | Python 3.11+ | The M1 package; `real/` is the real-weight edit path. |
| `python/tests/` | Python | unittest tests, including C/Python cross-checks. |
| `docs/` | Markdown | Guide, formats, M1 guide, model access, roadmap. |

## Commands

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release          # expect 27/27 passing
cd python && python -m mjolnir --help
```

Run the full ctest suite before proposing any change. Add `-DMJ_SANITIZE=ON` for memory-safety work.

## Invariants (never break these)

1. **No real weights in M0.** The store refuses whole model files. Do not weaken that check.
2. **Dry run only in M0.** `mj_nap` and `mj_nightly` must refuse without `--dry-run` and refuse `--live`.
3. **The original model is never written.** Only the working copy changes, and `real-apply --version base` must always restore it byte for byte.
4. **Append-only, hash-chained ledgers.** Never edit, reorder or delete a ledger line. A broken chain refuses appends.
5. **Versions are immutable.** A version id is never written twice, even after retirement.
6. **One format, two languages.** C is the reference. Any format change goes in C first, in `docs/FORMATS.md` or `docs/M1.md`, and in Python, with a cross-check test.
7. **No locks.** Hand off at phase boundaries or through atomics.
8. **Strict parsing.** Refuse malformed input; never guess. Check sizes before use.
9. **Deterministic M0.** Integer math and a fixed sample order. Do not add floating point or randomness without a fixed seed to M0.
10. **Nothing edited reaches official runners.** Do not wire Mjolnir output into PCHIE's official runners or records.

## Do

- Keep changes small and tested. Every bug fix gets a test.
- Keep docs accurate to the code; update the page when you change behavior.
- Keep run data in a `--data` folder outside the repository.
- Write plain, human-readable text in docs and commit messages.

## Don't

- Don't commit weights, adapters, stores, ledgers, grades, replays or run output (see `.gitignore`).
- Don't commit keys, tokens or passwords, or put them on a command line or in a log. Hosted-model keys live in Windows Credential Manager only ([model access](docs/MODEL-ACCESS.md)).
- Don't commit personal paths, user names or machine names.
- Don't feed lessons from failed runs into a model's prompt without JD's go.
- Don't lift a standing gate. Only JD can ([decisions](docs/guide/decisions.md)).
- Don't give the model tools; it only plays the task it is taught.
