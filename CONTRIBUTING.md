# Contributing

Thank you for helping. Mjolnir changes how a model's weights change, so the bar is careful, small and tested.

1. Build and run the whole suite first: `cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release`. All 19 tests must pass.
2. Keep each change to one idea, with a test that fails before and passes after.
3. C is C17 with no outside libraries and builds warning-free (`-Werror` / `/WX`). Python is 3.11+, standard library only outside `python/mjolnir/real/` and the toy trainer.
4. File format changes go in C, Python and the docs together, with a cross-check test.
5. Never commit weights, run data, stores, ledgers, keys or personal paths.
6. Read [AGENTS.md](AGENTS.md) for the invariants; they apply to everyone.
7. Standing gates are JD's to lift, not a contributor's ([decisions](docs/guide/decisions.md)).

By contributing you agree your work is released under the [MIT license](LICENSE).
