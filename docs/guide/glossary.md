# Glossary

| Term | Meaning |
|---|---|
| **Basis points (bp)** | Scores and shifts as integers from 0 to 10000, so every machine gives the same answer. |
| **Bolt PCHIE** | The deterministic harness that runs and checks a model. Mjolnir sits on top of it. |
| **Candidate** | A changed version produced by a dream. It becomes live only if every gate passes. |
| **Classroom** | Stage 1 of the hemisphere pipeline: one side acts, the other watches and grades, each learns from its own mistakes. |
| **Dream** | One cycle of working out a change on a copy, scoring it and gating it. |
| **Dry run** | A dream that records everything but never moves the live version. Every M0 dream is one. |
| **Golden set** | Older memories or ordinary text used to check that a change does not cause forgetting. |
| **Grade** | A teacher's label for one step: good, bad or productive. Stored in `grades.mjl`. |
| **Hemisphere swap** | Stage 2 of the hemisphere pipeline: both sides play, their samples are combined, and the child is kept only if it beats both parents. |
| **HEAD** | The store's pointer to the live version. The only file ever replaced, atomically. |
| **Integrate** | Make a candidate live, after every gate passes. |
| **Ledger (MJL1)** | The hash-chained, append-only record of dreams, grades and edits. See [the dream ledger](the-ledger.md). |
| **Little Nemo** | The smaller model PCHIE runs in its mazes (Nemotron 3.5 Bolt E4B). |
| **Little Super Nemo** | The larger model the hemisphere path is meant to take part in, from Nemotron 3.5 Super VL. |
| **MJHEMI2** | The hemisphere sample-set format: lesson or holdout evidence, bound by SHA-256 to the models that produced it. |
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
| **Writer token** | The `<ledger>.writer` file that gives one process at a time the right to append. |
