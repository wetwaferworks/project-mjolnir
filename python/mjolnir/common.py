"""Shared rules: ids, free text, hashes, time, and durable file writes.

These mirror mj_common.c and mj_fs.c exactly, so a file written here is
accepted by the C tools and the other way round.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import datetime
import hashlib
import os
import re
import zlib

ID_MAX = 64
TEXT_MAX = 256
_ID_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9._-]{0,63}")
_U64_RE = re.compile(r"0|[1-9][0-9]{0,19}")


class MjError(Exception):
    """Base for every refusal. The message says what was refused and why."""


class Refused(MjError):
    """A standing rule or a safety check said no."""


class Corrupt(MjError):
    """A checksum, hash or format check failed."""


def id_valid(s):
    return isinstance(s, str) and _ID_RE.fullmatch(s) is not None


def text_valid(s):
    if not isinstance(s, str) or len(s) > TEXT_MAX:
        return False
    return all(0x20 <= ord(c) <= 0x7E and c != "|" for c in s)


def parse_u64(s):
    """Canonical decimal only: no sign, no leading zeros, fits in 64 bits."""
    if not isinstance(s, str) or _U64_RE.fullmatch(s) is None:
        return None
    v = int(s)
    return v if v < (1 << 64) else None


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def sha256_hex(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path, chunk=16 * 1024 * 1024):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def utc_stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def safe_id(s, limit=ID_MAX):
    """Turn any name into a valid id: unknown characters become '_'."""
    out = "".join(c if (c.isascii() and (c.isalnum() or c in "._-")) else "_" for c in s)
    out = out.lstrip(".-") or "x"
    return out[:limit]


def _fsync_dir(d):
    if os.name == "nt":
        return  # NTFS has no directory handle to flush; the rename is journaled
    fd = os.open(d, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def write_atomic(path, data, overwrite=False):
    """Write to a temporary file, flush it to disk, then move it into place.

    With overwrite False the final name must not exist, and a race with another
    writer can never replace it: the move is a hard link, which fails if the
    name is taken. The data is read back and compared before returning.
    """
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    if not overwrite and os.path.exists(path):
        raise FileExistsError(path)
    tmp = "%s.%d.tmp" % (path, os.getpid())
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_BINARY", 0), 0o644)
    try:
        view = memoryview(data)
        while view:
            n = os.write(fd, view)
            view = view[n:]
        os.fsync(fd)
    finally:
        os.close(fd)
    try:
        if overwrite:
            os.replace(tmp, path)
        else:
            os.link(tmp, path)
            os.remove(tmp)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    _fsync_dir(d)
    with open(path, "rb") as f:
        if f.read() != bytes(data):
            raise Corrupt("read-back mismatch after writing " + os.path.basename(path))


def append_durable(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | getattr(os, "O_BINARY", 0), 0o644)
    try:
        view = memoryview(data)
        while view:
            n = os.write(fd, view)
            view = view[n:]
        os.fsync(fd)
    finally:
        os.close(fd)
