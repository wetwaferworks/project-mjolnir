"""Command line for Mjolnir stage M1.

    python -m mjolnir replays    --runs FOLDER [FOLDER ...]
    python -m mjolnir label      --runs FOLDER ... --data FOLDER [--port 8765]
    python -m mjolnir toy-seed   --data FOLDER
    python -m mjolnir toy-nap    --runs FOLDER ... --data FOLDER [--device cuda] [--integrate]
    python -m mjolnir real-register --original DIR --working DIR --data FOLDER
    python -m mjolnir real-dream    --working DIR --data FOLDER --runs FOLDER ... [--layers 40 41]
    python -m mjolnir real-apply    --original DIR --working DIR --data FOLDER --version ID
    python -m mjolnir real-eval     --original DIR --working DIR --data FOLDER --runs FOLDER ...
    python -m mjolnir real-verify   --dir DIR --sums FILE

--data is where stores, ledgers, grades and reports live. Keep it outside any
shared or published folder: it holds run data.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import argparse
import json
import os
import sys

from . import grades, ledger, replay, toy
from .common import MjError
from .store import Store


def _runs(args):
    errs = []
    runs = replay.load_all(args.runs, on_error=lambda p, e: errs.append((os.path.basename(p), str(e))))
    for name, e in errs:
        print("skipped %s: %s" % (name, e), file=sys.stderr)
    return runs


def main(argv=None):
    ap = argparse.ArgumentParser(prog="mjolnir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("replays")
    s.add_argument("--runs", nargs="+", required=True)
    s = sub.add_parser("label")
    s.add_argument("--runs", nargs="+", required=True)
    s.add_argument("--data", required=True)
    s.add_argument("--port", type=int, default=8765)
    s.add_argument("--grader", default="JD")
    s.add_argument("--no-browser", action="store_true")
    s = sub.add_parser("toy-seed")
    s.add_argument("--data", required=True)
    s = sub.add_parser("toy-nap")
    s.add_argument("--runs", nargs="+", required=True)
    s.add_argument("--data", required=True)
    s.add_argument("--device", default="cuda")
    s.add_argument("--integrate", action="store_true")
    s.add_argument("--steps", type=int, default=300)
    s.add_argument("--max-shift-bp", type=int, default=2000)
    s.add_argument("--id")
    for name in ("real-register", "real-dream", "real-apply", "real-eval"):
        s = sub.add_parser(name)
        s.add_argument("--data", required=True)
        s.add_argument("--working", required=True)
        if name != "real-dream":
            s.add_argument("--original", required=True)
        if name in ("real-dream", "real-eval"):
            s.add_argument("--runs", nargs="+", required=True)
        if name == "real-dream":
            s.add_argument("--layers", nargs="+", type=int, default=[40, 41])
            s.add_argument("--rank", type=int, default=16)
            s.add_argument("--strengths", nargs="+", type=float, default=[0.02, 0.05, 0.1, 0.2])
            s.add_argument("--id")
        if name == "real-apply":
            s.add_argument("--version", required=True)
    s = sub.add_parser("real-verify")
    s.add_argument("--dir", required=True)
    s.add_argument("--sums", required=True)
    a = ap.parse_args(argv)

    try:
        if a.cmd == "replays":
            runs = _runs(a)
            steps = sum(len(r["steps"]) for r in runs)
            print("%d runs, %d steps" % (len(runs), steps))
            for r in runs:
                c = {k: sum(1 for x in r["steps"] if x["auto"] == k) for k in ("good", "productive", "not-yet")}
                print("  %-40s steps=%3d score=%5d bp %s" % (r["id"], len(r["steps"]), replay.score_bp(r["steps"]), c))
        elif a.cmd == "label":
            from .labeler import Session, serve
            runs = _runs(a)
            os.makedirs(a.data, exist_ok=True)
            serve(Session(runs, os.path.join(a.data, "grades.mjl"), a.grader), a.port, not a.no_browser)
        elif a.cmd == "toy-seed":
            st = Store(os.path.join(a.data, "toy-store"))
            if st.head() is not None:
                raise MjError("the toy store already has a live version")
            st.put("toy-seed", "milestone", "-", "zero toy policy", toy.save([[0] * toy.F for _ in range(toy.A)]))
            st.set_head("toy-seed")
            print("toy store seeded")
        elif a.cmd == "toy-nap":
            from . import toytrain
            from .real.edit import split_runs
            runs = _runs(a)
            learn, hold = split_runs(runs)
            st = Store(os.path.join(a.data, "toy-store"))
            gm = grades.latest(os.path.join(a.data, "grades.mjl"))
            res = toytrain.nap(st, os.path.join(a.data, "toy-dreams.mjl"), learn, hold, gm, dream_id=a.id,
                               device=a.device, integrate=a.integrate, steps=a.steps,
                               max_shift_bp=a.max_shift_bp)
            print(json.dumps(res, indent=1))
        elif a.cmd.startswith("real-"):
            from .real import session, workcopy
            if a.cmd == "real-register":
                session.register(a.original, a.working, a.data)
            elif a.cmd == "real-dream":
                rep = session.dream(a.working, a.data, a.runs, layers=tuple(a.layers), rank=a.rank,
                                    strengths=tuple(a.strengths), dream_id=a.id)
                print(json.dumps({k: rep[k] for k in ("dream", "candidate", "seconds") if k in rep}, default=float))
            elif a.cmd == "real-apply":
                session.apply(a.original, a.working, a.data, a.version)
            elif a.cmd == "real-eval":
                print(json.dumps(session.evaluate(a.original, a.working, a.data, a.runs), indent=1))
            elif a.cmd == "real-verify":
                want = workcopy.parse_sums(open(a.sums, "rb").read())
                got = workcopy.checksums(a.dir)
                bad = [n for n in want if got.get(n) != want[n]]
                print("OK %d files match" % len(want) if not bad else "MISMATCH: " + ", ".join(bad))
                return 0 if not bad else 1
    except (MjError, FileExistsError) as e:
        print("refused: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
