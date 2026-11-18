# Getting started on Windows

## What you need

- Visual Studio 2022 Build Tools or later, with the C++ workload (it includes CMake). Run the commands below from a Developer PowerShell, or put the CMake that ships with Visual Studio on `PATH`.
- Python 3.11 or newer for the M1 tests and tools.
- Optional, for GPU training and the real-weight path: a Python environment with a CUDA build of `torch`, and `numpy`. The real-weight path also needs a model in safetensors format.

## Build and test

```
cmake -S . -B build -DMJ_PYTHON=C:\path\to\python.exe
cmake --build build --config Release
ctest --test-dir build -C Release
```

`-DMJ_PYTHON` is optional. Without it CMake uses the first Python it finds; if that Python has no `torch`, the GPU and real-weight tests are skipped rather than failed. Expect `100% tests passed, 0 tests failed out of 19`.

The tools land in `build\Release\`: `mj_nap.exe`, `mj_nightly.exe`, `mj_ledger_verify.exe` and `mj_store_verify.exe`.

## A first dream

From an empty folder:

```
build\Release\mj_nap.exe --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
build\Release\mj_nightly.exe --dry-run --store dream-store --ledger dreams.mjl --id night-1
build\Release\mj_ledger_verify.exe dreams.mjl
build\Release\mj_store_verify.exe dream-store
```

(Adjust the `build\Release\` prefix to wherever you built.) Each dream prints its snapshot, candidate, gate results and `outcome dry-run-pass`.

## The Python side

```
cd python
python -m mjolnir --help
```

See [the labeling tool](labeling.md) and [the real-weight edit path](real-weight-editing.md). Hosted-model keys, when they are needed, go in Windows Credential Manager; see [model access](../MODEL-ACCESS.md).
