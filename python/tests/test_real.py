"""The real-weight path on a tiny stand-in model file: copy, deltas, apply,
roll back, and the refusals that protect the original.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import json
import os
import stat
import struct
import unittest

from mjolnir import ledger
from mjolnir.common import Corrupt, Refused, sha256_file
from tests.util import has_torch, run, tmpdir, tool

NAME_A = "model.language_model.layers.0.mlp.down_proj.weight"
NAME_B = "model.language_model.layers.1.mlp.down_proj.weight"


def write_fake_model(folder, rows=8, cols=12):
    import torch
    os.makedirs(folder, exist_ok=True)
    g = torch.Generator().manual_seed(3)
    tensors = {NAME_A: torch.randn(rows, cols, generator=g), NAME_B: torch.randn(rows, cols, generator=g),
               "model.language_model.norm.weight": torch.ones(rows)}
    header, blobs, off = {}, [], 0
    for k, t in tensors.items():
        b = t.to(torch.bfloat16).view(torch.uint16).numpy().astype("<u2").tobytes()
        header[k] = {"dtype": "BF16", "shape": list(t.shape), "data_offsets": [off, off + len(b)]}
        blobs.append(b)
        off += len(b)
    h = json.dumps(header).encode()
    with open(os.path.join(folder, "model-00001-of-00001.safetensors"), "wb") as f:
        f.write(struct.pack("<Q", len(h)) + h + b"".join(blobs))
    with open(os.path.join(folder, "config.json"), "w") as f:
        f.write("{}")


@unittest.skipUnless(has_torch(), "torch and numpy are not installed")
class RealPathTest(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir("real-" + self._testMethodName)
        self.orig = os.path.join(self.d, "original")
        self.work = os.path.join(self.d, "working")
        self.data = os.path.join(self.d, "data")
        write_fake_model(self.orig)

    def tearDown(self):
        for n in os.listdir(self.orig):  # let the next run clean up
            os.chmod(os.path.join(self.orig, n), stat.S_IWRITE | stat.S_IREAD)

    def _delta(self, scale=0.5, name=NAME_A):
        import torch
        from mjolnir.real import delta, session
        U = torch.randn(8, 2, generator=torch.Generator().manual_seed(5))
        V = torch.randn(2, 12, generator=torch.Generator().manual_seed(6))
        return delta.pack(session.base_sha(self.data), [(name, U, V, scale)])

    def test_register_apply_rollback(self):
        from mjolnir.real import safet, session, workcopy
        mf = "model-00001-of-00001.safetensors"
        o_sha = sha256_file(os.path.join(self.orig, mf))
        session.register(self.orig, self.work, self.data, log=lambda *a: None)
        self.assertFalse(os.access(os.path.join(self.orig, mf), os.W_OK))  # original marked read-only
        self.assertEqual(sha256_file(os.path.join(self.work, mf)), o_sha)
        st = session.real_store(self.data)
        st.put("e1", "candidate", "base", "test edit", self._delta())
        session.apply(self.orig, self.work, self.data, "e1", log=lambda *a: None)
        w_file = os.path.join(self.work, mf)
        self.assertNotEqual(sha256_file(w_file), o_sha)
        h, _ = safet.read_header(w_file)
        a0 = safet.read_tensor(os.path.join(self.orig, mf), h[NAME_A])
        a1 = safet.read_tensor(w_file, h[NAME_A])
        b0 = safet.read_tensor(os.path.join(self.orig, mf), h[NAME_B])
        b1 = safet.read_tensor(w_file, h[NAME_B])
        self.assertGreater(float((a1 - a0).abs().max()), 0.01)
        self.assertEqual(float((b1 - b0).abs().max()), 0.0)  # untouched tensor stays bit-identical
        self.assertEqual(workcopy.read_state(self.work)["applied"], "e1")
        # a second edit on top, then roll back to the base
        st.put("e2", "candidate", "e1", "second", self._delta(0.25, NAME_B))
        touched = session.apply(self.orig, self.work, self.data, "e2", log=lambda *a: None)
        self.assertEqual(touched, sorted([NAME_A, NAME_B]))
        session.apply(self.orig, self.work, self.data, "base", log=lambda *a: None)
        self.assertEqual(sha256_file(w_file), o_sha)
        self.assertEqual(sha256_file(os.path.join(self.orig, mf)), o_sha)
        outcomes = [r["outcome"] for _, r, _ in ledger.read(session.paths(self.data)["ledger"])]
        self.assertEqual(outcomes, ["integrated", "integrated", "integrated", "rolled-back"])
        verify = tool("mj_store_verify")
        if verify:
            code, out = run([verify, session.paths(self.data)["store"]])
            self.assertEqual(code, 0, out)
            self.assertIn("head base", out)

    def test_working_must_differ_from_original(self):
        from mjolnir.real import workcopy
        with self.assertRaises(Refused):
            workcopy.make_copy(self.orig, self.orig)
        with self.assertRaises(Refused):
            workcopy.make_copy(self.orig, os.path.join(self.orig, "inside"))

    def test_delta_for_other_model_refused(self):
        import torch
        from mjolnir.real import delta, session
        session.register(self.orig, self.work, self.data, log=lambda *a: None)
        st = session.real_store(self.data)
        blob = delta.pack("1" * 64, [(NAME_A, torch.ones(8, 1), torch.ones(1, 12), 1.0)])
        st.put("x", "candidate", "base", "", blob)
        with self.assertRaises(Refused):
            session.apply(self.orig, self.work, self.data, "x", log=lambda *a: None)
        blob = delta.pack(session.base_sha(self.data), [(NAME_A, torch.ones(9, 1), torch.ones(1, 12), 1.0)])
        st.put("y", "candidate", "base", "", blob)
        with self.assertRaises(Refused):
            session.apply(self.orig, self.work, self.data, "y", log=lambda *a: None)

    def test_hard_link_refused(self):
        from mjolnir.real import workcopy
        os.makedirs(self.work)
        mf = "model-00001-of-00001.safetensors"
        os.link(os.path.join(self.orig, mf), os.path.join(self.work, mf))
        with self.assertRaises(Refused):
            workcopy._model_paths(self.orig, self.work)

    def test_delta_format(self):
        import torch
        from mjolnir.real import delta
        U, V = torch.randn(5, 3), torch.randn(3, 7)
        blob = delta.pack("a" * 64, [("t.w", U, V, 0.125)])
        base, entries = delta.unpack(blob)
        self.assertEqual(base, "a" * 64)
        self.assertTrue(torch.equal(entries[0][1], U))
        self.assertAlmostEqual(entries[0][3], 0.125)
        self.assertTrue(torch.allclose(delta.dense(entries[0]), U @ V * 0.125))
        for bad in (blob[:-1] + bytes([blob[-1] ^ 1]), blob[:40] + b"X" + blob[41:]):
            with self.assertRaises(Corrupt):
                delta.unpack(bad)
        from mjolnir.store import payload_is_model_file
        self.assertFalse(payload_is_model_file(blob))

    def test_low_rank_matches_dense(self):
        import torch
        from mjolnir.real.edit import low_rank
        g = torch.Generator().manual_seed(9)
        grads = {"g": [torch.randn(6, generator=g) for _ in range(3)],
                 "o": [torch.randn(6, generator=g) for _ in range(3)],
                 "k": [torch.randn(10, generator=g) for _ in range(3)]}
        U, V = low_rank(grads, rank=3)
        G = torch.stack([gi / gi.norm() * oi.norm() for gi, oi in zip(grads["g"], grads["o"])], dim=1)
        K = torch.stack([k / k.norm() ** 2 for k in grads["k"]], dim=0)
        self.assertTrue(torch.allclose(U @ V, G @ K, atol=1e-5))
        U1, V1 = low_rank(grads, rank=1)
        self.assertEqual(tuple(U1.shape), (6, 1))

    def test_whitened_keys(self):
        import torch
        from mjolnir.real.edit import low_rank, whiten
        g = torch.Generator().manual_seed(4)
        common = torch.randn(10, generator=g)
        B = torch.stack([common * 3 + 0.1 * torch.randn(10, generator=g) for _ in range(40)])
        K = torch.stack([common * 3 + torch.randn(10, generator=g) for _ in range(2)])
        lam = 0.1 * float((B * B).sum()) / B.numel()
        C = lam * torch.eye(10) + B.t() @ B / 40
        self.assertTrue(torch.allclose(whiten(K, B), K @ torch.linalg.inv(C).t(), rtol=1e-3, atol=1e-4))
        grads = {"g": [torch.randn(6, generator=g) for _ in range(2)], "o": [torch.ones(6)] * 2, "k": list(K)}
        U, V = low_rank(grads, rank=2, background=B)
        # an edited input still gets its full response; ordinary text gets much less
        hit = (U @ V @ K[0]).norm()
        quiet = (U @ V @ B[0]).norm()
        self.assertGreater(float(hit), 5 * float(quiet))


if __name__ == "__main__":
    unittest.main()
