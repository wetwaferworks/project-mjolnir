# Documentation

The named stages in this repository are M0 and M1. Work after M1 (the C and CUDA port, the classroom and hemisphere swap, and the hardened ledger) is already in the tree and is listed as Unreleased in the [changelog](../CHANGELOG.md). The next named stage is M2, on the [roadmap](ROADMAP.md).

If you are new, start with the [README](../README.md), then the [guide](guide/README.md).

| Page | What it covers |
|---|---|
| [guide/](guide/README.md) | The guide: overview, architecture, getting started, build and test, the ledger, design principles, intended use and safety, labeling, real-weight edits, models and formats, glossary. |
| [FORMATS.md](FORMATS.md) | File formats: the version store, the dream ledger, toy weights, hemisphere sample sets (MJHEMI2). |
| [M1.md](M1.md) | The stage M1 guide and the M1 file formats (MJDELTA1, MJBASE1, MJWORK1, MJDIRTY1). These formats are still current; M1 is not the latest *work* in the tree. |
| [C-PORT.md](C-PORT.md) | Moving the Python code to C and CUDA: what is ported, the parity tests, where C is stricter, and which Python remains. |
| [MODEL-ACCESS.md](MODEL-ACCESS.md) | How Mjolnir signs in to hosted models: API keys in Windows Credential Manager, no OAuth. |
| [hemispheres.md](hemispheres.md) | The left and right hemispheres: the classroom and the hemisphere swap, with diagrams, guardrails and results. |
| [guide/models-and-formats.md](guide/models-and-formats.md) | Ternary, packed ternary and NanoQuant as model formats Mjolnir consumes, with links to the PCHIE docs. |
| [ROADMAP.md](ROADMAP.md) | What is done, what is unreleased in this tree, and what comes next. |
