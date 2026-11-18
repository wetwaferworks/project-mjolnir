# JD's decisions and the standing gates

Project Mjolnir is built to a short list of design decisions made by JD (Jaydee Wetwork), and to standing gates that hold until JD lifts them himself. Code that contradicts either is a bug. This page is the reference; other pages point here.

## The decisions (November 15, 2026)

1. **Structure.** The four-layer picture in [the architecture](architecture.md) is how the system is built. Editing weights during dreams is the core, and it is the part that has to be right.
2. **What a dream edits.** The full weights, directly. A full copy is saved first, so any dream can be rolled back.
3. **The update rule.** Strengthen whatever led to productive outcomes, and weaken whatever did not.
4. **Step size.** Small nudges at first. Catastrophic forgetting must not happen.
5. **The replay mix.** Older memories that already became better reasoning are mixed in with the new ones.
6. **Dream material.** Replays of recorded runs, circuit experiments and tests, read mainly from the memory file.
7. **Labeling.** Mazes and circuits are labeled automatically, because their outcomes are plainly true or false. The language stage works like a preschool curriculum, with JD grading in the loop, and that needs a labeling and training tool that helps him step through the runs.
8. **Partial credit counts.** A maze run that clears one pit and falls into the next is a partial success. A pixel screen with three pixels wired right and a mistake on the fourth, or a fault in the memory transmission, counts the good wiring as good and marks only the faulty part as needing refinement.
9. **When it dreams.** Every night. New verified experience can also start a dream early, through a "take a nap" function.
10. **Failed dreams.** When verification fails, the dreamer learns why. JD grading an outcome as not productive is one source of that reason.
11. **Rollback history.** Keep about two weeks of weight versions, as storage allows. Training runs on the workstation, at full compute.

## Standing gates

These hold for every stage until JD lifts them himself:

- Nothing touches the real weights until it verifies outside the dream.
- No real model weights are edited, and no adapter is applied, without JD's explicit go. On November 17, 2026, JD gave that go for a working copy: edits may be made and evaluated there, the original download stays untouched and checksummed as the rollback, every edit is versioned in the store, and nothing edited is wired into PCHIE's official runners.
- No training run on any real model until its license is confirmed, Nemotron included.
- Guided recurrence only, never full recursion.
- Every dream, score, grade and commit enters the hash chain.
- Lessons from failed runs are not fed into a model's prompt without JD's go.
- The model is told about its own harness.

M0 adds two of its own: it works on toy weights only, and every dream is a dry run. The store refuses files that look like real model weights, and the dream driver and both tools refuse to run without `--dry-run`.

## What the gates mean in the code

| Gate | Where it is enforced |
|---|---|
| Toy weights only in M0 | `mj_store` refuses payloads that look like whole model files (GGUF or safetensors); `python/mjolnir/store.py` does the same. |
| Every M0 dream is a dry run | `mj_dream` refuses to run without dry-run; `mj_nap` and `mj_nightly` refuse to start without `--dry-run`, and refuse `--live`. Two ctest cases (`tool_refuses_live`, `tool_refuses_no_dry_run`) prove it. |
| The original model is never written | `python/mjolnir/real/workcopy.py` checksums the original, marks it read-only, requires a separate working folder and refuses a hard link to the original. |
| Every dream, score, grade and commit enters the hash chain | `mj_ledger` and `python/mjolnir/ledger.py` (dreams, naps, edits, applies) and `python/mjolnir/grades.py` (grades). |
| Integration needs every gate to pass | The four M0 gates in `mj_toy` and `toytrain.py`; the golden, task and no-trade gates in `real/edit.py`. |
| Nothing edited goes into official runners | Nothing in this repository writes to PCHIE or its runners. The working copy is the only thing an edit ever writes to. |

See [Safety, gates and rollback](safety-and-rollback.md) for how each rule plays out step by step.
