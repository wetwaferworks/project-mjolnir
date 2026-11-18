# Getting started on Linux

## What you need

- CMake 3.16 or newer and GCC or Clang with C17 support.
- Python 3.11 or newer.
- Optional: `torch` and `numpy` for GPU training and the real-weight path.

## Build and test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

The C code is built with `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wvla -Werror`, so any warning fails the build. Expect `100% tests passed, 0 tests failed out of 19`.

## A first dream

From an empty folder, with `B` set to your build folder:

```
$B/mj_nap --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
$B/mj_nightly --dry-run --store dream-store --ledger dreams.mjl --id night-1
$B/mj_ledger_verify dreams.mjl
$B/mj_store_verify dream-store
```

The verifier prints `OK 2 records` and the chain head; the store checker prints `OK 5 versions, head toy-genesis`.

## Sanitizers

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMJ_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan
```

See [build and test](build-and-test.md) for fuzzing and the full test list.
