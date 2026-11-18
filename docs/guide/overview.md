# Overview

Mjolnir improves a model's weights through guided dreaming, with every change recorded, checked and reversible.

**The problem.** A model that plays a task (for now, LithoKraft mazes run through Bolt PCHIE) makes good moves and bad ones. We want it to get better from its own experience, without forgetting what it already knows, and without any change slipping in unrecorded or unchecked.

**The approach.** At night, or after something worth learning, the model dreams. A candidate change is worked out on a copy, scored with partial credit, and gated: it has to stay small, keep behavior on a golden set nearly unchanged, keep the golden score, and improve the task. Only then may it land. Every dream, grade and edit is a line in a hash-chained ledger, and every weight version is stored by its SHA-256 so any earlier version, including the untouched original, can be restored.

**The teacher stays in the loop.** JD labels data, grades outcomes and decides what counts as productive. The loop is guided recurrence, never full recursion.

## How the pieces fit

```
  recorded PCHIE runs (read-only)  ──►  replays + partial-credit labels
                                                 │
            JD's grades (labeling tool) ─────────┤
                                                 ▼
  live weights ──snapshot──► dream on a copy ──► score ──► gates ──► candidate
       ▲                                                                │
       └──────────── integrate only if every gate passes ◄──────────────┘
                     (otherwise discard; the reason becomes a note for the next dream)

  every step ──► hash-chained ledger        every version ──► version store (SHA-256)
```

## Stages

| Stage | Status | What it covers |
|---|---|---|
| M0 | In this repository | Foundations on toy weights: store, ledger, scorer, update rule, dream drivers. Dry runs only. |
| M1 | In this repository | Labeling tool, real replays, GPU toy training, real-weight edits on a working copy. |
| M2 | Planned | A live PCHIE watching and verifying edits on the working copy before they count. See the [roadmap](../ROADMAP.md). |

## What Mjolnir is not

- It is not a training framework for large models. The real-weight path makes small, low-rank, gated edits.
- It does not touch PCHIE's official runners or records. An edited model stays in its working copy.
- It does not store weights, run data or ledgers in this repository.
- It does not give the model tools. The model only plays the game it is being taught.

Next: [Architecture](architecture.md).
