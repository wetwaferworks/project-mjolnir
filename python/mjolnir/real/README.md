# python/mjolnir/real/

The real-weight edit path. It works on a working copy of a model, never the original. Guide: [real-weight edits](../../../docs/guide/real-weight-editing.md).

| Module | Role |
|---|---|
| `session.py` | The register, dream, apply and evaluate steps, each written to the ledger. `base` is the id of the untouched original. |
| `workcopy.py` | Original and working copy: checksums, read-only marking, applying a version's delta chain, rollback to base. |
| `edit.py` | Works out a reward-weighted low-rank edit (after ROME), tries strengths in memory, applies the gates. |
| `delta.py` | The MJDELTA1 format for a stored edit. |
| `model.py` | Runs the model's text part in 4 GB of graphics memory, layer by layer. |
| `safet.py` | Minimal safetensors reader; tensors mapped read-only. |
