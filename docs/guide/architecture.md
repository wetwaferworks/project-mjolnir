# Architecture

This page describes how Mjolnir is put together, from the big picture down to each module. How the stages are numbered is in the [overview](overview.md). The file formats are in [FORMATS.md](../FORMATS.md) and [M1.md](../M1.md). The ledger's writer token, repair and pinning are in [the dream ledger](the-ledger.md). The classroom and swap are in [the hemispheres](../hemispheres.md).

## The four layers (D1 to D4)

- **D1, the seating.** From the bottom up: the hardware; the weights, which are the dreamer; the Personal Control Harness, with its hash chain and step seals; Bolt PCHIE, the seat that runs and checks the weights; Mjolnir on top, with its reasoning, planning, memory, preferences and orchestration, and the weight editing at its core; and the domains it works in, from mazes and circuits to scientific research and robotics. Edits only flow down into the weights after they pass verification.
- **D2, the dreaming cycle.** Dream, watch, rest, verify, then commit or roll back. See below.
- **D3, memory and preferences.** Working memory, validated memory, persistent memory and long-term memory, with consolidation during rest. Preferences, how the system approaches problems and chooses between options, shape every tier and grow stronger at each commit.
- **D4, planning and orchestration.** A coordinator holds the goal, hands subtasks to several reasoning instances, and merges what they bring back into the sealed record. Every instance runs inside the harness.

## The dreaming cycle

1. **Dream.** Candidate weight changes are explored in simulation only, on a copy, never on the live weights.
2. **Watch.** An awake PCHIE observes the dream and labels what happened: good, bad or productive.
3. **Rest and score.** The results are weighed and scored against the outcome we want. This is where the day's verified experience gets folded in.
4. **Verify.** The candidate weights are put into a PCHIE and tested on the real task, outside the dream.
5. **Integrate or roll back.** If the candidate holds up, the edit lands and the memories and preferences behind it are saved. If it fails, the edit is thrown away, the previous weights stay live, and the reason it failed becomes a note the next dream reads.

The loop is guided, never fully self-recursive. A teacher stays in it: a person labels data, grades outcomes and decides what counts as productive.

In M0 the "watch" and "verify" steps are done by the toy scorer and gates. For real models, PCHIE can already play the rounds and hand sealed sample sets to `mj_hemi gate`, and Mjolnir works the scores out itself. A live PCHIE watching a dreamed edit on the working copy before it counts as integrated is M2 work (see the [roadmap](../ROADMAP.md)).

## Stage M0: the modules

| Part | What it does |
|---|---|
| Weight version store (`mj_store`) | Saves every version once, named by its SHA-256. Takes a full snapshot before every dream. Writes go to a temporary file, are flushed to disk and then renamed into place, and every write is read back and checked. A version can never be overwritten, not even after it is retired. Rollback re-checks the old version before making it live. Keeps the newest 14 nightly versions, the newest 14 snapshots and 14 candidates, plus every pinned milestone and whatever is live. Retired versions keep their record. A storage ceiling and a free-disk floor are checked before anything is written. |
| Dream ledger (`mj_ledger`) | One line per dream or grade: the dream, when it ran, what started it, who graded it, which replays went in, the weights before and the candidate, an edit summary, the scores, the behavior shift, each gate's result, the outcome, and the failure note. Every line carries the hash of the line before it and its own hash. Any change, deletion, reordering or torn write is caught at the exact line, and nothing more can be appended until the chain is whole again. One writer at a time holds `<ledger>.writer`. `mj_ledger_verify` recomputes the chain, and can pin a head, show or release a stale token, and repair a torn last line. See [the dream ledger](the-ledger.md). |
| Partial-credit scorer (`mj_score`) | Mazes are scored per segment: everything cleared before a failure is a success, the failure itself is "not yet", and anything after it is never graded. Circuits are scored per element: each working element is good and each faulty one is "not yet". Scores are integer basis points, so every machine gives the same answer. |
| Update rule (`mj_toy`) | Reward-weighted nudges on the toy policy: cleared segments strengthen the action taken and the "not yet" segment weakens it. Old memories are replayed in between the new ones. No weight may move more than a set cap in one dream. Four gates decide the result: the step cap held, the behavior shift on a golden set stayed under its limit, the golden score did not drop, and the task score went up. |
| Dream driver and nap (`mj_dream`, `mj_nightly`, `mj_nap`) | Checks the ledger, loads and checks the live weights, reads the last failure note and adjusts (half the step size and more old replay after a drift failure), takes the snapshot, dreams, scores, runs the gates, saves the candidate for the record, confirms the live weights did not move, and writes the ledger line. Both are dry runs in M0. The nightly run also applies the retention policy and re-checks the store. |

Everything is plain C17 with no outside libraries. There are no locks anywhere: M0 runs on one thread, and when work is split later it will hand off at phase boundaries or through atomics. Every input has a size limit and is checked before use, and anything malformed is refused rather than guessed at. Memory use per dream has a hard ceiling, and each dream has a step limit and a time limit. A dream that hits one leaves nothing behind but its ledger line.


