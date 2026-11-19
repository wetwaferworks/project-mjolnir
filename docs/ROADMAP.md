# Roadmap

Nothing here is promised by a date. The safeguards in [intended use and safety](guide/intended-use-and-safety.md) hold throughout.

The named stages in this repository are M0 and M1. Work after M1 that is already in the tree is listed first as "in this tree, unreleased," which matches the [changelog](../CHANGELOG.md). The next *named* stage is still M2.

## Done

- **M0**: foundations on toy weights (store, ledger, scorer, update rule, nightly and nap drivers, fuzzed test bench).
- **M1**: labeling tool, read-only real replays, toy training from those replays, and the real-weight edit path on a working copy.

## In this tree, unreleased

These are built, tested and documented. They have not been given a new stage number.

- The C and CUDA port of the M1 paths: `mj_m1`, `mj_real`, the dirty marker, parity tests against Python. See [the C port](C-PORT.md).
- The classroom and the hemisphere swap on toy policies, the MJHEMI2 sample sets, the gate for real models, and the journal. See [the hemispheres](hemispheres.md).
- The hardened dream ledger: one writer at a time, a token file, torn-tail repair, and `--expect` to pin a head. See [the ledger](guide/the-ledger.md).

## M2 (next named stage)

- Watch and verify with a live PCHIE on the edited working copy, outside the dream, before an edit counts as integrated.
- Hand-labeled grades feeding the real-weight dreams, not only the toy policy.
- Wider edits once the golden gates show how much the model can take.
- The hemispheres on real models: a ternary and a full-precision copy of the same network as the left and right, playing PCHIE worlds, with edits on a working copy only. Little Super Nemo (from Nemotron 3.5 Super VL) is the large model this path is meant for; Little Nemo remains the smaller test bed.

## Later

- Evaluation loops aimed at public benchmarks, including [Artificial Analysis](https://artificialanalysis.ai/), so a dreamed edit can be compared on a known suite. No benchmark numbers are claimed here yet.
- Hosted-model help with grading or comparing answers, signed in with API keys only (see [model access](MODEL-ACCESS.md)).
- Memory and preference tiers (layer D3) and the planning coordinator (layer D4) described in the [architecture](guide/architecture.md).
- Dream material beyond mazes: circuit experiments and tests (design principle 6).
