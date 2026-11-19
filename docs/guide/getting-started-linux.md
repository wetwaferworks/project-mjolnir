# Getting started on Linux

## What you need

- CMake 3.16 or newer and GCC or Clang with C17 support.
- Optional: the CUDA toolkit, for `-DMJ_CUDA=ON`. Without it the CPU path runs, with the same bytes.
- Optional: Python 3.11 or newer.
- Optional: `torch` and `numpy` for the GPU and real-weight Python tests.

## Build and test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Add `-DMJ_CUDA=ON` when the toolkit is installed. The C code is built with `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wvla -Werror`, so any warning fails the build. Expect **34** tests with the Python tests on, or **26** without them.

## A first dream

From an empty folder, with `B` set to your build folder:

```
$B/mj_nap --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
$B/mj_nightly --dry-run --store dream-store --ledger dreams.mjl --id night-1
$B/mj_ledger_verify dreams.mjl
$B/mj_store_verify dream-store
```

The verifier prints `OK 2 records` and the chain head; the store checker prints `OK 5 versions, head toy-genesis`. Pin the head later with `mj_ledger_verify --expect SEQ:HEAD dreams.mjl`.

## A first classroom

```
$B/mj_hemi pipeline --data hemi-data --classroom-rounds 2 --rounds 2
$B/mj_ledger_verify hemi-data/hemispheres.mjl
```

Keep `hemi-data` outside a shared or published folder.

## Sanitizers

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMJ_SANITIZE=ON -DMJ_PYTHON_TESTS=OFF
cmake --build build-asan -j
ctest --test-dir build-asan
```

Expect 26 tests. On GCC and Clang this also turns on UndefinedBehaviorSanitizer. See [build and test](build-and-test.md) for fuzzing and the full test list.
