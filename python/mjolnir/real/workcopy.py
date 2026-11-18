"""The original download, its working copy, and applying stored edits.

  original/   the download. Never opened for writing; marked read-only.
  working/    a byte-for-byte copy. Edits are applied here only.

Applying a version restores every tensor any earlier edit touched to the
original bytes, then adds the version's chain of deltas, oldest first, and
writes only those tensors back into the working copy. So any version,
including the untouched base, can be checked out again at any time.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import shutil
import stat

from . import delta as mjdelta
from . import safet
from ..common import Corrupt, Refused, sha256_file, write_atomic

MODEL_FILE = "model-00001-of-00001.safetensors"


def checksums(folder):
    out = {}
    for name in sorted(os.listdir(folder)):
        p = os.path.join(folder, name)
        if os.path.isfile(p):
            out[name] = (os.path.getsize(p), sha256_file(p))
    return out


def format_sums(sums):
    return "".join("%s  %d  %s\n" % (h, n, name) for name, (n, h) in sorted(sums.items())).encode("ascii")


def parse_sums(data):
    out = {}
    for line in data.decode("ascii").splitlines():
        h, n, name = line.split("  ", 2)
        out[name] = (int(n), h)
    return out


def _guard(original, working):
    o, w = os.path.realpath(original), os.path.realpath(working)
    if o == w or w.startswith(o + os.sep) or o.startswith(w + os.sep):
        raise Refused("the working copy must be a separate folder from the original")


def make_copy(original, working):
    """Copy every file, verify it, and mark the original files read-only."""
    _guard(original, working)
    if os.path.exists(working) and os.listdir(working):
        raise Refused("the working folder is not empty")
    os.makedirs(working, exist_ok=True)
    before = checksums(original)
    for name in before:
        shutil.copyfile(os.path.join(original, name), os.path.join(working, name))
    after = checksums(working)
    if after != before:
        raise Corrupt("the copy does not match the original")
    for name in before:
        p = os.path.join(original, name)
        os.chmod(p, stat.S_IREAD)
    return before


def _model_paths(original, working):
    _guard(original, working)
    o, w = os.path.join(original, MODEL_FILE), os.path.join(working, MODEL_FILE)
    if os.path.samefile(o, w):
        raise Refused("the working model file is the original file")
    if os.stat(w).st_nlink != 1:
        raise Refused("the working model file is hard-linked; refusing to write through it")
    return o, w


def read_state(working):
    p = os.path.join(working, "MJOLNIR-STATE.txt")
    if not os.path.exists(p):
        return {"applied": "-", "touched": []}
    lines = open(p, "rb").read().decode("ascii").splitlines()
    if not lines or lines[0] != "MJWORK1" or not lines[1].startswith("applied="):
        raise Corrupt("working copy state file")
    return {"applied": lines[1][8:], "touched": [l[8:] for l in lines[2:] if l.startswith("touched=")]}


def write_state(working, applied, touched):
    text = "MJWORK1\napplied=%s\n" % applied + "".join("touched=%s\n" % t for t in sorted(touched))
    write_atomic(os.path.join(working, "MJOLNIR-STATE.txt"), text.encode("ascii"), overwrite=True)


def _write_tensor(fh, info, t):
    import torch
    data = t.to(torch.bfloat16).contiguous().view(torch.uint16).numpy().astype("<u2").tobytes()
    if len(data) != info["nbytes"]:
        raise Corrupt("tensor size changed")
    fh.seek(info["abs_offset"])
    fh.write(data)
    return data


def checkout(store, version_id, original, working, base_sha):
    """Make the working copy equal to the original plus the version's delta chain."""
    import torch
    o_file, w_file = _model_paths(original, working)
    header, _ = safet.read_header(o_file)
    chain = [v for v in store.chain(version_id) if v["kind"] != "milestone" or not store.get(v["id"]).startswith(b"MJBASE1")]
    sums = {}
    for v in chain:
        base, entries = mjdelta.unpack(store.get(v["id"]))
        if base != base_sha:
            raise Refused("version %s was computed on a different model file" % v["id"])
        for e in entries:
            if e[0] not in header or header[e[0]]["dtype"] != "BF16":
                raise Refused("delta names a tensor the model does not have: " + e[0])
            d = mjdelta.dense(e)
            if list(d.shape) != header[e[0]]["shape"]:
                raise Refused("delta shape does not match " + e[0])
            sums[e[0]] = sums.get(e[0], 0) + d
    state = read_state(working)
    touched = set(state["touched"]) | set(sums)
    written = {}
    with open(w_file, "r+b") as fh:
        for name in sorted(touched):
            orig = safet.read_tensor(o_file, header[name])
            new = orig + sums[name] if name in sums else orig
            written[name] = _write_tensor(fh, header[name], new)
        fh.flush()
        os.fsync(fh.fileno())
    with open(w_file, "rb") as fh:
        for name, data in written.items():
            fh.seek(header[name]["abs_offset"])
            if fh.read(len(data)) != data:
                raise Corrupt("read-back mismatch in " + name)
    write_state(working, version_id, [n for n in touched if n in sums])
    return sorted(touched)
