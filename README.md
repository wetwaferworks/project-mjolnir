# Project Mjolnir

**Measured Judgment, Operational Language, Neural Intelligent Researcher.**
*The researcher that dreams.*

Mjolnir is the system that sits on top of Bolt PCHIE and improves a model's weights while it dreams. PCHIE is the seat: it runs whatever weights are plugged into it and checks every step they take. Mjolnir is the suit built around that seat. At night, or when there is something new worth learning, the model dreams about what it did. A changed copy of its weights is worked out inside the dream, scored, and then tested for real outside the dream. Only an edit that holds up outside the dream is allowed to land, and every dream is written into a hash chain, so the whole history of how the model changed can be replayed and checked by anyone.

This folder holds stage **M0**, the foundations. M0 never touches a real model. Everything here runs on a tiny made-up policy (eight inputs, four actions) so the rules can be proven before any real weights get near them.

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

The loop is guided, never fully self-recursive. A teacher stays in it: JD labels data, grades outcomes and decides what counts as productive.

## JD's decisions (November 15, 2026)

1. **Structure.** The four-layer picture above is how the system is built. Editing weights during dreams is the core, and it is the part that has to be right.
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

## What M0 contains

| Part | What it does |
|---|---|
| Weight version store (`mj_store`) | Saves every version once, named by its SHA-256. Takes a full snapshot before every dream. Writes go to a temporary file, are flushed to disk and then renamed into place, and every write is read back and checked. A version can never be overwritten, not even after it is retired. Rollback re-checks the old version before making it live. Keeps the newest 14 nightly versions, the newest 14 snapshots and 14 candidates, plus every pinned milestone and whatever is live. Retired versions keep their record. A storage ceiling and a free-disk floor are checked before anything is written. |
| Dream ledger (`mj_ledger`) | One line per dream or grade: the dream, when it ran, what started it, who graded it, which replays went in, the weights before and the candidate, an edit summary, the scores, the behavior shift, each gate's result, the outcome, and the failure note. Every line carries the hash of the line before it and its own hash. Any change, deletion, reordering or torn write is caught at the exact line, and nothing more can be appended until the chain is whole again. `mj_ledger_verify` recomputes the chain from the start. |
| Partial-credit scorer (`mj_score`) | Mazes are scored per segment: everything cleared before a failure is a success, the failure itself is "not yet", and anything after it is never graded. Circuits are scored per element: each working element is good and each faulty one is "not yet". Scores are integer basis points, so every machine gives the same answer. |
| Update rule (`mj_toy`) | Reward-weighted nudges on the toy policy: cleared segments strengthen the action taken and the "not yet" segment weakens it. Old memories are replayed in between the new ones. No weight may move more than a set cap in one dream. Four gates decide the result: the step cap held, the behavior shift on a golden set stayed under its limit, the golden score did not drop, and the task score went up. |
| Dream driver and nap (`mj_dream`, `mj_nightly`, `mj_nap`) | Checks the ledger, loads and checks the live weights, reads the last failure note and adjusts (half the step size and more old replay after a drift failure), takes the snapshot, dreams, scores, runs the gates, saves the candidate for the record, confirms the live weights did not move, and writes the ledger line. Both are dry runs in M0. The nightly run also applies the retention policy and re-checks the store. |

Everything is plain C17 with no outside libraries. There are no locks anywhere: M0 runs on one thread, and when work is split later it will hand off at phase boundaries or through atomics. Every input has a size limit and is checked before use, and anything malformed is refused rather than guessed at. Memory use per dream has a hard ceiling, and each dream has a step limit and a time limit. A dream that hits one leaves nothing behind but its ledger line.

## Building and testing

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Options: `-DMJ_SANITIZE=ON` builds with AddressSanitizer (and UndefinedBehaviorSanitizer on GCC and Clang). `-DMJ_LIBFUZZER=ON` adds libFuzzer targets when the compiler supports them. The test bench always includes the built-in fuzzer, which runs the same on every platform; `-DMJ_FUZZ_ITERATIONS=` sets how hard it works.

Try a dream by hand:

```
mj_nap --dry-run --seed-toy --store dream-store --ledger dreams.mjl --id first-nap
mj_nightly --dry-run --store dream-store --ledger dreams.mjl --id night-1
mj_ledger_verify dreams.mjl
```

## Stage M1

M1 brings in real material and the real model. The full guide is in [docs/M1.md](docs/M1.md). How Mjolnir will sign in to hosted models is in [docs/MODEL-ACCESS.md](docs/MODEL-ACCESS.md).

- **The labeling tool.** A page on this machine only that steps JD through recorded runs, least certain steps first. Big buttons and single keys (Enter to agree with the suggested label, 1 good, 2 bad, 3 productive, Space to skip, Backspace to go back), all nameable by voice. Every grade is a line in its own hash-chained ledger.
- **Real replays.** Recorded PCHIE research runs are read, never written, with every line's hash checked. Each step gets a partial-credit label: productive, good, or not yet.
- **Training on the GPU.** The toy policy now dreams on real replays and JD's grades, trained on the workstation GPU, with the M0 gates deciding the outcome and the result loadable by the M0 tools.
- **The real-weight edit path.** Works on a working copy of the model, never the original. An edit is worked out from replays, tried in memory against a golden set, stored as a small low-rank delta, written into the working copy only on request, and measured on disk against the original. Any version, including the untouched base, can be checked out again.

Open the labeling tool with:

```
python -m mjolnir label --runs RUN_FOLDER --data DATA_FOLDER
```

## What comes next (M2)

- Watch and verify with a live PCHIE on the edited working copy, outside the dream, before an edit counts as integrated.
- JD's grades feeding the real-weight dreams, not only the toy policy.
- Wider edits once the golden gates show how much the model can take.

## Where this repository came from

Mjolnir started as a folder inside the LithoKraft workspace. Its history was split out with `git subtree split`, so the first two commits here are the original M0 commits with the folder prefix removed. In the original workspace they were c1f0c59 and 9d93c6b; here they are ccebb0f and 4316300. Nothing else came with them.

## Credits

- Project Mjolnir, its design, the dreaming cycle and every decision above: **JD** (Jaydee Wetwork).
- Architecture and operation report: **Elfie** (Muse Wonder), registrar.
- Implementation brief (operational reasoning, planning, memory, preferences and orchestration): **Gizmo** and **Luna**.
- Contributors: **Helia**, and **Flash Gordon**, also known as **Quicksilver** (Gemini Flash Lite 3.5).
- The ledger of decisions, the dreaming distillation, and the M0 and M1 builds: **Cortana**.

## License

[MIT](LICENSE) © 2026 Jaydee Wetwork / Wet Wafer Works
