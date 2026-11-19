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
| `replay.py` (run logs, chain check, steps, automatic labels, folder scan) | `mj_replay.h`, `mj_json.h`, `mj_m1 replays` | Ported. The same folders give byte-identical replay dumps. |
| `grades.py` | `mj_grades.h`, `mj_m1 grade`, `mj_m1 grades` | Ported. Same ledger records; each side reads the other's grades. |
| `toy.py`: `features`; `toytrain.py`; `split_runs` from `real/edit.py`; the `toy-seed` and `toy-nap` commands | `mj_toytrain.h`, `mj_m1 toy-seed`, `mj_m1 toy-nap` | Ported, on the processor (32 weights: a GPU would only add transfer time). Same loss, optimizer, clamps, gates, store records and ledger fields; the minibatches come from splitmix64 instead of PyTorch's generator (see below). |
| `labeler.py` (the local grading page) | | Not yet. |
| `__main__.py` | `mj_m1`, `mj_real` | The replay, grade and toy commands and the real-weight commands without model code have C versions; `label`, `real-dream` and `real-eval` do not. |

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

`python/tests/test_c_port_m1.py` (ctest `py_c_port_m1`) does the same for
replays, grades and the toy nap:

- **Replays**: a folder of run logs goes through `replay.load_all` and
  `mj_m1 replays-json`, and both are written as
  `json.dumps(sort_keys=True, separators=(",", ":"))`. The texts must be
  identical, and so must the "skipped" messages and the `replays` summary
  text. The folder is awkward on purpose: repeated keys, `1`, `1.0` and
  `true` used as the same value, `-0.0`, a 23-digit token, non-ASCII text
  and file names, CRLF and blank lines, a hidden folder, a copy of a run, a
  broken hash, a log without a start record, a long first line. Measured: 6
  runs, 35 steps, 2 skipped, 12,232 identical bytes.
- **Grades**: Python and C each add grades to one ledger; each side's
  "latest grade per step" equals the other's, and a C record equals the
  record Python writes for the same grade, field for field apart from the
  time.
- **Toy nap**: C and Python draw minibatches differently, so their
  candidates differ. The test replays a 40-step C nap in PyTorch with the
  very same minibatch indices (measured: 0 Q16 units apart on Linux, at most
  1 on Windows, where the math library's `expf` differs; the test allows 4), then
  recomputes every gate number of a full 300-step C nap in Python from the C
  candidate (task, golden and shift scores, largest move, sample counts,
  gate results, ledger fields): all equal. Finally Python and C nap on
  one store and ledger in turn. Over 300 steps the two float32 sums drift
  apart (a few tenths of a weight unit) because momentum carries rounding
  differences forward, which is why the weights are compared on the short
  run.

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
  key error);
- run logs that are not UTF-8 (Python also detects UTF-16 and UTF-32), that
  use `NaN` or `Infinity`, or that nest deeper than 512 levels (Python stops
  with a recursion error at about 1000);
- run logs where Python stops with an uncaught error instead of skipping
  the file: a record that is not an object, a reward that is not a number,
  proposals that are null or a number, a list used as a state, a decision
  ended without being advised, and an "open" that is a non-empty list or
  text that is not a number (C refuses these with a message);
- symbolic links and junctions in a run folder (Python's glob follows them;
  C skips them), folders more than 32 deep, and more than 4,096 run logs in
  one scan.

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
- `replay.py`, `grades.py` and `toytrain.py`, and in `__main__.py` the
  `replays`, `toy-seed` and `toy-nap` commands. While `labeler.py` and
  `real/edit.py` stay, they still import `replay` and `grades` (the dream
  reads replays to build its examples), so these files go only when those
  do.
- `toy.py`: `features`; `load`, `save` and `act` were already in C
  (`mj_toy.h`) and are used by the Python nap and its tests.

## Building

```
cmake -S . -B build -DMJ_CUDA=ON          # or without MJ_CUDA for the CPU path only
cmake --build build --config Release
ctest --test-dir build -C Release
```
