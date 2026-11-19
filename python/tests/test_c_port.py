"""Parity between the C port and the Python real-weight path.

Each check runs the same steps both ways and compares what lands on disk:
delta files byte for byte, store records, state files and ledger fields
byte for byte, and the edited weights element by element (the only place
the two may differ: torch sums a float32 matmul in its own order, C sums
in double; any difference is one bf16 step and is counted and printed).

Skipped when torch is missing or the C tools are not built.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os
import shutil
import stat
import struct
import unittest

from mjolnir import ledger
from mjolnir.common import sha256_file
from tests.util import has_torch, run, tmpdir, tool

MF = "model-00001-of-00001.safetensors"
NAMES = ["model.language_model.layers.0.mlp.down_proj.weight", "model.language_model.layers.0.mlp.up_proj.weight",
         "model.language_model.layers.1.mlp.down_proj.weight"]
ROWS, COLS, RANK = 96, 160, 16


def write_model(folder, seed=11):
    import torch
    os.makedirs(folder, exist_ok=True)
    g = torch.Generator().manual_seed(seed)
    tensors = {n: torch.randn(ROWS, COLS, generator=g) * 0.05 for n in NAMES}
    tensors["model.language_model.norm.weight"] = torch.ones(ROWS)
    header, blobs, off = {"__metadata__": {"format": "pt"}}, [], 0
    for k, t in tensors.items():
        b = t.to(torch.bfloat16).view(torch.uint16).numpy().astype("<u2").tobytes()
        header[k] = {"dtype": "BF16", "shape": list(t.shape), "data_offsets": [off, off + len(b)]}
        blobs.append(b)
        off += len(b)
    import json
    h = json.dumps(header).encode()
    h += b" " * ((8 - len(h) % 8) % 8)
    with open(os.path.join(folder, MF), "wb") as f:
        f.write(struct.pack("<Q", len(h)) + h + b"".join(blobs))
    with open(os.path.join(folder, "config.json"), "w") as f:
        f.write("{}")


def unlock(folder):
    if os.path.isdir(folder):
        for n in os.listdir(folder):
            os.chmod(os.path.join(folder, n), stat.S_IWRITE | stat.S_IREAD)


def bf16_steps(a, b):
    """Elements that differ, and the largest difference in bf16 steps."""
    import numpy as np
    x = np.frombuffer(a, dtype="<u2").astype(np.int32)
    y = np.frombuffer(b, dtype="<u2").astype(np.int32)
    # map sign-magnitude to a monotonic integer line
    x = np.where(x & 0x8000, 0x8000 - (x & 0x7FFF), 0x8000 + x)
    y = np.where(y & 0x8000, 0x8000 - (y & 0x7FFF), 0x8000 + y)
    d = np.abs(x - y)
    return int((d != 0).sum()), int(d.max()) if d.size else 0


def slurp(path):
    with open(path, "rb") as f:
        return f.read()


@unittest.skipUnless(has_torch() and tool("mj_real"), "needs torch, numpy and the C tools")
class CPortParity(unittest.TestCase):
    def setUp(self):
        self.real = tool("mj_real")
        self.d = tmpdir("cport-" + self._testMethodName)

    def tearDown(self):
        for side in os.listdir(self.d):
            unlock(os.path.join(self.d, side, "original"))

    def side(self, name):
        root = os.path.join(self.d, name)
        o, w, data = (os.path.join(root, x) for x in ("original", "working", "data"))
        write_model(o)
        return o, w, data

    def c(self, *args):
        code, out = run([self.real] + list(args))
        return code, out

    # ---- delta files ----
    def test_delta_bytes(self):
        import random
        import torch
        from mjolnir.real import delta
        rnd = random.Random(5)
        scales = [0.5, 0.1, 1e-05, 1e16, 1e15, -0.0, 0.0001, 1 / 3, 0.02, 2.5e-308, 5e-324, 1.7976931348623157e308,
                  9.900000000000001e-05, 123456789012345678.0]
        scales += [rnd.uniform(-1, 1) * 10 ** rnd.randint(-30, 30) for _ in range(120)]
        g = torch.Generator().manual_seed(1)
        entries = []
        for i, s in enumerate(scales):
            r, c, k = rnd.randint(1, 9), rnd.randint(1, 9), rnd.randint(1, 4)
            entries.append(("t%d.w_%d" % (i, k), torch.randn(r, k, generator=g), torch.randn(k, c, generator=g), s))
        for chunk in range(0, len(entries), 60):
            blob = delta.pack("ab" * 32, entries[chunk:chunk + 60])
            src = os.path.join(self.d, "py-%d.mjd" % chunk)
            dst = os.path.join(self.d, "c-%d.mjd" % chunk)
            with open(src, "wb") as f:
                f.write(blob)
            code, out = self.c("delta-repack", src, dst)
            self.assertEqual(code, 0, out)
            with open(dst, "rb") as f:
                self.assertEqual(f.read(), blob, "C packs the same entries into different bytes")
        code, out = self.c("delta-info", src)
        self.assertEqual(code, 0, out)
        self.assertIn("entries", out)

    # ---- a whole session, both ways ----
    def test_session(self):
        import torch
        from mjolnir.real import delta, safet, session, workcopy
        from mjolnir.store import Store
        po, pw, pd = self.side("py")
        co, cw, cd = self.side("c")
        go, gw, gd = self.side("gpu")
        quiet = dict(log=lambda *a: None)
        o_sha = sha256_file(os.path.join(po, MF))
        self.assertEqual(o_sha, sha256_file(os.path.join(co, MF)))

        session.register(po, pw, pd, **quiet)
        for o, w, d in ((co, cw, cd), (go, gw, gd)):
            code, out = self.c("register", "--original", o, "--working", w, "--data", d)
            self.assertEqual(code, 0, out)
            self.assertFalse(os.access(os.path.join(o, MF), os.W_OK))
        # base record, state file, ledger fields
        pst, cst = session.real_store(pd), session.real_store(cd)
        self.assertEqual(pst.get("base"), cst.get("base"))
        self.assertEqual(pst.info("base")["kind"], cst.info("base")["kind"])
        self.assertEqual(pst.info("base")["label"], cst.info("base")["label"])
        for f in ("MJOLNIR-STATE.txt", MF, "config.json"):
            self.assertEqual(slurp(os.path.join(pw, f)), slurp(os.path.join(cw, f)), f)

        # the same deltas into all three stores
        g = torch.Generator().manual_seed(7)
        base = session.base_sha(pd)
        d1 = delta.pack(base, [(NAMES[0], torch.randn(ROWS, RANK, generator=g), torch.randn(RANK, COLS, generator=g),
                                0.02)])
        d2 = delta.pack(base, [(NAMES[1], torch.randn(ROWS, RANK, generator=g), torch.randn(RANK, COLS, generator=g),
                                0.05),
                               (NAMES[0], torch.randn(ROWS, 4, generator=g), torch.randn(4, COLS, generator=g), -0.1)])
        for d in (pd, cd, gd):
            st = Store(session.paths(d)["store"], max_store_bytes=8 << 30, max_blob_bytes=1 << 30)
            st.put("e1", "candidate", "base", "first", d1)
            st.put("e2", "candidate", "e1", "second", d2)

        session.apply(po, pw, pd, "e2", **quiet)
        code, out = self.c("apply", "--original", co, "--working", cw, "--data", cd, "--version", "e2", "--cpu")
        self.assertEqual(code, 0, out)
        code, gout = self.c("apply", "--original", go, "--working", gw, "--data", gd, "--version", "e2")
        self.assertEqual(code, 0, gout)
        print("\n  C apply (GPU run): " + gout.strip().replace("\n", " | "))

        self.assertEqual(slurp(os.path.join(pw, "MJOLNIR-STATE.txt")),
                         slurp(os.path.join(cw, "MJOLNIR-STATE.txt")))
        h, _ = safet.read_header(os.path.join(pw, MF))
        total_diff, worst, total = 0, 0, 0
        pf, cf, gf = (slurp(os.path.join(w, MF)) for w in (pw, cw, gw))
        self.assertEqual(cf, gf, "the CPU and GPU paths must give the same bytes")
        self.assertEqual(pf[:h[NAMES[0]]["abs_offset"]], cf[:h[NAMES[0]]["abs_offset"]])
        for name, info in sorted(h.items()):
            a, b = info["abs_offset"], info["abs_offset"] + info["nbytes"]
            n, m = bf16_steps(pf[a:b], cf[a:b])
            total_diff, worst, total = total_diff + n, max(worst, m), total + info["nbytes"] // 2
            if name not in NAMES[:2]:
                self.assertEqual(n, 0, name)  # untouched tensors: identical
        print("  edited weights, Python vs C: %d of %d elements differ, at most %d bf16 step(s)"
              % (total_diff, total, worst))
        self.assertLessEqual(worst, 1)
        self.assertLess(total_diff, total // 100)

        # ledgers: same records apart from ids, times and hashes
        pl, cl = ledger.read(session.paths(pd)["ledger"]), ledger.read(session.paths(cd)["ledger"])
        self.assertEqual(len(pl), len(cl))
        for (_, a, _), (_, b, _) in zip(pl, cl):
            for k in a:
                if k in ("dream", "when"):
                    self.assertEqual(a[k].split("-")[0], b[k].split("-")[0])
                else:
                    self.assertEqual(a[k], b[k], k)

        # roll back both ways: byte-identical to the original
        session.apply(po, pw, pd, "base", **quiet)
        code, out = self.c("apply", "--original", co, "--working", cw, "--data", cd, "--version", "base")
        self.assertEqual(code, 0, out)
        self.assertEqual(sha256_file(os.path.join(pw, MF)), o_sha)
        self.assertEqual(sha256_file(os.path.join(cw, MF)), o_sha)
        self.assertEqual(sha256_file(os.path.join(co, MF)), o_sha)

        # each side reads what the other wrote
        self.assertEqual(workcopy.read_state(cw)["applied"], "base")
        session.apply(co, cw, cd, "e1", **quiet)  # Python applies onto the C working copy
        code, out = self.c("apply", "--original", po, "--working", pw, "--data", pd, "--version", "e1")
        self.assertEqual(code, 0, out)  # and C onto the Python one
        code, out = self.c("status", "--working", cw)
        self.assertEqual(code, 0, out)
        self.assertIn("applied e1", out)
        verify = tool("mj_ledger_verify")
        for d in (pd, cd):
            code, out = run([verify, session.paths(d)["ledger"]])
            self.assertEqual(code, 0, out)

        # real-verify, both ways, same answer
        sums = os.path.join(self.d, "sums.txt")
        with open(sums, "wb") as f:
            f.write(workcopy.format_sums(workcopy.checksums(po)))
        code, out = self.c("verify", "--dir", co, "--sums", sums)
        self.assertEqual((code, out.strip()), (0, "OK 2 files match"))
        code, out = self.c("verify", "--dir", cw, "--sums", sums)
        self.assertEqual(code, 1)
        self.assertTrue(out.startswith("MISMATCH: "), out)

    # ---- the low-rank edit ----
    def test_low_rank(self):
        import json
        import numpy as np
        import torch
        from mjolnir.real import edit
        g = torch.Generator().manual_seed(21)
        n, out, d, m = 24, 160, 384, 300
        k = torch.randn(n, d, generator=g)
        o = torch.randn(n, out, generator=g) * 3
        gr = torch.randn(n, out, generator=g) * 0.01
        bg = torch.randn(m, d, generator=g) * torch.linspace(0.2, 2.0, d)  # uneven directions
        grads = {"k": list(k), "o": list(o), "g": list(gr)}

        def save(path, tensors):
            header, blobs, off = {}, [], 0
            for name, t in tensors.items():
                b = t.contiguous().numpy().astype("<f4").tobytes()
                header[name] = {"dtype": "F32", "shape": list(t.shape), "data_offsets": [off, off + len(b)]}
                blobs.append(b)
                off += len(b)
            h = json.dumps(header).encode()
            with open(path, "wb") as f:
                f.write(struct.pack("<Q", len(h)) + h + b"".join(blobs))

        def load(path):
            raw = slurp(path)
            hl = struct.unpack("<Q", raw[:8])[0]
            h = json.loads(raw[8:8 + hl])
            return {name: torch.from_numpy(np.frombuffer(raw, dtype="<f4", count=int(np.prod(t["shape"])),
                                                         offset=8 + hl + t["data_offsets"][0]).copy()).reshape(t["shape"])
                    for name, t in h.items()}

        for label, background, rank, extra in (("plain", None, 8, []), ("whitened", bg, 8, ["--shared", "64"]),
                                               ("whitened, full rank", bg, n, ["--shared", "64"])):
            U, V = edit.low_rank(grads, rank, None if background is None else edit.with_shared_key(background, grads["k"]))
            want = (U.double() @ V.double())
            inp = os.path.join(self.d, "lr-in-%s.safetensors" % label.replace(" ", "").replace(",", ""))
            save(inp, {"k": k, "o": o, "g": gr, **({} if background is None else {"background": background})})
            for dev in ([], ["--cpu"]):
                outp = inp.replace("lr-in", "lr-out" + "".join(dev))
                code, msg = self.c("lowrank", inp, outp, "--rank", str(rank), *extra, *dev)
                self.assertEqual(code, 0, msg)
                got = load(outp)
                prod = got["U"].double() @ got["V"].double()
                err = float((prod - want).norm() / want.norm())
                print("\n  low rank (%s, rank %d, %s): relative difference from Python %.2e; %s"
                      % (label, rank, "CPU" if dev else "GPU if present", err, msg.strip()))
                self.assertLess(err, 1e-4)

    def test_refusals_match(self):
        from mjolnir.real import session
        o, w, d = self.side("r")
        code, out = self.c("register", "--original", o, "--working", o, "--data", d)
        self.assertEqual(code, 2, out)
        code, out = self.c("register", "--original", o, "--working", os.path.join(o, "inner"), "--data", d)
        self.assertEqual(code, 2, out)
        code, out = self.c("register", "--original", o, "--working", w, "--data", d)
        self.assertEqual(code, 0, out)
        code, out = self.c("apply", "--original", o, "--working", w, "--data", d, "--version", "missing")
        self.assertEqual(code, 2, out)
        self.assertEqual(session.base_sha(d), sha256_file(os.path.join(o, MF)))


if __name__ == "__main__":
    unittest.main()
