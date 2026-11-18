"""Ledger: chain, refusals, and agreement with the C verifier.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import unittest

from mjolnir import ledger
from mjolnir.common import MjError
from tests.util import run, tmpdir, tool


def rec(i, **kw):
    base = dict(dream="d-%d" % i, when="2026-11-17T12:00:00Z", trigger="test", grader="JD", edit="e", gates="g",
                outcome="grade", notes="n")
    base.update(kw)
    return ledger.record(**base)


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir("ledger-" + self._testMethodName)
        self.p = os.path.join(self.d, "l.mjl")

    def test_append_and_read(self):
        for i in range(1, 6):
            self.assertEqual(ledger.append(self.p, rec(i)), i)
        chain = ledger.read(self.p)
        self.assertEqual([s for s, _, _ in chain], [1, 2, 3, 4, 5])
        self.assertEqual(chain[2][1]["dream"], "d-3")

    def test_absent_is_empty(self):
        self.assertEqual(ledger.read(self.p), [])

    def test_tamper_is_caught_at_the_line(self):
        for i in range(1, 4):
            ledger.append(self.p, rec(i))
        data = open(self.p, "rb").read().replace(b"d-2", b"d-9")
        open(self.p, "wb").write(data)
        with self.assertRaises(ledger.ChainBroken) as cm:
            ledger.read(self.p)
        self.assertEqual(cm.exception.line, 2)
        with self.assertRaises(ledger.ChainBroken):
            ledger.append(self.p, rec(4))

    def test_torn_line_refuses_append(self):
        ledger.append(self.p, rec(1))
        with open(self.p, "ab") as f:
            f.write(b"MJL1|2|abc")
        with self.assertRaises(ledger.ChainBroken):
            ledger.append(self.p, rec(2))

    def test_reorder_and_delete_caught(self):
        for i in range(1, 4):
            ledger.append(self.p, rec(i))
        lines = open(self.p, "rb").read().splitlines(True)
        open(self.p, "wb").write(lines[0] + lines[2])
        with self.assertRaises(ledger.ChainBroken):
            ledger.read(self.p)

    def test_bad_fields_refused(self):
        for bad in (dict(dream="-x"), dict(when="2026-11-17 12:00"), dict(trigger="Nap"), dict(notes="a|b"),
                    dict(score_after=10001), dict(outcome="done"), dict(inputs=",".join("r%d" % i for i in range(17)))):
            with self.assertRaises(MjError):
                ledger.append(self.p, rec(1, **bad))
        self.assertFalse(os.path.exists(self.p))

    def test_c_verifier_accepts_python_ledger(self):
        exe = tool("mj_ledger_verify")
        if not exe:
            self.skipTest("C tools not built")
        for i in range(1, 8):
            ledger.append(self.p, rec(i, inputs="run-a,run-b", score_before=12, score_after=10000, shift_bp=3))
        code, out = run([exe, self.p])
        self.assertEqual(code, 0, out)
        self.assertIn("OK 7 records", out)
        data = open(self.p, "rb").read()
        open(self.p, "wb").write(data.replace(b"run-b", b"run-c", 1))
        code, out = run([exe, self.p])
        self.assertEqual(code, 1, out)


if __name__ == "__main__":
    unittest.main()
