"""Store: versions, refusals, rollback, and agreement with the C store.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import struct
import unittest

from mjolnir import ledger, toy
from mjolnir.common import Corrupt, Refused
from mjolnir.store import Store, format_manifest, parse_manifest
from tests.util import run, tmpdir, tool


class StoreTest(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir("store-" + self._testMethodName)
        self.s = Store(os.path.join(self.d, "s"), min_free_bytes=0)

    def test_put_get_head_rollback(self):
        a = self.s.put("v1", "milestone", "-", "first", b"alpha")
        b = self.s.put("v2", "candidate", "v1", "second", b"beta")
        self.assertEqual((a["seq"], b["seq"]), (1, 2))
        self.assertEqual(self.s.get("v2"), b"beta")
        self.assertIsNone(self.s.head())
        self.s.set_head("v2")
        self.assertEqual(self.s.head(), "v2")
        self.s.rollback("v1")
        self.assertEqual(self.s.head(), "v1")
        self.assertEqual([v["id"] for v in self.s.chain("v2")], ["v1", "v2"])
        self.assertTrue(os.path.exists(os.path.join(self.s.root, "pins", "v1.pin")))

    def test_no_overwrite_ever(self):
        self.s.put("v1", "nightly", "-", "", b"x")
        with self.assertRaises(FileExistsError):
            self.s.put("v1", "nightly", "-", "", b"y")
        os.rename(os.path.join(self.s.root, "versions", "v1.ver"), os.path.join(self.s.root, "retired", "v1.ver"))
        with self.assertRaises(FileExistsError):
            self.s.put("v1", "nightly", "-", "", b"z")

    def test_model_files_refused(self):
        st = struct.pack("<Q", 2) + b"{}"
        for data in (b"GGUF....", b"PK\x03\x04rest", st):
            with self.assertRaises(Refused):
                self.s.put("m", "candidate", "-", "", data)

    def test_corrupt_blob_caught(self):
        v = self.s.put("v1", "nightly", "-", "", b"payload")
        p = os.path.join(self.s.root, "objects", v["sha256"] + ".blob")
        os.chmod(p, 0o644)
        open(p, "wb").write(b"payloaX")
        with self.assertRaises(Corrupt):
            self.s.get("v1")
        with self.assertRaises(Corrupt):
            self.s.set_head("v1")

    def test_manifest_strict(self):
        v = {"id": "a", "seq": 3, "kind": "nightly", "sha256": "0" * 64, "size": 5, "crc32": 7, "parent": "-",
             "label": "x"}
        m = format_manifest(v)
        self.assertEqual(parse_manifest(m), v)
        for bad in (m.replace(b"seq=3", b"seq=03"), m.replace(b"label=x", b"label=y"), m + b"\n", m[:-1]):
            with self.assertRaises(Corrupt):
                parse_manifest(bad)

    def test_c_tools_use_a_python_store(self):
        night, verify = tool("mj_nightly"), tool("mj_store_verify")
        if not night or not verify:
            self.skipTest("C tools not built")
        self.s.put("toy-seed", "milestone", "-", "zero toy policy", toy.save([[0] * 8 for _ in range(4)]))
        self.s.set_head("toy-seed")
        code, out = run([verify, self.s.root])
        self.assertEqual(code, 0, out)
        self.assertIn("OK 1 versions, head toy-seed", out)
        lp = os.path.join(self.d, "dreams.mjl")
        code, out = run([night, "--dry-run", "--store", self.s.root, "--ledger", lp, "--id", "py-night"])
        self.assertEqual(code, 0, out)
        # Python reads back what C wrote
        self.assertGreaterEqual(len(self.s.versions()), 3)
        self.assertEqual(self.s.verify_all(), len(self.s.versions()))
        self.assertEqual(ledger.read(lp)[-1][1]["dream"], "py-night")
        cand = [v for v in self.s.versions() if v["kind"] == "candidate"][0]
        self.assertEqual(len(toy.load(self.s.get(cand["id"]))), 4)

    def test_toy_blob_round_trip(self):
        w = [[(a * 8 + i - 16) * 4096 for i in range(8)] for a in range(4)]
        b = toy.save(w)
        self.assertEqual(len(b), toy.BLOB_LEN)
        self.assertEqual(toy.load(b), w)
        with self.assertRaises(Corrupt):
            toy.load(b[:-1] + bytes([b[-1] ^ 1]))


if __name__ == "__main__":
    unittest.main()
