# python/mjolnir/

The M1 package. It reads and writes exactly the formats of the C library, which stays the reference.

| Module | Role |
|---|---|
| `__main__.py` | The command line: `replays`, `label`, `toy-seed`, `toy-nap`, `real-register`, `real-dream`, `real-apply`, `real-eval`, `real-verify`. |
| `common.py` | Ids, free text, hashes, time and durable writes, mirroring the C rules. |
| `ledger.py` | The MJL1 hash-chained ledger. |
| `store.py` | The weight version store; refuses whole model files. |
| `grades.py` | JD's grades in their own ledger; the latest grade for a step wins. |
| `replay.py` | Read-only PCHIE research logs, chain-checked, labeled with partial credit. |
| `labeler.py` | The labeling page on `127.0.0.1` ([guide](../../docs/guide/labeling.md)). |
| `toy.py`, `toytrain.py` | The toy policy and its GPU nap on real replays. |
| [`real/`](real/README.md) | The real-weight edit path. |

`--data` names where stores, ledgers, grades and reports go. Keep it outside any shared or published folder.
