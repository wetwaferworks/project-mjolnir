# The real-weight edit path

The path works on a **working copy** of a model, never on the original download. The full reference is [M1.md, section 4](../M1.md); this page is the short version.

```
cd python
python -m mjolnir real-register --original ORIGINAL --working WORKING --data DATA_FOLDER
python -m mjolnir real-dream    --working WORKING --data DATA_FOLDER --runs RUN_FOLDER [--layers 40 41] [--rank 16] [--strengths 0.02 0.05 0.1 0.2]
python -m mjolnir real-apply    --original ORIGINAL --working WORKING --data DATA_FOLDER --version ID
python -m mjolnir real-eval     --original ORIGINAL --working WORKING --data DATA_FOLDER --runs RUN_FOLDER
python -m mjolnir real-verify   --dir DIR --sums FILE
```

The same register, apply and verify steps run in C with `mj_real` (no model code; the apply uses the GPU when built with `-DMJ_CUDA=ON`, else the CPU, with the same bytes):

```
mj_real register --original ORIGINAL --working WORKING --data DATA_FOLDER
mj_real apply    --original ORIGINAL --working WORKING --data DATA_FOLDER --version ID [--cpu] [--threads N]
mj_real verify   --dir DIR --sums FILE
mj_real status   --working WORKING
```

1. **Register** checksums every file of the original, copies it, checks the copy byte for byte, marks the original read-only, and pins a base record (MJBASE1) in the store. The working folder must be separate and not a hard link.
2. **Dream** works out a reward-weighted, low-rank edit to the MLP output projection of the chosen layers, tries each strength in memory only, and stores the strongest passing one as an MJDELTA1 candidate. Nothing is written to the working copy.
3. **Apply** restores touched tensors to the original bytes, adds the version's deltas oldest first, writes only those tensors, and reads them back. `--version base` rolls back to a byte-for-byte copy of the original.
4. **Evaluate** scores original and working copy as they are on disk.
5. **Verify** compares a folder against a checksum list (`OK n files match` or `MISMATCH`).

## Gates

- Golden set: KL divergence from the unedited model at most 0.02 per token, loss at most 2% higher, top-1 agreement at least 97%.
- Task: the score rises on the examples learned from and does not fall on held-out runs.
- No trade-offs: good steps keep their accuracy and likelihood, and the chance of repeating a loop does not rise.

## The method

A first-order, low-rank form of rank-one model editing after ROME (Meng, Bau, Andonian and Belinkov, "Locating and Editing Factual Associations in GPT", NeurIPS 2022, arXiv:2202.05262), reward-weighted per design principle 3. Each example's key is whitened by the covariance of the projection's inputs over ordinary text, applied through the Woodbury identity, so the edit follows what differs between maze steps and stays quiet elsewhere. The derivation is in the docstring of `python/mjolnir/real/edit.py`.

## Memory

Weights stay in the file, mapped read-only. Embedding tables are read row by row on the processor, and each decoder layer is copied to the GPU just before it runs. A dream with four trial strengths fits in 4 GB of graphics memory. No code from a model download is ever run.
