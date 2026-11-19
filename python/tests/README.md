# python/tests/

unittest tests for the M1 package, run by ctest as `py_ledger`, `py_store`, `py_replay`, `py_labeler`, `py_toytrain`, `py_real`, `py_c_port` and `py_c_port_m1` (45 tests in all).

```
cd python
python -m unittest -v tests.test_ledger tests.test_store tests.test_replay tests.test_labeler tests.test_toytrain tests.test_real tests.test_c_port tests.test_c_port_m1
```

- `MJ_TEST_TMP`: scratch folder for test files.
- `MJ_TOOLS_DIR`: folder with the built C tools; the C/Python cross-checks are skipped without it.
- `test_toytrain.py` and `test_real.py` need `torch` and `numpy` and are skipped without them. The real-weight tests build small synthetic models; no real model is used.
- `test_c_port.py` and `test_c_port_m1.py`: parity between the C port and the Python modules (real-weight path and low-rank edit; replays, grades and the toy nap). See [docs/C-PORT.md](../../docs/C-PORT.md).
- `util.py`: shared helpers, including synthetic PCHIE research logs.
