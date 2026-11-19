"""A nap for the toy policy, trained on the workstation GPU from real replays.

The dream follows the M0 cycle on the toy model:
  1. load and check the live toy weights (HEAD of the toy store)
  2. snapshot them, so the dream can always be rolled back
  3. build the dream batch: replay steps with JD's grade where he gave one,
     otherwise the automatic partial-credit label, with older memories (the
     golden set) mixed in after every new sample (design principle 5)
  4. train a float copy on the GPU: raise the likelihood of what was
     productive and lower the likelihood of what was not (design principle 3),
     clamping every weight to the step cap after every step (design principle 4)
  5. round to Q16 and run the four M0 gates: step cap, behavior shift on the
     golden set, golden score not lower, task score higher
  6. save the candidate and write the ledger line; HEAD only moves when the
     caller asks to integrate and every gate passed

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import hashlib
import time

from . import ledger, toy
from .common import MjError, utc_now, utc_stamp
from .grades import AUTO_REWARD, REWARD


def build_batch(runs, grade_map):
    samples, sources = [], set()
    for r in runs:
        for x, a, s in toy.features(r["steps"]):
            g = grade_map.get((r["id"], s["index"]))
            if g:
                rew, who = REWARD[g[0]], "JD"
            else:
                rew, who = AUTO_REWARD[s["auto"]], "auto"
            samples.append((x, a, rew, who))
            sources.add(r["id"])
    return samples, sorted(sources)


def golden_from(runs):
    """Older memories: the clean forward steps of held-out runs."""
    gold = []
    for r in runs:
        for x, a, s in toy.features(r["steps"]):
            if s["auto"] in ("good", "productive"):
                gold.append((x, a))
    return gold


def task_bp(w, samples):
    if not samples:
        return 0
    ok = 0
    for x, a, rew, _ in samples:
        hit = toy.act(w, x) == a
        ok += hit if rew > 0 else (not hit)
    return ok * 10000 // len(samples)


def golden_bp(w, gold):
    return (sum(toy.act(w, x) == a for x, a in gold) * 10000 // len(gold)) if gold else 0


def shift_bp(w0, w1, gold):
    return (sum(toy.act(w0, x) != toy.act(w1, x) for x, _ in gold) * 10000 // len(gold)) if gold else 0


def train(w0, samples, gold, device="cuda", steps=300, lr=0.5, step_cap=0.5, replay_old_per_new=1, seed=1):
    import torch
    if device == "cuda" and not torch.cuda.is_available():
        raise MjError("CUDA is not available; pass --device cpu to train on the processor")
    g = torch.Generator().manual_seed(seed)
    base = torch.tensor(w0, dtype=torch.float32) / 65536.0
    xs = [s[0] for s in samples] + [x for x, _ in gold] * replay_old_per_new
    ys = [s[1] for s in samples] + [a for _, a in gold] * replay_old_per_new
    rs = [s[2] for s in samples] + [1.0] * (len(gold) * replay_old_per_new)
    X = torch.tensor(xs, dtype=torch.float32).to(device) / 127.0
    Y = torch.tensor(ys, dtype=torch.long).to(device)
    R = torch.tensor(rs, dtype=torch.float32).to(device)
    W0 = base.to(device)
    W = W0.clone().requires_grad_(True)
    opt = torch.optim.SGD([W], lr=lr, momentum=0.9)
    n = X.shape[0]
    bs = min(256, n)
    for _ in range(steps):
        idx = torch.randint(0, n, (bs,), generator=g).to(device)
        logp = torch.log_softmax(X[idx] @ W.t() * 4.0, dim=-1).gather(1, Y[idx, None]).squeeze(1)
        r = R[idx]
        # strengthen: -r log p.  weaken: -|r| log(1 - p), which fades once the
        # action is unlikely instead of pushing every other action up forever
        weaken = torch.log1p(-logp.exp().clamp(max=1 - 1e-6))
        loss = -torch.where(r > 0, r * logp, -r * weaken).mean()
        opt.zero_grad()
        loss.backward()
        opt.step()
        with torch.no_grad():
            W.copy_(torch.max(torch.min(W, W0 + step_cap), W0 - step_cap))
            W.clamp_(-63.0, 63.0)
    if device == "cuda":
        torch.cuda.synchronize()
    q = (W.detach().cpu() * 65536.0).round().to(torch.int64).tolist()
    return [[int(v) for v in row] for row in q], float(loss.detach().cpu())


def nap(store, ledger_path, runs, golden_runs, grade_map, dream_id=None, device="cuda", integrate=False,
        steps=300, lr=0.5, step_cap=0.5, max_shift_bp=2000, seed=1):
    t0 = time.time()
    dream_id = dream_id or ("toy-nap-" + utc_stamp())
    ledger.read(ledger_path)  # refuse to dream on a broken chain
    live = store.head()
    if live is None:
        raise MjError("the toy store has no live version; seed it first (toy-seed)")
    w0 = toy.load(store.get(live))
    samples, sources = build_batch(runs, grade_map)
    gold = golden_from(golden_runs)
    if not samples or not gold:
        raise MjError("no replay steps or no golden steps to dream on")
    snap = dream_id + ".snap"
    store.put(snap, "snapshot", live, "before " + dream_id, toy.save(w0))
    w1, loss = train(w0, samples, gold, device=device, steps=steps, lr=lr, step_cap=step_cap, seed=seed)
    cap_q = int(step_cap * 65536) + 1
    moved = max(abs(w1[a][i] - w0[a][i]) for a in range(toy.A) for i in range(toy.F))
    sb, sa = task_bp(w0, samples), task_bp(w1, samples)
    gb, ga = golden_bp(w0, gold), golden_bp(w1, gold)
    sh = shift_bp(w0, w1, gold)
    gates = {"step": moved <= cap_q, "shift": sh <= max_shift_bp, "golden": ga >= gb, "task": sa > sb}
    gate_text = ",".join("%s=%s" % (k, "pass" if v else "fail") for k, v in gates.items())
    cand = dream_id + ".cand"
    store.put(cand, "candidate", live, "toy nap on %d replay steps" % len(samples), toy.save(w1))
    passed = all(gates.values())
    if passed and integrate:
        store.set_head(cand)
        outcome = "integrated"
    else:
        outcome = "dry-run-pass" if passed else "rejected"
    failed = [k for k, v in gates.items() if not v]
    notes = ("failed: " + ",".join(failed)) if failed else "-"
    if len(sources) <= 16:
        inputs = ",".join(sources)
    else:
        inputs = "set-" + hashlib.sha256(",".join(sources).encode()).hexdigest()[:60]
    graded_by_jd = sum(1 for s in samples if s[3] == "JD")
    edit = "gpu=%s steps=%d lr=%g cap=%g samples=%d jd=%d gold=%d loss=%.4f" % (
        device, steps, lr, step_cap, len(samples), graded_by_jd, len(gold), loss)
    rec = ledger.record(dream=dream_id, when=utc_now(), trigger="nap", grader="JD" if graded_by_jd else "auto-maze",
                        inputs=inputs, before=live, candidate=cand, edit=edit, score_before=sb, score_after=sa,
                        shift_bp=sh, gates=gate_text, outcome=outcome, notes=notes)
    seq = ledger.append(ledger_path, rec)
    return {"dream": dream_id, "outcome": outcome, "gates": gates, "task_before": sb, "task_after": sa,
            "golden_before": gb, "golden_after": ga, "shift_bp": sh, "max_move_q16": moved, "samples": len(samples),
            "jd_graded": graded_by_jd, "gold": len(gold), "ledger_seq": seq, "seconds": round(time.time() - t0, 2),
            "candidate": cand, "live": store.head()}
