"""Replays: chain checks, partial-credit labels, and read-only use of run logs.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import hashlib
import os
import unittest

from mjolnir import replay, toy
from tests.util import tmpdir, write_run

# right, right, back left (a loop), right again (revisit), up, up with the goal
MOVES = [(1, 1, 0, 0), (1, 1, 0, 0), (3, -1, 0, 0), (1, 1, 0, 0), (0, 0, 1, 0), (0, 0, 1, 1048576)]


class ReplayTest(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir("replay-" + self._testMethodName)
        self.p = os.path.join(self.d, "b1s1_ge.jsonl")
        write_run(self.p, MOVES)

    def test_load_and_labels(self):
        r = replay.load_run(self.p)
        self.assertTrue(r["ended"])
        self.assertEqual(len(r["steps"]), 6)
        self.assertEqual([s["auto"] for s in r["steps"]], ["good", "good", "not-yet", "not-yet", "good", "productive"])
        self.assertEqual(replay.score_bp(r["steps"]), 6666)
        self.assertTrue(r["id"].startswith("b1s1_ge."))

    def test_source_is_never_written(self):
        before = (hashlib.sha256(open(self.p, "rb").read()).hexdigest(), os.stat(self.p).st_mtime_ns)
        replay.load_all([self.d])
        after = (hashlib.sha256(open(self.p, "rb").read()).hexdigest(), os.stat(self.p).st_mtime_ns)
        self.assertEqual(before, after)
        self.assertEqual(sorted(os.listdir(self.d)), ["b1s1_ge.jsonl"])

    def test_tampered_log_refused(self):
        data = open(self.p, "rb").read().replace(b'"action":3', b'"action":2', 1)
        bad = os.path.join(self.d, "bad.jsonl")
        open(bad, "wb").write(data)
        with self.assertRaises(replay.ReplayError):
            replay.load_run(bad)
        errs = []
        runs = replay.load_all([self.d], on_error=lambda p, e: errs.append(p))
        self.assertEqual(len(runs), 1)
        self.assertEqual(len(errs), 1)

    def test_duplicate_copies_counted_once(self):
        sub = os.path.join(self.d, "copy")
        os.makedirs(sub)
        open(os.path.join(sub, "b1s1_ge.jsonl"), "wb").write(open(self.p, "rb").read())
        self.assertEqual(len(replay.load_all([self.d])), 1)

    def test_other_files_ignored(self):
        open(os.path.join(self.d, "notes.jsonl"), "wb").write(b'{"format":"something.else"}\n')
        self.assertEqual(len(replay.find_runs([self.d])), 1)

    def test_duplicate_proposals_collapse(self):
        props = replay._best_per_token([[8, 524288, 1], [8, 262144, 2], [9, 786432, 1], ["x", 1], [7]])
        self.assertEqual(props, [[9, 0.75], [8, 0.5]])

    def test_toy_features(self):
        r = replay.load_run(self.p)
        f = toy.features(r["steps"])
        self.assertEqual(len(f), 6)
        x, a, s = f[3]
        self.assertEqual(len(x), 8)
        self.assertEqual(x[5], 127)  # standing on visited ground
        self.assertTrue(all(-128 <= v <= 127 for v in x))


if __name__ == "__main__":
    unittest.main()
