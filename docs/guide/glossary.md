# Glossary

| Term | Meaning |
|---|---|
| **Basis points (bp)** | Scores and shifts as integers from 0 to 10000, so every machine gives the same answer. |
| **Bolt PCHIE** | The deterministic harness that runs and checks a model. Mjolnir sits on top of it. |
| **Candidate** | A changed version produced by a dream. It becomes live only if every gate passes. |
| **Dream** | One cycle of working out a change on a copy, scoring it and gating it. |
| **Dry run** | A dream that records everything but never moves the live version. Every M0 dream is one. |
| **Golden set** | Older memories or ordinary text used to check that a change does not cause forgetting. |
| **Grade** | JD's label for one step: good, bad or productive. Stored in `grades.mjl`. |
| **HEAD** | The store's pointer to the live version. The only file ever replaced, atomically. |
| **Integrate** | Make a candidate live, after every gate passes. |
| **Ledger (MJL1)** | The hash-chained, append-only record of dreams, grades and edits. |
| **Little Nemo** | The model PCHIE runs in its mazes. |
| **MJBASE1 / MJDELTA1 / MJWORK1** | The base record, a stored low-rank edit, and the working copy's state file. |
| **Nap** | A dream started early because there is new verified experience (`mj_nap`). |
| **Nightly** | The scheduled dream, which also applies retention (`mj_nightly`). |
| **Not yet** | The partial-credit label for the step where a run failed. |
| **Partial credit** | Credit for everything a run got right before it failed (design principle 8). |
| **Pin / milestone** | A version that retention never retires. |
| **Productive** | A step that moved the task forward (earned a reward). |
| **Q16** | Fixed-point weights, 65536 = 1.0, used by the toy policy. |
| **ROME** | Rank-One Model Editing (Meng et al., 2022), the open method the real-weight path builds on. |
| **Snapshot** | The copy of the live version taken before every dream: the rollback point. |
| **Step cap** | The most any weight may move in one dream. |
| **Toy policy** | The tiny made-up model (eight inputs, four actions) M0 runs on. |
| **Working copy** | The checksummed copy of a model that edits are applied to. The original is never written. |
