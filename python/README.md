# python/

The stage M1 Python side. Run commands from this folder, or put it on `PYTHONPATH`.

```
python -m mjolnir --help
```

- [`mjolnir/`](mjolnir/README.md): the package.
- [`tests/`](tests/README.md): the tests.

Python 3.11 or newer. The labeling tool, replays, stores and ledgers need only the standard library. GPU training and the real-weight path need `torch` (a CUDA build for the GPU) and `numpy`.
