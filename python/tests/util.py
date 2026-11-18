"""Helpers shared by the Python tests.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import hashlib
import json
import os
import shutil
import subprocess
import tempfile


def tmpdir(name):
    base = os.environ.get("MJ_TEST_TMP") or tempfile.gettempdir()
    d = os.path.join(base, "py-" + name)
    def unlock(func, path, _exc):  # a test may leave a read-only original behind
        os.chmod(path, 0o666)
        func(path)

    if os.path.exists(d):
        shutil.rmtree(d, onerror=unlock)
    os.makedirs(d)
    return d


def tool(name):
    """Path to a C tool from the build, or None when the tools are not built."""
    d = os.environ.get("MJ_TOOLS_DIR")
    if not d:
        return None
    for cand in (os.path.join(d, name + ".exe"), os.path.join(d, name)):
        if os.path.exists(cand):
            return cand
    return None


def run(args):
    p = subprocess.run(args, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def has_torch():
    try:
        import numpy  # noqa: F401
        import torch  # noqa: F401
        return True
    except Exception:
        return False


def write_run(path, moves, start_xy=(0, 0)):
    """Write a small, correctly chained pchie.research.v1 log.

    moves: list of (action, dx, dy, reward_q20)."""
    prev = "0" * 64
    lines = []

    def emit(d):
        nonlocal prev
        d = dict(d)
        d["prev"] = prev
        body = json.dumps(d, separators=(",", ":"))
        h = hashlib.sha256(body[:-1].encode()).hexdigest()  # the hash covers the bytes before ,"sha256"
        lines.append(body[:-1] + ',"sha256":"%s"}' % h)
        prev = h

    emit({"format": "pchie.research.v1", "event": "start", "flags": "test"})
    x, y = start_xy
    for i, (a, dx, dy, rq) in enumerate(moves, 1):
        state = (x + 1) * 1000 + y
        emit({"format": "pchie.research.v1", "event": "advise", "decision": i, "task": 5, "x": x, "y": y, "key": 0,
              "open": (1 << a) | (1 << ((a + 2) % 4)), "proposals": [[7 + a, 600000, 1], [7 + (a + 1) % 4, 400000, 1]], "scores": []})
        emit({"format": "pchie.research.v1", "event": "decided", "decision": i, "token": 7 + a})
        x, y = x + dx, y + dy
        emit({"format": "pchie.research.v1", "event": "transition", "state": state, "action": a,
              "next": (x + 1) * 1000 + y, "reward_q20": rq, "ok": 1})
    emit({"format": "pchie.research.v1", "event": "end", "decisions": len(moves)})
    with open(path, "wb") as f:
        f.write(("\n".join(lines) + "\n").encode())
