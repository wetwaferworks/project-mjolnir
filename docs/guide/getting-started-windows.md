# Getting started on Windows

## What you need

- Visual Studio 2022 Build Tools or later, with the C++ workload (it includes CMake). Run the commands below from a Developer PowerShell, or put the CMake that ships with Visual Studio on `PATH`.
- Optional: the CUDA toolkit, if you want the GPU paths (`-DMJ_CUDA=ON`). The default architecture is 86. Without CUDA the CPU path runs, with the same bytes.
- Optional: Python 3.11 or newer, for the parity tests and the few remaining Python commands.
- Optional, for the GPU and real-weight *Python* tests: a Python environment with a CUDA build of `torch`, and `numpy`. The real-weight path also needs a model in safetensors format, kept outside this repository.

## Build and test

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Add `-DMJ_CUDA=ON` to the first command when the toolkit is installed. `-DMJ_PYTHON=C:\path\to\python.exe` is optional; without it CMake uses the first Python it finds, and if that Python has no `torch` the GPU and real-weight Python tests are skipped rather than failed.

Expect **34** tests in a Release build with the Python tests on, or **26** when Python tests are off.

The tools land in `build\Release\`: `mj_nap.exe`, `mj_nightly.exe`, `mj_ledger_verify.exe`, `mj_store_verify.exe`, `mj_m1.exe`, `mj_real.exe` and `mj_hemi.exe`.

## A first dream

From an empty folder:

```
build\Release\mj_nap.exe --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
build\Release\mj_nightly.exe --dry-run --store dream-store --ledger dreams.mjl --id night-1
build\Release\mj_ledger_verify.exe dreams.mjl
build\Release\mj_store_verify.exe dream-store
```

(Adjust the `build\Release\` prefix to wherever you built.) Each dream prints its snapshot, candidate, gate results and `outcome dry-run-pass`. Write down the head the verifier prints if you want to pin it later with `--expect`.

## A first classroom

```
build\Release\mj_hemi.exe pipeline --data hemi-data --classroom-rounds 2 --rounds 2
build\Release\mj_ledger_verify.exe hemi-data\hemispheres.mjl
```

Keep `hemi-data` outside a shared or published folder.

## AddressSanitizer

```
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DMJ_SANITIZE=ON -DMJ_PYTHON_TESTS=OFF
cmake --build build-asan --config Debug
ctest --test-dir build-asan -C Debug
```

Expect 26 tests. See [build and test](build-and-test.md) for the full options and the test list.

## The Python side

```
cd python
python -m mjolnir --help
```

Most commands also have a C tool (`mj_m1`, `mj_real`). `real-dream` and `real-eval` still run in Python, because they run the model. See [the labeling tool](labeling.md) and [the real-weight edit path](real-weight-editing.md). Hosted-model keys, when they are needed, go in Windows Credential Manager; see [model access](../MODEL-ACCESS.md).
