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


def _rd(p):
    with open(p, "rb") as fh:
        return fh.read()


def _wr(p, data):
    with open(p, "wb") as fh:
        fh.write(data)


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


    # ---- one writer at a time, shared with C ----
    def test_token_held_refuses_and_changes_nothing(self):
        ledger.append(self.p, rec(1))
        before = _rd(self.p)
        tok = b"MJW1|1|1|elsewhere|2026-11-19T00:00:00Z|" + b"a" * 32 + b"\n"
        _wr(self.p + ".writer", tok)
        with self.assertRaises(ledger.Busy) as cm:
            ledger.append(self.p, rec(2))
        self.assertIn("--release-stale", str(cm.exception))
        self.assertEqual(_rd(self.p), before)
        self.assertEqual(_rd(self.p + ".writer"), tok)  # never taken over or removed

    def test_token_is_released_and_has_the_c_format(self):
        seen = {}
        real_append = ledger.append_durable

        def spy(path, data):
            seen["token"] = _rd(self.p + ".writer")
            real_append(path, data)

        ledger.append_durable = spy
        try:
            ledger.append(self.p, rec(1))
        finally:
            ledger.append_durable = real_append
        self.assertFalse(os.path.exists(self.p + ".writer"))
        f = seen["token"].decode("ascii").rstrip("\n").split("|")
        self.assertEqual((f[0], int(f[1])), ("MJW1", os.getpid()))
        self.assertTrue(int(f[2]) > 0 and len(f[5]) == 32)
        self.assertEqual([x for x in os.listdir(self.d) if "writer" in x], [])

    def test_failed_append_cuts_its_own_bytes(self):
        ledger.append(self.p, rec(1))
        before = _rd(self.p)
        real_append = ledger.append_durable

        def torn(path, data):
            real_append(path, data[: len(data) // 2])
            raise OSError("disk went away")

        ledger.append_durable = torn
        try:
            with self.assertRaises(OSError):
                ledger.append(self.p, rec(2))
        finally:
            ledger.append_durable = real_append
        self.assertEqual(_rd(self.p), before)
        self.assertFalse(os.path.exists(self.p + ".writer"))

    def test_c_sees_a_python_writer_as_running(self):
        t = tool("mj_ledger_verify")
        if t is None:
            self.skipTest("C tools not built")
        ledger.append(self.p, rec(1))
        tok = ledger._claim(self.p)
        try:
            rc, out = run([t, "--who", self.p])
            self.assertIn("still running", out)
            rc, out = run([t, "--release-stale", self.p])
            self.assertNotEqual(rc, 0)
            self.assertTrue(os.path.exists(self.p + ".writer"))  # a live Python writer is never cleared
            with self.assertRaises(ledger.Busy):
                ledger.append(self.p, rec(2))
        finally:
            ledger._release(self.p, tok)
        self.assertFalse(os.path.exists(self.p + ".writer"))
        self.assertEqual(ledger.append(self.p, rec(2)), 2)

if __name__ == "__main__":
    unittest.main()
