# python/

The Python package. Most commands now have a C tool (`mj_m1`, `mj_real`); this side remains for parity tests and for the real-weight steps that still run a large model. Run commands from this folder, or put it on `PYTHONPATH`.

```
python -m mjolnir --help
```

- [`mjolnir/`](mjolnir/README.md): the package.
- [`tests/`](tests/README.md): the tests.

Python 3.11 or newer. The labeling tool, replays, stores and ledgers need only the standard library. GPU training and the real-weight path need `torch` (a CUDA build for the GPU) and `numpy`.
