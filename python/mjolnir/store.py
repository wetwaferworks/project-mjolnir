"""The weight version store, in the same layout and formats as mj_store.c.

    <store>/HEAD                the live version
    <store>/objects/<sha>.blob  each payload once, named by its SHA-256
    <store>/versions/<id>.ver   one immutable manifest per version
    <store>/retired/<id>.ver    manifests retired by retention (kept as record)
    <store>/pins/<id>.pin       pinned versions

A version id can never be written twice. Every write is atomic and read
back. Payloads that look like whole model files are refused here as in C:
real-weight edits are stored as Mjolnir deltas (see real/delta.py), never as
copies of the model.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import re
import shutil
import struct

from .common import Corrupt, MjError, Refused, crc32, id_valid, parse_u64, sha256_hex, text_valid, write_atomic

KINDS = ("snapshot", "nightly", "milestone", "candidate")
MANIFEST_MAX = 1024
_HEX64 = re.compile(r"[0-9a-f]{64}")
_HEX8 = re.compile(r"[0-9a-f]{8}")


def payload_is_model_file(data):
    """Same test as mj_payload_is_model_file."""
    if data[:4] == b"GGUF" or data[:4] == b"PK\x03\x04":
        return True
    if len(data) >= 9:
        hl = struct.unpack("<Q", data[:8])[0]
        if 2 <= hl <= len(data) - 8 and data[8:9] == b"{":
            return True
    return False


def format_manifest(v):
    body = "MJVER1\nid=%s\nseq=%d\nkind=%s\nsha256=%s\nsize=%d\ncrc32=%08x\nparent=%s\nlabel=%s\n" % (
        v["id"], v["seq"], v["kind"], v["sha256"], v["size"], v["crc32"], v["parent"], v["label"])
    return (body + "mcrc=%08x\n" % crc32(body.encode("ascii"))).encode("ascii")


def parse_manifest(data):
    if not data or len(data) > MANIFEST_MAX or not data.endswith(b"\n"):
        raise Corrupt("manifest size or ending")
    try:
        lines = data.decode("ascii").split("\n")[:-1]
    except UnicodeDecodeError:
        raise Corrupt("manifest is not ASCII")
    keys = ("id", "seq", "kind", "sha256", "size", "crc32", "parent", "label", "mcrc")
    if len(lines) != 10 or lines[0] != "MJVER1":
        raise Corrupt("manifest layout")
    v = {}
    for k, line in zip(keys, lines[1:]):
        if not line.startswith(k + "="):
            raise Corrupt("manifest field order")
        v[k] = line[len(k) + 1:]
    seq, size = parse_u64(v["seq"]), parse_u64(v["size"])
    if (not id_valid(v["id"]) or seq is None or v["kind"] not in KINDS or not _HEX64.fullmatch(v["sha256"])
            or size is None or not _HEX8.fullmatch(v["crc32"]) or not (v["parent"] == "-" or id_valid(v["parent"]))
            or not text_valid(v["label"]) or not _HEX8.fullmatch(v["mcrc"])):
        raise Corrupt("manifest field value")
    body_end = data.rfind(b"mcrc=")
    if crc32(data[:body_end]) != int(v["mcrc"], 16):
        raise Corrupt("manifest checksum")
    out = {"id": v["id"], "seq": seq, "kind": v["kind"], "sha256": v["sha256"], "size": size,
           "crc32": int(v["crc32"], 16), "parent": v["parent"], "label": v["label"]}
    if format_manifest(out) != data:
        raise Corrupt("manifest not canonical")
    return out


class Store:
    def __init__(self, root, max_store_bytes=1 << 30, max_blob_bytes=64 << 20, min_free_bytes=4 << 30):
        if max_blob_bytes > max_store_bytes:
            raise MjError("blob ceiling above store ceiling")
        self.root = root
        self.max_store_bytes, self.max_blob_bytes, self.min_free_bytes = max_store_bytes, max_blob_bytes, min_free_bytes
        for d in ("objects", "versions", "retired", "pins"):
            os.makedirs(os.path.join(root, d), exist_ok=True)

    def _p(self, *parts):
        return os.path.join(self.root, *parts)

    def _manifests(self, sub):
        out = []
        for name in os.listdir(self._p(sub)):
            if name.endswith(".ver") and id_valid(name[:-4]):
                out.append(self._read_manifest(sub, name[:-4]))
        return out

    def _read_manifest(self, sub, vid):
        with open(self._p(sub, vid + ".ver"), "rb") as f:
            v = parse_manifest(f.read())
        if v["id"] != vid:
            raise Corrupt("manifest id does not match its file name")
        return v

    def versions(self):
        return sorted(self._manifests("versions"), key=lambda v: v["seq"])

    def next_seq(self):
        seqs = [v["seq"] for v in self._manifests("versions") + self._manifests("retired")]
        return (max(seqs) if seqs else 0) + 1

    def used_bytes(self):
        d = self._p("objects")
        return sum(os.path.getsize(os.path.join(d, n)) for n in os.listdir(d) if n.endswith(".blob"))

    def ever_used(self, vid):
        return os.path.exists(self._p("versions", vid + ".ver")) or os.path.exists(self._p("retired", vid + ".ver"))

    def info(self, vid):
        if not id_valid(vid):
            raise MjError("bad version id")
        return self._read_manifest("versions", vid)

    def put(self, vid, kind, parent, label, data):
        data = bytes(data)
        if not id_valid(vid) or kind not in KINDS or not text_valid(label) or not (parent == "-" or id_valid(parent)):
            raise MjError("bad version id, kind, parent or label")
        if len(data) > self.max_blob_bytes:
            raise MjError("payload is larger than the blob ceiling")
        if payload_is_model_file(data):
            raise Refused("whole model files are not stored; store a Mjolnir delta instead")
        if self.ever_used(vid):
            raise FileExistsError("version id already used: " + vid)
        if parent != "-":
            self.info(parent)
        sha = sha256_hex(data)
        blob = self._p("objects", sha + ".blob")
        if not os.path.exists(blob):
            if self.used_bytes() + len(data) > self.max_store_bytes:
                raise MjError("store ceiling would be passed")
            if shutil.disk_usage(self.root).free - len(data) < self.min_free_bytes:
                raise MjError("free-disk floor would be passed")
            write_atomic(blob, data)
        v = {"id": vid, "seq": self.next_seq(), "kind": kind, "sha256": sha, "size": len(data),
             "crc32": crc32(data), "parent": parent, "label": label}
        write_atomic(self._p("versions", vid + ".ver"), format_manifest(v))
        if self.info(vid) != v:
            raise Corrupt("manifest read-back mismatch")
        self.get(vid)
        if kind == "milestone":
            self.pin(vid)
        return v

    def get(self, vid):
        v = self.info(vid)
        if v["size"] > self.max_blob_bytes:
            raise MjError("payload is larger than the blob ceiling")
        try:
            with open(self._p("objects", v["sha256"] + ".blob"), "rb") as f:
                data = f.read(self.max_blob_bytes + 1)
        except FileNotFoundError:
            raise Corrupt("blob missing for " + vid)
        if len(data) != v["size"] or crc32(data) != v["crc32"] or sha256_hex(data) != v["sha256"]:
            raise Corrupt("blob check failed for " + vid)
        return data

    def head(self):
        try:
            with open(self._p("HEAD"), "rb") as f:
                data = f.read(256)
        except FileNotFoundError:
            return None
        lines = data.decode("ascii", "replace").split("\n")
        if len(lines) != 4 or lines[0] != "MJHEAD1" or not lines[1].startswith("id=") or not lines[2].startswith("crc="):
            raise Corrupt("HEAD layout")
        body = ("MJHEAD1\n%s\n" % lines[1]).encode("ascii")
        if "%08x" % crc32(body) != lines[2][4:] or not id_valid(lines[1][3:]):
            raise Corrupt("HEAD checksum")
        return lines[1][3:]

    def set_head(self, vid):
        self.get(vid)  # verify before it can go live
        body = ("MJHEAD1\nid=%s\n" % vid).encode("ascii")
        write_atomic(self._p("HEAD"), body + ("crc=%08x\n" % crc32(body)).encode("ascii"), overwrite=True)
        if self.head() != vid:
            raise Corrupt("HEAD read-back mismatch")

    rollback = set_head

    def pin(self, vid):
        self.info(vid)
        p = self._p("pins", vid + ".pin")
        if not os.path.exists(p):
            write_atomic(p, b"pinned\n")

    def chain(self, vid):
        """The version and its parents, oldest first."""
        out, seen = [], set()
        while vid != "-":
            if vid in seen:
                raise Corrupt("parent loop at " + vid)
            seen.add(vid)
            v = self.info(vid)
            out.append(v)
            vid = v["parent"]
        return out[::-1]

    def verify_all(self):
        n = 0
        for v in self.versions():
            self.get(v["id"])
            n += 1
        return n