### One M0 dream, step by step

From `include/mjolnir/mj_dream.h`:

1. Refuse unless dry-run (stage M0 never changes live weights).
2. Check the ledger chain; refuse to dream on a broken chain.
3. Load and verify the live weights named by `HEAD`.
4. Read the last dream's failure note and adapt (design principle 10).
5. Snapshot the live weights into the store: the rollback point.
6. Dream: reward-weighted nudges on a copy, old memories mixed in.
7. Watch and rest: partial-credit scoring of the candidate.
8. Verify: step, shift, golden and improvement gates.
9. Record the candidate in the store (never as `HEAD` in M0).
10. Check `HEAD` is unchanged, then append the dream to the ledger.

### Status codes

Every fallible C call returns an `mj_status` (`include/mjolnir/mj_common.h`). Anything other than `MJ_OK` leaves the caller's state unchanged. The codes name the reason: bad input, I/O failure, refused overwrite, not found, checksum mismatch, storage ceiling, size or memory ceiling, time or step ceiling, failed gate, refused by a standing gate, or broken hash chain.

## After M0: C, CUDA, and the M1 modules

Most of what started as the M1 Python package now has a C implementation, with CUDA where the GPU helps. C is the reference for every file format. The tests check both directions: the C tools accept what Python writes, and Python reads what C writes. Python remains for the steps that still run a large model's forward and backward passes (`real-dream`, `real-eval`), and as a parity check. The [C port page](../C-PORT.md) is the current map. The tools are `mj_m1` (replays, grades, the labeling page, the toy nap) and `mj_real` (register, apply, verify, rollback, low-rank edit).

### Hemispheres

The classroom (`mj_classroom`) and the hemisphere swap (`mj_hemisphere_swap`), driven by the `mj_hemi` tool, are later work in this same tree (listed as Unreleased, not as a new stage number). Each side is a model that can name a distribution over legal moves. The answer key is the final judge. Real models are decided by `mj_hemi gate`, which takes sealed sample sets and works out every score itself. See [the hemispheres](../hemispheres.md).

### The M1 modules

| Module | What it does |
|---|---|
| `mjolnir.common` | Shared rules for ids, free text, hashes, time and durable writes, mirroring `mj_common.c` and `mj_fs.c`. |
| `mjolnir.ledger` | The MJL1 dream ledger. Appends are refused while the chain is broken, and every append is checked again on disk. |
| `mjolnir.store` | The weight version store, same layout as `mj_store.c`. Refuses whole model files; real-weight edits are stored as deltas. |
| `mjolnir.grades` | Grades in their own hash-chained ledger (`grades.mjl`). Grades are never edited; the latest grade for a step wins. The C side is `mj_grades` / `mj_m1 grade`. |
| `mjolnir.replay` | Opens PCHIE research logs (`pchie.research.v1`) read-only, checks every line's hash and link, and labels each step productive, good or not yet. |
| `mjolnir.labeler` | The labeling tool: a page on `127.0.0.1` that steps through runs, least certain steps first. See [the labeling tool](labeling.md). |
| `mjolnir.toy` | The M0 toy policy (same byte format as `mj_toy.c`) and the features it reads from replay steps. |
| `mjolnir.toytrain` | A nap for the toy policy, trained on the GPU from real replays and grades, gated by the M0 gates. |
| `mjolnir.real` | The real-weight edit path: working copy, low-rank edits, in-memory trials, gates, apply and evaluate. The C side is `mj_workcopy`, `mj_lowrank`, `mj_session` and `mj_real`. Evaluating a large model still runs in Python. See [the real-weight edit path](real-weight-editing.md). |

### The real-weight path, in brief

- `real/workcopy.py`: the original download and its working copy. Applying a version restores every tensor an earlier edit touched to the original bytes, then adds the version's chain of deltas oldest first, and writes only those tensors back.
- `real/edit.py`: works out a reward-weighted, low-rank edit to the MLP output projection of chosen layers, after the rank-one model editing method of ROME, tries several strengths in memory, and keeps the strongest one that passes every gate.
- `real/delta.py`: the MJDELTA1 file format for a stored edit, a few megabytes at most.
- `real/model.py` and `real/safet.py`: run the text part of the model with 4 GB of graphics memory. Weights stay in the file, mapped read-only; no code from a model download is ever run.
- `real/session.py`: the register, dream, apply and evaluate steps, each one written to the ledger.

## Design rules that hold everywhere

- **No locks.** M0 runs on one thread; when work is split later it hands off at phase boundaries or through atomics. The labeling server is single-threaded too.
- **Strict parsing.** One spelling per value, size limits checked before use, and anything malformed is refused rather than guessed at.
- **Durable writes.** Write to a temporary file, flush, rename into place, read back and check.
- **Determinism.** M0 math is integer with a fixed sample order, so a dream gives the same candidate bit for bit on every machine. Scores are integer basis points.
- **Bounded work.** Every dream has a memory ceiling, a step limit and a time limit. A dream that hits one leaves nothing behind but its ledger line.
