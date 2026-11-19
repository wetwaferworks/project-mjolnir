# Models Mjolnir consumes

Mjolnir does not convert or pack models. It reads weights and, for the hemispheres, distributions over legal moves. The formats a real model may arrive in are built and documented in [Bolt PCHIE](https://github.com/wetwaferworks/bolt-pch-ie). This page only says how they relate to Mjolnir.

No model weights are in this repository. Little Nemo (NVIDIA Nemotron 3.5 Bolt E4B) is the model Mjolnir is developed against; Little Super Nemo (from Nemotron 3.5 Super VL, a Mamba-2, mixture-of-experts and attention hybrid) is the larger model the hemisphere path is meant to take part in. Each model has its own license; see [CREDITS.md](../../CREDITS.md).

## What PCHIE prepares, and what Mjolnir does with it

| Format | Where it is documented | How Mjolnir uses it |
|---|---|---|
| Ordinary safetensors (full precision or the vendor's quantisation, for example Q4) | The model's own layout; Mjolnir's reader is in [the real-weight path](real-weight-editing.md) and [the C port](../C-PORT.md) | The real-weight edit path registers a checksummed original, edits a working copy, and can roll back to the original byte for byte. |
| Ternary weights, and packed ternary (five weights per byte, with optional compact scales and entropy coding) | PCHIE, [ternary and packing](https://github.com/wetwaferworks/bolt-pch-ie/blob/main/docs/ternary/README.md) | A hemisphere can be "the same network at a different precision": a ternary copy as the left, the fuller-precision original as the right, or the other way around. Edits still go through a Mjolnir working copy, never the original. |
| NanoQuant (low-bit factors, including the first-stage CUDA path) | PCHIE, [NanoQuant](https://github.com/wetwaferworks/bolt-pch-ie/blob/main/docs/ternary/nanoquant.md) | Another packed form a real model may be stored in. Mjolnir consumes the weights; it does not run the quantiser. |

The hemisphere interface itself does not care how the weights are stored. A hemisphere is two functions: give a distribution over the legal moves of a state, and say what you are. The toy policy, a dense transformer, a mixture-of-experts model or a state-space hybrid all fit the same slot. See [the hemispheres](../hemispheres.md).

For a text model, that distribution is the next-token probabilities of the move names, renormalised over the legal moves. PCHIE is the world that plays the rounds and writes the sample sets; Mjolnir works out the scores and decides keep or roll back (`mj_hemi gate`).
