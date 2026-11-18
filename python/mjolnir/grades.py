"""JD's grades, kept in their own hash-chained ledger (MJL1, outcome "grade").

Each grade is one ledger line: the step it grades (in "dream" and "inputs"),
who graded it, and the label in "gates" as label=good|bad|productive. Grades
are never edited; a new grade for the same step simply comes later in the
chain, and the latest one wins.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
from . import ledger
from .common import MjError, id_valid, safe_id, utc_now

LABELS = {"good": 10000, "productive": 10000, "bad": 0}
REWARD = {"good": 1.0, "productive": 1.5, "bad": -1.0}
AUTO_REWARD = {"good": 1.0, "productive": 1.5, "not-yet": -1.0}


def step_id(replay_id, index):
    return "%s.s%04d" % (replay_id, index)


def add(path, replay_id, index, label, grader="JD", auto=""):
    if label not in LABELS:
        raise MjError("unknown label: %r" % label)
    if not id_valid(grader):
        raise MjError("bad grader name")
    sid = step_id(replay_id, index)
    r = ledger.record(dream=safe_id("g-" + sid), when=utc_now(), trigger="label", grader=grader,
                      inputs=safe_id(replay_id), edit="-", score_after=LABELS[label], gates="label=" + label,
                      outcome="grade", notes=("step=%d auto=%s" % (index, auto or "-")))
    return ledger.append(path, r)


def latest(path):
    """{(replay_id, index): (label, grader)} with the newest grade per step."""
    out = {}
    for _seq, r, _h in ledger.read(path):
        if r["outcome"] != "grade" or not r["gates"].startswith("label="):
            continue
        label = r["gates"][6:]
        idx = None
        for part in r["notes"].split():
            if part.startswith("step="):
                try:
                    idx = int(part[5:])
                except ValueError:
                    idx = None
        if label in LABELS and idx is not None:
            out[(r["inputs"], idx)] = (label, r["grader"])
    return out
