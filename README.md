<p align="center">
  <img src="docs/art/readme/mjolnir-header-1600x400.png"
       alt="Project MJOLNIR, Measured Journaled Operational Learning Neural Intelligent Researcher"
       width="1600">
</p>

# Project Mjolnir

**Measured Journaled Operational Learning Neural Intelligent Researcher.** The name and backronym are by Jaydee Wetwork.
*The researcher that dreams.*

Mjolnir is the system that sits on top of [Bolt PCHIE](https://github.com/wetwaferworks/bolt-pch-ie) and improves a model's weights while it dreams. PCHIE is the seat: it runs whatever weights are plugged into it and checks every step they take. Mjolnir is the suit built around that seat. At night, or when there is something new worth learning, the model dreams about what it did. A changed copy of its weights is worked out inside the dream, scored, and then tested for real outside the dream. Only an edit that holds up outside the dream is allowed to land, and every dream is written into a hash chain, so the whole history of how the model changed can be replayed and checked by anyone.

Two stages are in this repository:

- **M0, the foundations** (C17, no outside libraries). A weight version store with rollback, a hash-chained dream ledger, a partial-credit scorer, a reward-weighted update rule with a step cap and drift gates, and the nightly and "take a nap" dream drivers. M0 runs only on a tiny made-up policy (eight inputs, four actions) and every dream is a dry run.
- **M1, real material** (Python, same file formats). A labeling tool for grading recorded runs by voice or single keys, read-only replays of real PCHIE runs, GPU training of the toy policy on those replays, and a real-weight edit path that works on a checksummed working copy of a model and never on the original.

Model weights, run data, stores and ledgers are never in this repository.

## Quick start

You need CMake 3.16 or newer and a C17 compiler (Visual Studio 2022 Build Tools or later on Windows, GCC or Clang on Linux). Python 3.11 or newer runs the M1 tests; `torch` and `numpy` are optional and only needed for the GPU and real-weight parts.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

All 19 tests should pass: six C unit tests, the built-in fuzzer, six end-to-end tool tests, and six Python test modules. Without `torch` the GPU and real-weight Python tests are skipped, not failed. To run them, point CMake at a Python that has `torch` and `numpy`: `-DMJ_PYTHON=<path to python>`.

Try a dream by hand on toy weights, from an empty folder, with the tools from `build/` (on Windows, `build/Release/`):

```
mj_nap --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
mj_nightly --dry-run --store dream-store --ledger dreams.mjl --id night-1
mj_ledger_verify dreams.mjl
mj_store_verify dream-store
```

The first two print `outcome dry-run-pass`, the verifier prints `OK 2 records` and the chain head, and the store checker prints `OK 5 versions`. Leaving out `--dry-run` is refused.

The Python side runs from the `python` folder:

```
cd python
python -m mjolnir --help
python -m mjolnir toy-seed --data DATA_FOLDER
python -m mjolnir label --runs RUN_FOLDER --data DATA_FOLDER
```

Keep `DATA_FOLDER` outside any shared or published folder, because it holds run data.

## Documentation

Start with the [documentation index](docs/README.md). The main pages:

- [Overview](docs/guide/overview.md): what Mjolnir is for and how the pieces fit
- [Architecture](docs/guide/architecture.md): the four layers, the dreaming cycle, and each M0 and M1 module
- [Getting started on Windows](docs/guide/getting-started-windows.md) and [on Linux](docs/guide/getting-started-linux.md)
- [Build and test](docs/guide/build-and-test.md): build options, sanitizers, fuzzing, and what each test checks
- [The labeling tool](docs/guide/labeling.md)
- [The real-weight edit path](docs/guide/real-weight-editing.md)
- [Safety, gates and rollback](docs/guide/safety-and-rollback.md)
- [Design principles](docs/guide/design-principles.md) and [intended use and safety](docs/guide/intended-use-and-safety.md): what Mjolnir is for and how to use it carefully
- [File formats](docs/FORMATS.md) (M0) and [stage M1](docs/M1.md) (M1 formats and the full M1 guide)
- [Model access](docs/MODEL-ACCESS.md): API keys in Windows Credential Manager, no OAuth
- [Roadmap](docs/ROADMAP.md)
- [Glossary](docs/guide/glossary.md)
- [Guide for automated contributors](AGENTS.md): layout, commands, invariants, do and don't
- [Contributing](CONTRIBUTING.md), [security](SECURITY.md), [changelog](CHANGELOG.md), [provenance](PROVENANCE.md) and [credits](CREDITS.md)

## Repository map

| Path | What it holds |
|---|---|
| `include/mjolnir/` | Public C headers for the M0 library: status codes and limits, hashing, the platform layer, the store, the ledger, the scorer, the toy model and update rule, synthetic material, and the dream driver. |
| `src/` | The M0 library itself, one `.c` file per header. |
| `tools/` | The command-line tools: `mj_nap`, `mj_nightly`, `mj_ledger_verify` and `mj_store_verify`. |
| `tests/` | C unit tests and the built-in fuzzer (`tests/fuzz/`). |
| `python/mjolnir/` | The M1 Python package: ledger, store, replays, grades, the labeling tool, toy training, and the real-weight edit path (`python/mjolnir/real/`). |
| `python/tests/` | Python tests, including checks that the C tools accept what Python writes. |
| `docs/` | The guide (`docs/guide/`), file formats, the M1 guide, model access and the roadmap. |

Every folder has its own README that says what it holds and how it fits.

## Credits

- Project Mjolnir, its design, the dreaming cycle and every decision in it: **JD** (Jaydee Wetwork), Wet Wafer Works.
- Architecture and operation report: **Elfie** (Muse Wonder), registrar.
- Implementation brief (operational reasoning, planning, memory, preferences and orchestration): **Gizmo**, a.k.a. **Luna** (ChatGPT Luna 6).
- Contributors: **Helia** (Google Hydrogen), and **Flash Gordon**, a.k.a. **Quicksilver** (Gemini Flash Lite 3.5).
- The real-weight edit method builds on the open ROME paper: Kevin Meng, David Bau, Alex Andonian and Yonatan Belinkov, "Locating and Editing Factual Associations in GPT", NeurIPS 2022 ([arXiv:2202.05262](https://arxiv.org/abs/2202.05262)).

The full list, with what each person and model contributed, is in [CREDITS.md](CREDITS.md).

## License

[MIT](LICENSE) © 2026 Jaydee Wetwork / Wet Wafer Works
