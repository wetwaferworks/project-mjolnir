"""Parity between the C port (mj_m1) and the Python replay, grades and toy nap.

Replays: the same folders go through replay.load_all and `mj_m1 replays-json`,
and both are written as json.dumps(sort_keys=True, separators=(",", ":")):
the texts must be identical, as must the skipped-file messages and the
`replays` summary. The folders hold awkward but valid logs on purpose
(repeated keys, 1 == 1.0 == true, -0.0, non-ASCII text, CRLF and blank lines,
hidden folders, copies, broken chains).

Grades: each side writes grades the other reads, with the same fields.

Toy nap: the C nap draws its minibatches from splitmix64 instead of PyTorch's
generator, so the candidates are not the same. Instead the test (1) replays
a short C training (40 steps) in PyTorch with the very same minibatch indices
and compares the weights, and (2) recomputes every gate number in Python from
the candidate of a full 300-step C nap and compares them with what C reported
and wrote in the ledger. Over 300 steps with momentum the two float32 sums
(PyTorch's order and C's) drift apart by up to a few tenths of a weight unit,
which is why the weights are compared on the short run.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import unittest

from mjolnir import grades, ledger, replay, toy
from tests.util import has_torch, run, tmpdir, tool, write_run
from tests.test_replay import MOVES

PY_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class RawLog:
    """A chained log built from raw record texts (so repeated keys survive)."""

    def __init__(self):
        self.prev = "0" * 64
        self.lines = []

    def raw(self, inner):
        body = '{' + inner + ',"prev":"%s"' % self.prev
        h = hashlib.sha256(body.encode()).hexdigest()
        self.lines.append(body + ',"sha256":"%s"}' % h)
        self.prev = h

    def rec(self, **kw):
        kw = dict({"format": "pchie.research.v1"}, **kw)
        self.raw(json.dumps(kw, separators=(",", ":"), ensure_ascii=False)[1:-1])

    def write(self, path, sep="\n"):
        with open(path, "wb") as f:
            f.write((sep.join(self.lines) + sep).encode())


def edge_log(path):
    L = RawLog()
    L.raw('"format":"pchie.research.v1","event":"start","flags":{"b":1,"a":"x","b":2}')
    L.rec(event="advise", decision=1, task=5, x=0, y=0, key=0, open=5,
          proposals=[[3, 5], [3, 7.5], [True, 2], [1, 9], [2], "junk", [4.0, 1], [5, "x"], [-1, 0], [6, -0.0], [7, 0.0],
                     [12345678901234567890123, 3]])
    L.rec(event="decided", decision=1.0, token="t")
    L.rec(event="transition", state=1, action=1.0, next=2.0, reward_q20=True, ok=None)
    L.rec(event="advise", decision=2, x=1, key=[1])
    L.rec(event="decided", decision=3, token=1)
    L.rec(event="decided", decision=2, token=2)
    L.rec(event="transition", state=2, action=0, next=1, reward_q20=-5)
    L.rec(event="advise", decision="a", proposals={})
    L.rec(event="decided", decision="a")
    L.rec(event="transition", state="s", action=[1], next="s", reward_q20=1.5e-300)
    L.rec(event="advise", decision=4, task="\u00e9\U0001F600\u007f", proposals="ab")
    L.rec(event="decided", decision=4, token=None)
    L.rec(event="transition", state=1.0, action=2, next=True)
    L.rec(event="advise", decision=5, proposals=[[9, 1e300], [8, 1e300], [10, 2.5e-308]])
    L.rec(event="advise", decision=6, proposals=[[9, 1]])
    L.rec(event="decided", decision=6, token=9)
    L.rec(event="transition", state=7, action=3, next=8, reward_q20=0.5)
    L.rec(event="transition", state=8, action=3, next=9)
    L.rec(event="other", note="\\ \" \n \t")
    L.write(path, sep="\r\n\n  \n")


@unittest.skipUnless(tool("mj_m1"), "needs the C tools")
class M1Parity(unittest.TestCase):
    def setUp(self):
        self.m1 = tool("mj_m1")
        self.d = tmpdir("cport-m1-" + self._testMethodName)

    def c(self, *args):
        p = subprocess.run([self.m1] + list(args), capture_output=True, timeout=300)
        return p.returncode, p.stdout.decode("utf-8"), p.stderr.decode("utf-8")

    def runs_folder(self):
        rd = os.path.join(self.d, "runs")
        sub = os.path.join(rd, "sub", "deeper")
        os.makedirs(sub)
        os.makedirs(os.path.join(rd, ".hidden"))
        for i in range(5):
            write_run(os.path.join(rd, "r%d.jsonl" % i), MOVES, start_xy=(i, i % 2))
        edge_log(os.path.join(sub, "\u00fcn\u00efcode run.jsonl"))
        shutil.copy(os.path.join(rd, "r1.jsonl"), os.path.join(sub, "copy-of-r1.jsonl"))
        shutil.copy(os.path.join(rd, "r1.jsonl"), os.path.join(rd, ".hidden", "h.jsonl"))
        data = open(os.path.join(rd, "r2.jsonl"), "rb").read()
        open(os.path.join(rd, "broken.jsonl"), "wb").write(data.replace(b'"task":5', b'"task":7', 1))
        open(os.path.join(rd, "nostart.jsonl"), "wb").write(b"\n".join(data.split(b"\n")[1:]))
        open(os.path.join(rd, "other.jsonl"), "wb").write(b'{"format":"something.else"}\n')
        open(os.path.join(rd, "empty.jsonl"), "wb").write(b"")
        open(os.path.join(rd, "long.jsonl"), "wb").write(b" " * 5000 + b'{"format":"pchie.research.v1"}\n')
        return rd

    def test_replays_identical(self):
        rd = self.runs_folder()
        errs = []
        runs = replay.load_all([rd], on_error=lambda p, e: errs.append("skipped %s: %s" % (os.path.basename(p), e)))
        want = json.dumps(runs, sort_keys=True, separators=(",", ":"))
        code, out, err = self.c("replays-json", "--runs", rd)
        self.assertEqual(code, 0, err)
        self.assertEqual(out.rstrip("\r\n"), want)  # a Windows console line ends in CR LF
        self.assertEqual(err.splitlines(), errs)
        print("\n  replays: %d runs, %d steps, %d skipped; C and Python dumps identical (%d bytes)"
              % (len(runs), sum(len(r["steps"]) for r in runs), len(errs), len(want)))
        # the summary command prints the same text
        p = subprocess.run([sys.executable, "-m", "mjolnir", "replays", "--runs", rd], capture_output=True, cwd=PY_DIR)
        code, out, err = self.c("replays", "--runs", rd)
        self.assertEqual(out, p.stdout.decode("utf-8"))
        self.assertEqual(err, p.stderr.decode("utf-8"))

    def test_grades_both_ways(self):
        gp = os.path.join(self.d, "grades.mjl")
        grades.add(gp, "r1.0123abcd", 3, "good", auto="not-yet")
        code, out, err = self.c("grade", "--data", self.d, "--replay", "r1.0123abcd", "--step", "3", "--label", "bad")
        self.assertEqual(code, 0, err)
        grades.add(gp, "r2.0123abcd", 0, "productive", grader="helper")
        code, out, err = self.c("grade", "--data", self.d, "--replay", "r\u00e9 2", "--step", "-4", "--label",
                                "good", "--grader", "JD", "--auto", "good")
        self.assertEqual(code, 0, err)
        code, out, err = self.c("grade", "--data", self.d, "--replay", "x", "--step", "1", "--label", "great")
        self.assertEqual(code, 2)
        self.assertIn("unknown label", err)
        recs = ledger.read(gp)
        self.assertEqual(len(recs), 4)
        side = os.path.join(self.d, "python-side.mjl")
        grades.add(side, "r\u00e9 2", -4, "good", grader="JD", auto="good")
        py_rec = dict(ledger.read(side)[0][1])
        c_rec = dict(recs[3][1])
        self.assertEqual(c_rec["inputs"], "r__2")
        for k in ("when",):
            c_rec.pop(k)
            py_rec.pop(k)
        self.assertEqual(c_rec, py_rec)
        latest = grades.latest(gp)
        code, out, err = self.c("grades", "--data", self.d)
        c_latest = {(a, int(i)): (lab, g) for a, i, lab, g in (l.split(" ") for l in out.splitlines())}
        self.assertEqual(c_latest, latest)

    @unittest.skipUnless(has_torch(), "needs torch")
    def test_toy_nap(self):
        import torch
        from mjolnir import toytrain
        from mjolnir.real.edit import split_runs
        from mjolnir.store import Store
        rd = self.runs_folder()
        data = os.path.join(self.d, "data")
        code, out, err = self.c("toy-seed", "--data", data)
        self.assertEqual(code, 0, err)
        grades.add(os.path.join(data, "grades.mjl"), replay.load_all([rd])[1]["id"], 2, "good")
        code, out, err = self.c("toy-nap", "--runs", rd, "--data", data, "--id", "c0", "--max-shift-bp", "10000",
                                "--steps", "40")
        self.assertEqual(code, 0, err)
        code, out, err = self.c("toy-nap", "--runs", rd, "--data", data, "--id", "c1", "--max-shift-bp", "10000")
        self.assertEqual(code, 0, err)
        res = json.loads(out)

        runs = replay.load_all([rd])
        learn, hold = split_runs(runs)
        gm = grades.latest(os.path.join(data, "grades.mjl"))
        samples, sources = toytrain.build_batch(learn, gm)
        gold = toytrain.golden_from(hold)
        st = Store(os.path.join(data, "toy-store"))
        w0 = toy.load(st.get("toy-seed"))
        w1 = toy.load(st.get("c1.cand"))
        w40 = toy.load(st.get("c0.cand"))
        self.assertEqual(toy.load(st.get("c1.snap")), w0)

        # (1) the same training in PyTorch, fed C's minibatch indices
        n = len(samples) + len(gold)
        bs = min(256, n)
        state = 1
        M = (1 << 64) - 1

        def nxt():
            nonlocal state
            state = (state + 0x9E3779B97F4A7C15) & M
            z = state
            z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M
            z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M
            return z ^ (z >> 31)

        X = torch.tensor([s[0] for s in samples] + [x for x, _ in gold], dtype=torch.float32) / 127.0
        Y = torch.tensor([s[1] for s in samples] + [a for _, a in gold], dtype=torch.long)
        R = torch.tensor([s[2] for s in samples] + [1.0] * len(gold), dtype=torch.float32)
        W0 = torch.tensor(w0, dtype=torch.float32) / 65536.0
        W = W0.clone().requires_grad_(True)
        opt = torch.optim.SGD([W], lr=0.5, momentum=0.9)
        for _ in range(40):
            idx = torch.tensor([nxt() % n for _ in range(bs)])
            logp = torch.log_softmax(X[idx] @ W.t() * 4.0, dim=-1).gather(1, Y[idx, None]).squeeze(1)
            r = R[idx]
            weaken = torch.log1p(-logp.exp().clamp(max=1 - 1e-6))
            loss = -torch.where(r > 0, r * logp, -r * weaken).mean()
            opt.zero_grad()
            loss.backward()
            opt.step()
            with torch.no_grad():
                W.copy_(torch.max(torch.min(W, W0 + 0.5), W0 - 0.5))
                W.clamp_(-63.0, 63.0)
        q = (W.detach() * 65536.0).round().to(torch.int64).tolist()
        diff = max(abs(q[a][i] - w40[a][i]) for a in range(toy.A) for i in range(toy.F))
        print("\n  toy nap: %d samples, %d golden; 40 steps in PyTorch with C's minibatches vs C: largest weight "
              "difference %d Q16 units, loss %.4f" % (len(samples), len(gold), diff, float(loss)))
        self.assertLessEqual(diff, 4)

        # (2) every gate number, recomputed in Python from C's candidate
        cap_q = int(0.5 * 65536) + 1
        moved = max(abs(w1[a][i] - w0[a][i]) for a in range(toy.A) for i in range(toy.F))
        want = {"task_before": toytrain.task_bp(w0, samples), "task_after": toytrain.task_bp(w1, samples),
                "golden_before": toytrain.golden_bp(w0, gold), "golden_after": toytrain.golden_bp(w1, gold),
                "shift_bp": toytrain.shift_bp(w0, w1, gold), "max_move_q16": moved, "samples": len(samples),
                "jd_graded": sum(1 for s in samples if s[3] == "JD"), "gold": len(gold)}
        self.assertEqual({k: res[k] for k in want}, want)
        self.assertEqual(want["jd_graded"], 1)
        self.assertEqual(res["gates"], {"step": moved <= cap_q, "shift": True,
                                        "golden": want["golden_after"] >= want["golden_before"],
                                        "task": want["task_after"] > want["task_before"]})
        rec = ledger.read(os.path.join(data, "toy-dreams.mjl"))[1][1]
        self.assertEqual(rec["inputs"], ",".join(sources))
        self.assertEqual((rec["score_before"], rec["score_after"], rec["shift_bp"]),
                         (want["task_before"], want["task_after"], want["shift_bp"]))
        self.assertEqual(rec["grader"], "JD")
        self.assertTrue(rec["edit"].startswith("gpu=cpu steps=300 lr=0.5 cap=0.5 samples=%d jd=1 gold=%d loss="
                                               % (len(samples), len(gold))))

        # a Python nap reads the store and ledger C wrote, and C reads Python's
        pres = toytrain.nap(st, os.path.join(data, "toy-dreams.mjl"), learn, hold, gm, dream_id="p1", device="cpu",
                            max_shift_bp=10000)
        self.assertEqual(pres["ledger_seq"], 3)
        code, out, err = self.c("toy-nap", "--runs", rd, "--data", data, "--id", "c2", "--integrate",
                                "--max-shift-bp", "10000")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads(out)["ledger_seq"], 4)
        self.assertEqual(Store(os.path.join(data, "toy-store")).head(),
                         "c2.cand" if json.loads(out)["outcome"] == "integrated" else "toy-seed")
        # the same id twice is refused by both
        code, out, err = self.c("toy-nap", "--runs", rd, "--data", data, "--id", "c2")
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
