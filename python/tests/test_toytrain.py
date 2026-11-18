"""Toy nap on the GPU (or the processor when no GPU): gates, store and ledger.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import unittest

from mjolnir import grades, ledger, replay, toy
from mjolnir.store import Store
from tests.test_replay import MOVES
from tests.util import has_torch, run, tmpdir, tool, write_run


@unittest.skipUnless(has_torch(), "torch and numpy are not installed")
class ToyTrainTest(unittest.TestCase):
    def setUp(self):
        import torch
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.d = tmpdir("toytrain-" + self._testMethodName)
        rd = os.path.join(self.d, "runs")
        os.makedirs(rd)
        for i in range(4):
            write_run(os.path.join(rd, "r%d.jsonl" % i), MOVES, start_xy=(i, 0))
        self.runs = replay.load_all([rd])
        self.st = Store(os.path.join(self.d, "toy"), min_free_bytes=0)
        self.st.put("toy-seed", "milestone", "-", "zero", toy.save([[0] * 8 for _ in range(4)]))
        self.st.set_head("toy-seed")
        self.lp = os.path.join(self.d, "toy-dreams.mjl")

    def test_nap_dry_run_then_integrate(self):
        from mjolnir import toytrain
        res = toytrain.nap(self.st, self.lp, self.runs[:3], self.runs[3:], {}, dream_id="t1", device=self.device,
                           max_shift_bp=10000)
        self.assertEqual(res["outcome"], "dry-run-pass", res)
        self.assertEqual(self.st.head(), "toy-seed")  # a dry run never moves HEAD
        self.assertGreater(res["task_after"], res["task_before"])
        self.assertLessEqual(res["max_move_q16"], int(0.5 * 65536) + 1)
        res2 = toytrain.nap(self.st, self.lp, self.runs[:3], self.runs[3:], {}, dream_id="t2", device=self.device,
                            integrate=True, max_shift_bp=10000)
        self.assertEqual(res2["outcome"], "integrated")
        self.assertEqual(self.st.head(), "t2.cand")
        chain = ledger.read(self.lp)
        self.assertEqual([r["outcome"] for _, r, _ in chain], ["dry-run-pass", "integrated"])
        exe = tool("mj_ledger_verify")
        if exe:
            code, out = run([exe, self.lp])
            self.assertEqual(code, 0, out)

    def test_jd_grades_override_auto(self):
        from mjolnir import toytrain
        gp = os.path.join(self.d, "grades.mjl")
        rid = self.runs[0]["id"]
        grades.add(gp, rid, 2, "good")
        samples, _ = toytrain.build_batch(self.runs[:1], grades.latest(gp))
        self.assertEqual(samples[2][2], 1.0)
        self.assertEqual(samples[2][3], "JD")
        self.assertEqual(samples[3][3], "auto")

    def test_shift_gate_rejects(self):
        from mjolnir import toytrain
        res = toytrain.nap(self.st, self.lp, self.runs[:3], self.runs[3:], {}, dream_id="t3", device=self.device,
                           integrate=True, max_shift_bp=0)
        self.assertEqual(res["outcome"], "rejected")
        self.assertFalse(res["gates"]["shift"])
        self.assertEqual(self.st.head(), "toy-seed")
        self.assertIn("shift", ledger.read(self.lp)[-1][1]["notes"])

    def test_step_cap_holds(self):
        from mjolnir import toytrain
        samples, _ = toytrain.build_batch(self.runs, {})
        gold = toytrain.golden_from(self.runs)
        w0 = toy.load(self.st.get("toy-seed"))
        w1, _ = toytrain.train(w0, samples, gold, device=self.device, steps=200, lr=1.0, step_cap=0.1)
        self.assertLessEqual(max(abs(v) for row in w1 for v in row), int(0.1 * 65536) + 1)

    def test_refuses_without_live_version(self):
        from mjolnir import toytrain
        from mjolnir.common import MjError
        empty = Store(os.path.join(self.d, "empty"), min_free_bytes=0)
        with self.assertRaises(MjError):
            toytrain.nap(empty, self.lp, self.runs, self.runs, {}, device=self.device)


if __name__ == "__main__":
    unittest.main()
