# Intended use and safety

## What Mjolnir is for

Mjolnir is a research system for studying how a model can learn from its own verified experience. It records what a model did, measures the outcome, and works out small, checked changes to a copy of the model's weights while it "dreams". The material it learns from is games, mazes, circuit experiments and graded lessons, run inside [Bolt PCHIE](https://github.com/wetwaferworks/bolt-pch-ie), where every step is recorded and can be replayed.

It is meant for researchers, students and hobbyists who want to see, step by step, how a weight edit changes a model, and to be able to undo any of it.

## Using it carefully

- **Work on a copy.** Point Mjolnir at a separate working copy of a model, never the original download. Keep the original checksummed as your rollback.
- **Check before you trust.** An edit has to pass its checks outside the dream before it counts. Look at the results yourself before you rely on an edited model.
- **Keep the history.** Leave the hash-chained ledgers on, and keep a copy of the ledger heads somewhere else so the history can be checked later.
- **Mind the license.** Confirm a model's license allows the training or editing you plan before you run anything on it.
- **Keep edited models separate.** Label an edited model as edited, and keep it out of anything that expects the original.
- **Start small.** Use small steps and the replay mix (design principles [4 and 5](design-principles.md)), and watch the drift gates.

## Built-in safeguards

| Safeguard | Where it is enforced |
|---|---|
| The original model is never written | `python/mjolnir/real/workcopy.py` checksums the original, marks it read-only, requires a separate working folder and refuses a hard link to the original. |
| Stage M0 works on toy weights only | `mj_store` refuses payloads that look like whole model files (GGUF or safetensors); `python/mjolnir/store.py` does the same. |
| Every M0 dream is a dry run | `mj_dream` refuses to run without dry-run; `mj_nap` and `mj_nightly` refuse to start without `--dry-run`, and refuse `--live`. Two ctest cases (`tool_refuses_live`, `tool_refuses_no_dry_run`) prove it. |
| Nothing lands unless it verifies outside the dream | The four M0 gates in `mj_toy` and `toytrain.py`; the golden, task and no-trade gates in `real/edit.py`. |
| Every dream, score, grade and edit is recorded | `mj_ledger` and `python/mjolnir/ledger.py` (dreams, naps, edits, applies) and `python/mjolnir/grades.py` (grades). A broken chain stops all further writes. |
| Recurrence is guided, never open-ended | Each dream runs a fixed cycle with memory, step and time ceilings. |
| Nothing is wired into other systems | Nothing in this repository writes to PCHIE or its runners. The working copy is the only thing an edit ever writes to. |

See [Safety, gates and rollback](safety-and-rollback.md) for rollback steps, and [SECURITY.md](../../SECURITY.md) to report a problem.
