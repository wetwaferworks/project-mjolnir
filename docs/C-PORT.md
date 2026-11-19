# The C port

Mjolnir's Python code is being moved to C (and CUDA where the GPU helps), one
module at a time, with the same file formats. A Python module stays until its
C replacement has passed parity tests against it; only then can it be
considered for removal, and that is a separate, explicit decision.

## Where it stands

| Python | C | Status |
|---|---|---|
| `real/safet.py` (header, offsets) | `mj_safet.h` | Ported. Every offset, size and shape is checked against the file first (see below). |
| `real/delta.py` (MJDELTA1) | `mj_delta.h` | Ported. Same bytes for the same entries. |
| `real/workcopy.py` (checksums, copy, state, checkout) | `mj_workcopy.h`, `mj_apply.h` | Ported, with a dirty marker for in-place writes. The apply runs on the GPU when built with `-DMJ_CUDA=ON`. |
| `real/session.py`: `register`, `apply`, `base_sha`; `real-verify` | `mj_session.h`, `mj_real` | Ported. Same store records, state file and ledger fields. |
| `store.py`, `ledger.py` | `mj_store.h`, `mj_ledger.h` | C was already the reference; Python reads and writes the same files. |
| `real/edit.py`: `whiten`, `with_shared_key`, `low_rank` | `mj_lowrank.h`, `mj_real lowrank` | Ported. The three big products with the background keys run through cuBLAS when built with CUDA; Cholesky, Householder QR and Jacobi SVD run on the CPU. All in double. |
| `real/edit.py`: gradients, background keys, the Evaluator, the strength trials | | Stays with the model code (they run the model). |
| `real/model.py`, `session.dream`, `session.evaluate` | | Stays in Python for now: it runs the Gemma forward and backward passes and the tokenizer through the transformers library. |
| `replay.py`, `grades.py`, `toytrain.py`, `labeler.py` | | Not yet. |

## Parity

`python/tests/test_c_port.py` (ctest `py_c_port`) runs the same steps both
ways on a small synthetic model and compares what lands on disk:

- **Delta files**: Python packs 134 entries with awkward scales (`1e-05`,
  `1e+16`, `-0.0`, `5e-324`, the largest double, random magnitudes from
  1e-30 to 1e30); C unpacks and packs them again. The bytes must be equal.
- **Register**: the base record, the state file and the working copy are
  byte-identical; ledger records match field for field apart from the time
  and the hashes that depend on it.
- **Apply** of a two-version chain (three deltas, rank 16 and 4): the state
  files are byte-identical, untouched tensors are byte-identical, and the
  edited weights are compared element by element.
- **Rollback** to `base` gives the original file byte for byte, both ways.
- Each side applies onto a working copy the other one made, and the C ledger
  checker accepts both ledgers.
- **Low-rank edit**: the same keys, outputs, gradients and background go
  through Python's `low_rank` (with `with_shared_key`) and `mj_real lowrank`;
  the products `U V` are compared (`U` and `V` alone are only defined up to
  the sign of each singular direction). Plain, whitened, and whitened at
  full rank: relative differences of 1.5e-6 to 3e-6, from Python's float32
  against C's double. The test allows 1e-4.

**Tolerance for the edited weights.** Python computes `U @ V` with a float32
matrix multiply whose summation order belongs to the math library. C sums
each dot product in double, in order, and rounds once to float; then both
multiply by the scale in float, add the terms in chain order in float, add
to the original weight in float, and round to bf16 (to nearest, ties to
even). So the two agree except where a value lands on a bf16 rounding
boundary, and then they differ by one bf16 step. The test allows at most one
step on at most 1% of the elements. Measured: 2 of 46,176 elements differ,
by one step, on Windows with CUDA (3 of 46,176 on Linux, CPU).

**CPU and GPU.** The CPU path and the CUDA kernel do the same operations in
the same order with no fused multiply-add (the kernel uses explicitly
rounded intrinsics and is built with `-fmad=false`; the C code is built
without floating-point contraction). They give the same bytes:
`test_workcopy` checks 264,967 elements with three terms of rank 4 to 16,
0 differ.

## Hardening carried into the C code

- **Safetensors header (M2).** The header length must fit the file and the
  100 MiB ceiling. Each tensor needs `begin <= end <= data size`,
  `end - begin` equal to the element count times the dtype size, whole
  non-negative dimensions (at most 8) whose product does not overflow, no
  overlap with another tensor, and a name used once. The JSON is parsed by a
  small strict reader: no unknown keys in a tensor entry, no NUL or lone
  surrogates in names, valid UTF-8, nothing but padding after the object.
  The working model file must also carry the very same header bytes as the
  original before anything is written at those offsets.
- **Temporary files (L1).** Atomic writes use `<path>.<pid>.<n>.tmp`,
  created exclusively, so two writers never share or delete each other's
  temporary.
- **Dirty marker (L2).** `MJOLNIR-DIRTY.txt` (format in [M1.md](M1.md))
  goes down before the first in-place write and comes off after read-back
  and the state update. A cut-short write is reported by `mj_real status`
  and repaired by the next apply.
- **Register guard.** C register refuses a working folder that is, contains
  or sits inside the original before doing anything. The Python register
  only checked this on the copy path: pointed at the original folder itself,
  it finds an "identical copy" and writes its state file into the original
  folder (the model file is not touched, and a later apply is refused).
- **Ledger appends (M1) are unchanged**: verify, append, verify again, as
  in Python. Two writers appending to one ledger at the same moment are not
  kept apart; run one session per data folder. Closing that gap needs a
  design choice and is left open here.

## Where C is stricter than Python

C refuses some inputs Python would accept. None of them is ever written by
either side:

- a delta scale that overflows to infinity (`1e999`) or a count with a sign
  or leading zero; and packing a zero dimension or a header longer than
  64 KiB (Python would write a file its own reader refuses);
- safetensors headers with unknown keys in a tensor entry, fractional or
  negative dimensions, or more than 8 dimensions;
- file names outside printable ASCII in a sums file (Python fails on these
  when it encodes the sums), and symbolic links in a checksummed folder
  (Python follows them; C lists regular files only);
- state files naming tensors the model does not have (Python fails with a
  key error).

A NaN computed by an edit becomes the bf16 quiet NaN `0x7FC0` (torch's
scalar rule); real weights and edits never produce one.

## Python that is now redundant

If removal is approved, these could go, since C does the same job and the
parity tests above cover it. Nothing has been deleted.

- `real/safet.py`: `read_header` only. `map_tensor` and `read_tensor` are
  still used by `model.py`, so the file stays while the dream does.
- `real/delta.py`: `unpack` and `dense` for applying; `pack` is still used
  by the dream.
- `real/workcopy.py`: all of it (checksums, copy, state, checkout).
- `real/edit.py`: `whiten` and `low_rank` (`with_shared_key` is two lines
  and goes with them), once the dream calls the C version.
- `real/session.py`: `register`, `apply`, `base_sha`, and the `real-register`,
  `real-apply` and `real-verify` commands in `__main__.py`.

## Building

```
cmake -S . -B build -DMJ_CUDA=ON          # or without MJ_CUDA for the CPU path only
cmake --build build --config Release
ctest --test-dir build -C Release
```
