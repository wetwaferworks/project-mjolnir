"""The dream ledger, in the same MJL1 format as mj_ledger.c.

One record per line, eighteen '|' separated fields. Every line carries the
hash of the line before it and its own SHA-256 over every byte before
"|hash". Appends are refused while the chain is broken, and every append is
verified again after it reaches the disk.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import re

from .common import Corrupt, MjError, append_durable, id_valid, parse_u64, sha256_hex, text_valid, utc_now

RECORD_MAX = 4096
FILE_MAX = 64 * 1024 * 1024
INPUTS_MAX = 16
SHORT_MAX = 24
OUTCOMES = ("integrated", "dry-run-pass", "rejected", "aborted", "rolled-back", "grade")
ZERO = "0" * 64
FIELDS = ("dream", "when", "trigger", "grader", "inputs", "before", "candidate", "edit",
          "score_before", "score_after", "shift_bp", "gates", "outcome", "notes")
_WHEN = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")
_SHORT = re.compile(r"[a-z0-9-]{1,%d}" % SHORT_MAX)
_HEX64 = re.compile(r"[0-9a-f]{64}")


class ChainBroken(MjError):
    def __init__(self, line, why):
        MjError.__init__(self, "ledger chain broken at line %d: %s" % (line, why))
        self.line = line


def _inputs_valid(s):
    if s == "-":
        return True
    if not s or not text_valid(s):
        return False
    parts = s.split(",")
    return len(parts) <= INPUTS_MAX and all(id_valid(p) for p in parts)


def _id_or_dash(s):
    return s == "-" or id_valid(s)


def record(**kw):
    """A record with the same defaults as mj_ledger_record_init."""
    r = {"dream": "", "when": utc_now(), "trigger": "test", "grader": "", "inputs": "-", "before": "-",
         "candidate": "-", "edit": "", "score_before": 0, "score_after": 0, "shift_bp": 0, "gates": "",
         "outcome": "aborted", "notes": ""}
    for k, v in kw.items():
        if k not in r:
            raise KeyError(k)
        r[k] = v
    return r


def check_fields(r):
    """Raise MjError naming the first field that the C parser would refuse."""
    checks = (
        ("dream", id_valid(r["dream"])), ("when", isinstance(r["when"], str) and _WHEN.fullmatch(r["when"])),
        ("trigger", isinstance(r["trigger"], str) and _SHORT.fullmatch(r["trigger"])),
        ("grader", id_valid(r["grader"])), ("inputs", _inputs_valid(r["inputs"])),
        ("before", _id_or_dash(r["before"])), ("candidate", _id_or_dash(r["candidate"])),
        ("edit", text_valid(r["edit"])),
        ("score_before", isinstance(r["score_before"], int) and 0 <= r["score_before"] <= 10000),
        ("score_after", isinstance(r["score_after"], int) and 0 <= r["score_after"] <= 10000),
        ("shift_bp", isinstance(r["shift_bp"], int) and 0 <= r["shift_bp"] <= 10000),
        ("gates", text_valid(r["gates"])), ("outcome", r["outcome"] in OUTCOMES), ("notes", text_valid(r["notes"])),
    )
    for name, ok in checks:
        if not ok:
            raise MjError("ledger field %r is not valid: %r" % (name, r.get(name)))


def _body(seq, prev, r):
    return "MJL1|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%s|%s|%s" % (
        seq, prev, r["dream"], r["when"], r["trigger"], r["grader"], r["inputs"], r["before"], r["candidate"],
        r["edit"], r["score_before"], r["score_after"], r["shift_bp"], r["gates"], r["outcome"], r["notes"])


def format_line(seq, prev, r):
    check_fields(r)
    body = _body(seq, prev, r)
    line = body + "|" + sha256_hex(body.encode("ascii")) + "\n"
    if len(line) > RECORD_MAX + 1:
        raise MjError("ledger record too long")
    return line.encode("ascii")


def parse_line(line):
    """Parse one line without its newline. Returns (seq, prev, record, hash)."""
    if not line or len(line) > RECORD_MAX:
        raise Corrupt("bad length")
    if any(c < 0x20 or c > 0x7E for c in line):
        raise Corrupt("non-printable byte")
    s = line.decode("ascii")
    f = s.split("|")
    if len(f) != 18 or f[0] != "MJL1":
        raise Corrupt("wrong field count or tag")
    seq = parse_u64(f[1])
    if not seq or not _HEX64.fullmatch(f[2]) or not _HEX64.fullmatch(f[17]):
        raise Corrupt("bad seq or hash field")
    nums = [parse_u64(x) for x in f[11:14]]
    if any(n is None for n in nums):
        raise Corrupt("bad number")
    r = dict(zip(FIELDS, f[3:11] + nums + f[14:17]))
    check_fields(r)
    if sha256_hex(s[: len(s) - 65].encode("ascii")) != f[17]:
        raise Corrupt("hash does not match")
    if format_line(seq, f[2], r) != line + b"\n":
        raise Corrupt("not in canonical form")
    return seq, f[2], r, f[17]


def read(path):
    """Verify the whole chain. Returns the list of (seq, record, hash).

    An absent ledger is an empty chain, as in C."""
    if not os.path.exists(path):
        return []
    if os.path.getsize(path) > FILE_MAX:
        raise MjError("ledger is larger than the limit")
    with open(path, "rb") as fh:
        buf = fh.read()
    out, prev, pos, n = [], ZERO, 0, 0
    while pos < len(buf):
        e = buf.find(b"\n", pos)
        n += 1
        if e < 0:
            raise ChainBroken(n, "torn last line")
        try:
            seq, p, r, h = parse_line(buf[pos:e])
        except MjError as ex:
            raise ChainBroken(n, str(ex))
        if seq != n or p != prev:
            raise ChainBroken(n, "sequence or link mismatch")
        out.append((seq, r, h))
        prev, pos = h, e + 1
    return out


def append(path, r):
    """Verify the chain, append one record durably, verify again. Returns its seq."""
    chain = read(path)
    seq = len(chain) + 1
    prev = chain[-1][2] if chain else ZERO
    line = format_line(seq, prev, r)
    if os.path.exists(path) and os.path.getsize(path) + len(line) > FILE_MAX:
        raise MjError("ledger would pass its size limit")
    append_durable(path, line)
    after = read(path)
    if len(after) != seq or after[-1][2] != sha256_hex(line[:-66]):
        raise ChainBroken(seq, "record did not land as written")
    return seq
