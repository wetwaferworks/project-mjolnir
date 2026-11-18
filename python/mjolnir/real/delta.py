"""MJDELTA1: a low-rank weight edit, small enough for the version store.

    MJDELTA1\n
    base=<64 hex: SHA-256 of the model file the edit was computed on>\n
    count=<number of entries>\n
    then per entry: tensor=<name> rows=<r> cols=<c> rank=<k> scale=<float>\n
    end\n
    then per entry, little-endian float32: U (rows x rank), then V (rank x cols)
    then the SHA-256 of every byte before it (32 bytes)

The change to a tensor is scale * U @ V. The file is not a model file, so the
store's refusal of whole model files does not get in its way.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import hashlib
import re
import struct

from ..common import Corrupt

MAX_ENTRIES = 256
MAX_RANK = 64
_ENTRY = re.compile(r"tensor=([A-Za-z0-9._]{1,200}) rows=([1-9][0-9]{0,5}) cols=([1-9][0-9]{0,5}) "
                    r"rank=([1-9][0-9]{0,2}) scale=([-+0-9.eE]{1,32})")


def pack(base_sha, entries):
    """entries: list of (name, U, V, scale) with U, V float32 torch tensors or nested lists."""
    import torch
    if not re.fullmatch(r"[0-9a-f]{64}", base_sha) or not 0 < len(entries) <= MAX_ENTRIES:
        raise ValueError("bad base hash or entry count")
    head = ["MJDELTA1", "base=" + base_sha, "count=%d" % len(entries)]
    body = b""
    for name, U, V, scale in entries:
        U = torch.as_tensor(U, dtype=torch.float32).contiguous()
        V = torch.as_tensor(V, dtype=torch.float32).contiguous()
        r, k = U.shape
        k2, c = V.shape
        if k != k2 or k > MAX_RANK:
            raise ValueError("rank mismatch or too large")
        head.append("tensor=%s rows=%d cols=%d rank=%d scale=%r" % (name, r, c, k, float(scale)))
        body += U.numpy().astype("<f4").tobytes() + V.numpy().astype("<f4").tobytes()
    data = ("\n".join(head) + "\nend\n").encode("ascii") + body
    return data + hashlib.sha256(data).digest()


def unpack(data):
    import numpy as np
    import torch
    if len(data) < 64 or hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise Corrupt("delta checksum")
    end = data.find(b"\nend\n")
    if end < 0 or end > 65536:
        raise Corrupt("delta header")
    lines = data[:end].decode("ascii").split("\n")
    if lines[0] != "MJDELTA1" or not lines[1].startswith("base=") or not lines[2].startswith("count="):
        raise Corrupt("delta header layout")
    base = lines[1][5:]
    count = int(lines[2][6:])
    if not re.fullmatch(r"[0-9a-f]{64}", base) or not 0 < count <= MAX_ENTRIES or len(lines) != 3 + count:
        raise Corrupt("delta header values")
    pos, out = end + 5, []
    for line in lines[3:]:
        m = _ENTRY.fullmatch(line)
        if not m:
            raise Corrupt("delta entry line")
        name, r, c, k, scale = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), float(m.group(5))
        nu, nv = r * k * 4, k * c * 4
        if pos + nu + nv > len(data) - 32:
            raise Corrupt("delta is shorter than its header says")
        U = torch.from_numpy(np.frombuffer(data, dtype="<f4", count=r * k, offset=pos).copy()).reshape(r, k)
        V = torch.from_numpy(np.frombuffer(data, dtype="<f4", count=k * c, offset=pos + nu).copy()).reshape(k, c)
        pos += nu + nv
        out.append((name, U, V, scale))
    if pos != len(data) - 32:
        raise Corrupt("delta has extra bytes")
    return base, out


def dense(entry):
    name, U, V, scale = entry
    return (U @ V) * scale
