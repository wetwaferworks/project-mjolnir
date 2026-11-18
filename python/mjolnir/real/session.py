"""The steps of a real-weight session, each one recorded in the ledger.

  register   checksum the original, make and verify the working copy, and
             store a base record (MJBASE1: file names, sizes, hashes) as a
             pinned milestone in the real-weight store
  dream      work out an edit on the working copy's current weights, try it
             in memory, gate it, and store it as a candidate version
  apply      write a stored version into the working copy (or the base,
             to roll back) and check what landed
  evaluate   score the working copy against the original on disk

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import gc
import json
import os
import time

from .. import ledger, replay
from ..common import MjError, Refused, utc_now, utc_stamp, write_atomic
from ..store import Store
from . import delta as mjdelta
from . import edit as mjedit
from . import workcopy

BASE_ID = "base"


def paths(data):
    return {"store": os.path.join(data, "real-store"), "ledger": os.path.join(data, "real-dreams.mjl"),
            "reports": os.path.join(data, "reports")}


def real_store(data):
    return Store(paths(data)["store"], max_store_bytes=8 << 30, max_blob_bytes=1 << 30)


def base_record(sums):
    return b"MJBASE1\n" + workcopy.format_sums(sums)


def register(original, working, data, log=print):
    p = paths(data)
    t0 = time.time()
    if os.path.exists(working) and os.listdir(working):
        sums = workcopy.checksums(original)
        if workcopy.checksums(working) != sums:
            raise Refused("a working copy exists and differs from the original; check it out from the store instead")
        log("working copy already present and identical")
    else:
        sums = workcopy.make_copy(original, working)
        log("working copy made and verified (%.0fs)" % (time.time() - t0))
    st = real_store(data)
    if not st.ever_used(BASE_ID):
        st.put(BASE_ID, "milestone", "-", "original download, untouched", base_record(sums))
        st.set_head(BASE_ID)
        workcopy.write_state(working, BASE_ID, [])
        ledger.append(p["ledger"], ledger.record(
            dream="register-" + utc_stamp(), trigger="register", grader="auto-check", before="-", candidate=BASE_ID,
            edit="working copy made; original checksummed and marked read-only", score_before=0, score_after=0,
            gates="copy=pass", outcome="integrated", notes="model=%s" % sums[workcopy.MODEL_FILE][1][:16]))
    return sums


def base_sha(data):
    st = real_store(data)
    text = st.get(BASE_ID)
    sums = workcopy.parse_sums(text[len(b"MJBASE1\n"):])
    return sums[workcopy.MODEL_FILE][1]


def _load(model_dir, layers):
    from transformers import AutoTokenizer
    from .model import TextModel
    tok = AutoTokenizer.from_pretrained(model_dir)
    m = TextModel(model_dir, resident_layers=layers)
    return m, tok


def _free(*objs):
    import torch
    for o in objs:
        del o
    gc.collect()
    torch.cuda.empty_cache()


def dream(working, data, run_roots, layers=(40, 41), rank=16, strengths=(0.02, 0.05, 0.1, 0.2), dream_id=None,
          log=print):
    p = paths(data)
    ledger.read(p["ledger"])
    st = real_store(data)
    live = st.head()
    state = workcopy.read_state(working)
    if state["applied"] != live:
        raise Refused("the working copy holds %s but the store's live version is %s" % (state["applied"], live))
    runs = replay.load_all(run_roots)
    if not runs:
        raise MjError("no recorded runs found")
    dream_id = dream_id or ("real-" + utc_stamp())
    m, tok = _load(working, layers)
    ev = mjedit.Evaluator(m, tok)
    res = mjedit.dream(m, ev, runs, layers=layers, rank=rank, strengths=strengths, log=log)
    chosen = res["chosen"]
    report = {k: v for k, v in res.items() if k != "factors"}
    report.update({"dream": dream_id, "when": utc_now(), "live": live, "runs": [r["id"] for r in runs]})
    cand = "-"
    if chosen:
        entries = []
        for L in layers:
            U, V = res["factors"][L]
            entries.append(("model.language_model.layers.%d.mlp.down_proj.weight" % L, U, V, chosen["strength"]))
        blob = mjdelta.pack(base_sha(data), entries)
        cand = dream_id + ".cand"
        st.put(cand, "candidate", live, "layers %s rank %d strength %g" % (
            "+".join(map(str, layers)), rank, chosen["strength"]), blob)
        report["candidate"] = cand
        report["delta_bytes"] = len(blob)
    b, a = res["before"]["learn"]["score_bp"], (chosen or res["trials"][-1])["learn"]["score_bp"]
    shift = int(round(10000 * (1 - (chosen or res["trials"][-1])["golden"]["top1_agree"])))
    gates = (chosen or res["trials"][-1])["gates"]
    ledger.append(p["ledger"], ledger.record(
        dream=dream_id, when=utc_now(), trigger="nap", grader="auto-maze", inputs="set-" + _set_hash(report["runs"]),
        before=live, candidate=cand,
        edit="low-rank down_proj edit layers=%s rank=%d strength=%s" % (
            "+".join(map(str, layers)), rank, chosen["strength"] if chosen else "-"),
        score_before=b, score_after=a, shift_bp=max(0, min(10000, shift)),
        gates=",".join("%s=%s" % (k, "pass" if v else "fail") for k, v in gates.items()),
        outcome="dry-run-pass" if chosen else "rejected",
        notes="in-memory trial only; not written to the working copy" if chosen else "no strength passed every gate"))
    os.makedirs(p["reports"], exist_ok=True)
    write_atomic(os.path.join(p["reports"], dream_id + ".json"), json.dumps(report, indent=1, default=float).encode())
    _free(m)
    return report


def _set_hash(ids):
    import hashlib
    return hashlib.sha256(",".join(sorted(ids)).encode()).hexdigest()[:60]


def apply(original, working, data, version_id, log=print):
    p = paths(data)
    st = real_store(data)
    t0 = time.time()
    before = workcopy.read_state(working)["applied"]
    touched = workcopy.checkout(st, version_id, original, working, base_sha(data))
    st.set_head(version_id)
    ledger.append(p["ledger"], ledger.record(
        dream="apply-" + utc_stamp(), trigger="apply", grader="auto-check", before=before, candidate=version_id,
        edit="wrote %d tensors into the working copy" % len(touched), gates="readback=pass",
        outcome="rolled-back" if version_id == BASE_ID else "integrated",
        notes="working copy only; the original is untouched"))
    log("working copy now holds %s (%d tensors written, %.0fs)" % (version_id, len(touched), time.time() - t0))
    return touched


def evaluate(original, working, data, run_roots, layers=(), log=print):
    """Score the original and the working copy as they are on disk."""
    t0 = time.time()
    runs = replay.load_all(run_roots)
    train_runs, hold_runs = mjedit.split_runs(runs)
    train = mjedit.dedupe(mjedit.examples_from(train_runs))
    hold = mjedit.dedupe(mjedit.examples_from(hold_runs))
    out = {}
    m, tok = _load(original, layers)
    ev = mjedit.Evaluator(m, tok)
    out["original"] = {"learn": ev.task(train)[0], "held_out": ev.task(hold)[0]}
    g, saved = ev.golden(mjedit.GOLDEN_TEXTS)
    out["original"]["golden"] = g
    _free(m, ev)
    log("original scored (%.0fs)" % (time.time() - t0))
    m, tok = _load(working, layers)
    ev = mjedit.Evaluator(m, tok)
    out["working"] = {"learn": ev.task(train)[0], "held_out": ev.task(hold)[0],
                      "golden": ev.golden(mjedit.GOLDEN_TEXTS, saved)[0],
                      "applied": workcopy.read_state(working)["applied"]}
    _free(m, ev)
    out["seconds"] = time.time() - t0
    log("working copy scored (%.0fs)" % out["seconds"])
    p = paths(data)
    os.makedirs(p["reports"], exist_ok=True)
    write_atomic(os.path.join(p["reports"], "eval-" + utc_stamp() + ".json"), json.dumps(out, indent=1).encode())
    return out
