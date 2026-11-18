"""The toy policy from M0 (8 features, 4 actions, Q16 weights) on real replays.

The byte format matches mj_toy.c exactly, so a version written here can be
loaded by mj_nap and mj_nightly. Replay steps are turned into the eight toy
features so the toy model can be trained on what PCHIE actually did.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import struct

from .common import Corrupt, crc32

F, A = 8, 4
MAGIC = b"MJTOY1\x00\x00"
BLOB_LEN = 8 + 4 + A * F * 4 + 4
WEIGHT_LIMIT = 64 * 65536
FEATURES = ("open bit 0", "open bit 1", "open bit 2", "open bit 3", "has key", "standing on visited ground",
            "previous move", "bias")


def save(w):
    out = bytearray(MAGIC + bytes([F, A, 0, 0]))
    for a in range(A):
        for i in range(F):
            v = int(w[a][i])
            if abs(v) > WEIGHT_LIMIT:
                raise ValueError("toy weight out of range")
            out += struct.pack("<i", v)
    out += struct.pack("<I", crc32(bytes(out)))
    return bytes(out)


def load(buf):
    if len(buf) != BLOB_LEN or buf[:8] != MAGIC or buf[8] != F or buf[9] != A or buf[10:12] != b"\0\0":
        raise Corrupt("not a toy weights blob")
    if struct.unpack("<I", buf[-4:])[0] != crc32(buf[:-4]):
        raise Corrupt("toy weights checksum")
    vals = struct.unpack("<%di" % (A * F), buf[12:-4])
    w = [list(vals[a * F:(a + 1) * F]) for a in range(A)]
    if any(abs(v) > WEIGHT_LIMIT for row in w for v in row):
        raise Corrupt("toy weight out of range")
    return w


def act(w, x):
    best, best_v = 0, None
    for a in range(A):
        v = sum(w[a][i] * x[i] for i in range(F))
        if best_v is None or v > best_v:
            best, best_v = a, v
    return best


def features(steps):
    """One int8 feature vector per step that took one of the four moves."""
    out, visited, prev_move = [], set(), 0
    for s in steps:
        here_visited = s["state"] in visited
        visited.add(s["state"])
        o = int(s.get("open") or 0)
        x = [127 if (o >> b) & 1 else 0 for b in range(4)]
        x += [127 if s.get("key") else 0, 127 if here_visited else 0, prev_move * 32, 127]
        if s["action"] in (0, 1, 2, 3):
            out.append((x, s["action"], s))
            prev_move = s["action"]
    return out
