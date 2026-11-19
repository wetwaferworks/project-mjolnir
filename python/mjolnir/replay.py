"""Replay dreams from recorded PCHIE runs, read-only.

A PCHIE research log (format pchie.research.v1) is a hash-chained JSON-lines
file: a start record, then for each decision an "advise" record (where the
agent stood, what was open, what it proposed), a "decided" record and a
"transition" record (the action and its reward), then an end record. Each
line's sha256 covers the exact bytes before its ',"sha256"' and names the
line before it in "prev".

This module opens those files for reading only, checks the chain, and turns
each run into a replay: a list of steps with an automatic partial-credit
label. It never writes next to the source files.

Automatic labels follow the partial-credit rule (design principle 8):
  productive  the step earned a reward (key or goal)
  good        the step reached ground the run had not stood on before
  not-yet     the step went back to ground already visited (a loop)

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import glob
import hashlib
import json
import os

from .common import MjError, safe_id

FORMAT = "pchie.research.v1"
MAX_FILE = 64 * 1024 * 1024
MAX_STEPS = 4096
ACTION_NAMES = {0: "move 0", 1: "move 1", 2: "move 2", 3: "move 3", 4: "action 4", 5: "action 5"}
Q20 = float(1 << 20)


class ReplayError(MjError):
    pass


def _check_chain(lines):
    prev = "0" * 64
    recs = []
    for i, raw in enumerate(lines, 1):
        cut = raw.rfind(b',"sha256"')
        if cut < 0:
            raise ReplayError("line %d has no sha256" % i)
        try:
            d = json.loads(raw)
        except ValueError:
            raise ReplayError("line %d is not JSON" % i)
        if d.get("format") != FORMAT:
            raise ReplayError("line %d is not %s" % (i, FORMAT))
        if d.get("prev") != prev:
            raise ReplayError("line %d does not link to the line before" % i)
        if hashlib.sha256(raw[:cut]).hexdigest() != d.get("sha256"):
            raise ReplayError("line %d hash does not match" % i)
        prev = d["sha256"]
        recs.append(d)
    return recs


def load_run(path):
    """Read one run log (read-only) and return a replay dict, or raise ReplayError."""
    if os.path.getsize(path) > MAX_FILE:
        raise ReplayError("run log is larger than the limit")
    with open(path, "rb") as f:
        data = f.read()
    lines = [l for l in data.split(b"\n") if l.strip()]
    recs = _check_chain(lines)
    if not recs or recs[0].get("event") != "start":
        raise ReplayError("run log does not begin with a start record")
    source_sha = hashlib.sha256(data).hexdigest()
    rid = safe_id(os.path.splitext(os.path.basename(path))[0], 48) + "." + recs[0]["sha256"][:8]
    steps, pending = [], {}
    for d in recs:
        ev = d.get("event")
        if ev == "advise":
            pending = {"advise": d}
        elif ev == "decided" and pending.get("advise", {}).get("decision") == d.get("decision"):
            pending["decided"] = d
        elif ev == "transition" and "decided" in pending:
            a = pending["advise"]
            steps.append({
                "index": len(steps), "decision": a.get("decision"), "task": a.get("task"),
                "x": a.get("x"), "y": a.get("y"), "key": a.get("key"), "open": a.get("open"),
                "proposals": _best_per_token(a.get("proposals", [])),
                "token": pending["decided"].get("token"), "action": d.get("action"),
                "state": d.get("state"), "next": d.get("next"), "reward": d.get("reward_q20", 0) / Q20,
                "ok": d.get("ok"), "record": d.get("sha256"),
            })
            pending = {}
            if len(steps) > MAX_STEPS:
                raise ReplayError("run has more steps than the limit")
    ended = recs[-1].get("event") == "end"
    _auto_label(steps)
    return {"id": rid, "source": os.path.basename(path), "source_sha256": source_sha,
            "flags": recs[0].get("flags", ""), "ended": ended, "steps": steps}


def _best_per_token(props):
    """One entry per proposed token, with its best score, highest first."""
    best = {}
    for p in props:
        if isinstance(p, list) and len(p) >= 2 and isinstance(p[0], int) and isinstance(p[1], (int, float)):
            best[p[0]] = max(best.get(p[0], float("-inf")), p[1] / Q20)
    return sorted(([t, v] for t, v in best.items()), key=lambda e: (-e[1], e[0]))


def _auto_label(steps):
    seen = set()
    for s in steps:
        seen.add(s["state"])
        if s["reward"] > 0:
            s["auto"] = "productive"
        elif s["next"] in seen:
            s["auto"] = "not-yet"
        else:
            s["auto"] = "good"
        s["revisit"] = s["next"] in seen
        seen.add(s["next"])
        props = sorted((p[1] for p in s["proposals"]), reverse=True)
        s["margin"] = (props[0] - props[1]) if len(props) >= 2 else 1.0


def score_bp(steps):
    """Partial credit in basis points: good and productive steps over all graded steps."""
    graded = [s for s in steps if s.get("auto")]
    if not graded:
        return 0
    good = sum(1 for s in graded if s["auto"] in ("good", "productive"))
    return good * 10000 // len(graded)


def find_runs(roots):
    """Every pchie.research.v1 log under the given folders (read-only scan)."""
    out = []
    for root in roots:
        for p in sorted(glob.glob(os.path.join(root, "**", "*.jsonl"), recursive=True)):
            try:
                with open(p, "rb") as f:
                    first = f.readline(4096)
            except OSError:
                continue
            if b'"format":"' + FORMAT.encode() + b'"' in first:
                out.append(p)
    return out


def load_all(roots, on_error=None):
    runs, seen = [], set()
    for p in find_runs(roots):
        try:
            r = load_run(p)
        except (ReplayError, OSError) as e:
            if on_error:
                on_error(p, e)
            continue
        if r["source_sha256"] in seen:  # the same run copied to two places
            continue
        seen.add(r["source_sha256"])
        runs.append(r)
    return runs
