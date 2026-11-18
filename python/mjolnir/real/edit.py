"""A dream on the real model: work out an edit, try it in memory, gate it.

The edit method is a first-order, low-rank version of the rank-one model
editing idea from the open ROME paper (Meng et al., "Locating and Editing
Factual Associations in GPT", 2022), applied to the MLP output projection of
chosen layers, and reward-weighted as in JD's decision 3:

  for each replay example i with reward r_i (positive for good steps,
  negative for loops), at the last prompt position:
    k_i  the input to the layer's down projection
    o_i  its output
    g_i  the gradient of r_i * log p(answer_i) with respect to that output
  the candidate change is   dW = c * sum_i ||o_i|| * (g_i/||g_i||) (C^-1 k_i / k_i.C^-1 k_i)^T
  truncated to a small rank, so a stored edit is a few megabytes at most.

C is the covariance of the projection's inputs over background text (the golden
set and the other positions of the prompts), with a small ridge, as in ROME.
Whitening the key this way makes the edit answer to what is particular about
the edited inputs and stay quiet on ordinary text, which shares the common
directions. C^-1 is never formed: it is applied through the Woodbury identity.

The dream tries a few strengths c in memory only (an overlay on the forward
pass, nothing written), scores each on the task and on a golden set, and keeps
the strongest one that passes every gate. Only then is the edit stored as a
candidate version. Writing it into the working copy is a separate step.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import math
import random
import time

LETTERS = ["A", "B", "C", "D"]

GOLDEN_TEXTS = [
    "The player picks up the key, opens the door, and walks into the next room of the maze.",
    "In a grid maze, each move goes up, down, left or right unless a wall blocks the way.",
    "A good strategy is to remember where you have been so you do not walk in circles.",
    "The circuit has four switches; when all of them are on, the lamp lights up.",
    "Score one point for each coin collected and lose a life when you fall into a pit.",
    "The robot turned left at the corner and followed the corridor to the end.",
    "Level two adds moving platforms, so timing each jump matters more than speed.",
    "Chess is played on a board of sixty-four squares with two players taking turns.",
    "When the timer reaches zero, the round ends and the scores are added up.",
    "To solve the puzzle, slide the blocks until the red one reaches the exit.",
    "The game saves your progress at every checkpoint along the path.",
    "A wire carries the signal from the button to the door, which slides open.",
]


def step_prompt(s, here_visited):
    o = int(s.get("open") or 0)
    opened = [LETTERS[b] for b in range(4) if (o >> b) & 1] or ["none"]
    return ("Maze step. Moves are A, B, C and D. Open moves: %s. Holding the key: %s. "
            "Standing on ground visited before: %s. Best move:" % (
                ", ".join(opened), "yes" if s.get("key") else "no", "yes" if here_visited else "no"))


def examples_from(runs):
    out = []
    for r in runs:
        visited = set()
        for s in r["steps"]:
            hv = s["state"] in visited
            visited.add(s["state"])
            if s["action"] not in (0, 1, 2, 3):
                continue
            reward = {"good": 1.0, "productive": 1.5, "not-yet": -1.0}[s["auto"]]
            out.append({"prompt": step_prompt(s, hv), "answer": s["action"], "reward": reward, "run": r["id"]})
    return out


def dedupe(examples):
    """Keep one example per (prompt, answer, sign) so repeats do not dominate."""
    seen, out = set(), []
    for e in examples:
        k = (e["prompt"], e["answer"], e["reward"] > 0)
        if k not in seen:
            seen.add(k)
            out.append(e)
    return out


class Evaluator:
    def __init__(self, model, tokenizer, batch=24):
        import torch
        self.m, self.tok, self.batch = model, tokenizer, batch
        self.answer_ids = []
        for L in LETTERS:
            ids = tokenizer.encode(" " + L, add_special_tokens=False)
            if len(ids) != 1:
                raise RuntimeError("answer letter is not one token")
            self.answer_ids.append(ids[0])
        self.bos = tokenizer.bos_token_id
        self.torch = torch

    def _pad(self, texts):
        torch = self.torch
        seqs = [[self.bos] + self.tok.encode(t, add_special_tokens=False) for t in texts]
        n = max(len(s) for s in seqs)
        ids = torch.zeros((len(seqs), n), dtype=torch.long)
        mask = torch.zeros((len(seqs), n), dtype=torch.long)
        for i, s in enumerate(seqs):
            ids[i, : len(s)] = torch.tensor(s)
            mask[i, : len(s)] = 1
        return ids, mask, [len(s) for s in seqs]

    def answer_logprobs(self, examples):
        """Log-probabilities over the four answer letters, per example."""
        torch = self.torch
        out = []
        with torch.no_grad():
            for b in range(0, len(examples), self.batch):
                chunk = examples[b: b + self.batch]
                ids, mask, lens = self._pad([e["prompt"] for e in chunk])
                h = self.m.hidden(ids, mask)
                last = torch.stack([h[i, lens[i] - 1] for i in range(len(chunk))])
                logits = self.m.logits_at(last)[:, self.answer_ids]
                out.append(torch.log_softmax(logits, dim=-1).cpu())
        return torch.cat(out)

    def task(self, examples):
        lp = self.answer_logprobs(examples)
        good = [i for i, e in enumerate(examples) if e["reward"] > 0]
        bad = [i for i, e in enumerate(examples) if e["reward"] < 0]
        ans = self.torch.tensor([e["answer"] for e in examples])
        top = lp.argmax(dim=-1)
        res = {"n": len(examples)}
        if good:
            res["good_acc"] = float((top[good] == ans[good]).float().mean())
            res["good_logp"] = float(lp[good, ans[good]].mean())
        if bad:
            res["loop_prob"] = float(lp[bad, ans[bad]].exp().mean())
        res["score_bp"] = int(10000 * (res.get("good_acc", 0) * len(good) + (1 - res.get("loop_prob", 0)) * len(bad))
                              / max(1, len(good) + len(bad)))
        return res, lp

    def golden(self, texts, base=None):
        """Mean next-token loss on the golden texts, and the shift from a saved base."""
        torch = self.torch
        ids, mask, lens = self._pad(texts)
        with torch.no_grad():
            h = self.m.hidden(ids, mask)
            nll, n, kl, agree, saved = 0.0, 0, 0.0, 0, []
            for i in range(len(texts)):
                lg = self.m.logits_at(h[i, : lens[i] - 1])
                lp = torch.log_softmax(lg, dim=-1)
                tgt = ids[i, 1: lens[i]].to(lp.device)
                nll += float(-lp.gather(1, tgt[:, None]).sum())
                n += lens[i] - 1
                top = lp.argmax(dim=-1)
                if base is not None:
                    blp = base[i].to(lp.device).float()
                    kl += float((blp.exp() * (blp - lp)).sum())
                    agree += int((blp.argmax(dim=-1) == top).sum())
                saved.append(lp.half().cpu())
        res = {"nll": nll / n, "tokens": n}
        if base is not None:
            res["kl_per_token"] = kl / n
            res["top1_agree"] = agree / n
        return res, saved


def background_keys(model, ev, examples, texts, layers, max_rows=1536, seed=11):
    """Inputs to each layer's down projection over ordinary text: every
    position of the golden texts and every position but the last of the prompts."""
    torch = ev.torch
    rows = {L: [] for L in layers}
    batches = [(texts, False)] + [([e["prompt"] for e in examples[b: b + ev.batch]], True)
                                  for b in range(0, len(examples), ev.batch)]
    for chunk, skip_last in batches:
        ids, mask, lens = ev._pad(chunk)
        cap, hooks = {}, []
        for L in layers:
            def hook(mod, args, o, L=L):
                cap[L] = args[0].detach()
            hooks.append(model.mlp_down(L).register_forward_hook(hook))
        try:
            with torch.no_grad():
                model.hidden(ids, mask)
        finally:
            for hk in hooks:
                hk.remove()
        for L in layers:
            for i in range(len(chunk)):
                n = lens[i] - 1 if skip_last else lens[i]
                rows[L].append(cap[L][i, 1:n].float().cpu())  # skip the start token
    g = torch.Generator().manual_seed(seed)
    out = {}
    for L in layers:
        B = torch.cat(rows[L])
        if B.shape[0] > max_rows:
            B = B[torch.randperm(B.shape[0], generator=g)[:max_rows]]
        out[L] = B
    return out


def with_shared_key(B, last_keys, weight=64):
    """Add the mean last-position key, heavily weighted, to the background.

    Every prompt ends the same way, so the last-position keys share one big
    component. Whitening it out makes the edit follow what differs between
    the prompts (what is open, the key, a revisit) instead of firing the same
    way on every maze prompt."""
    import torch
    mean = torch.stack(last_keys).mean(dim=0, keepdim=True)
    return torch.cat([B, mean.expand(weight, -1)])


def whiten(K, B, ridge=0.1):
    """Rows of K mapped through (lam I + B^T B / m)^-1, by the Woodbury identity."""
    import torch
    m, d = B.shape
    lam = ridge * float((B * B).sum()) / (m * d)
    G = B @ B.t() + m * lam * torch.eye(m, dtype=B.dtype)
    S = torch.linalg.solve(G, B @ K.t())
    return (K - (B.t() @ S).t()) / lam


def gradients(model, ev, examples, layers):
    """k, o and g at the last prompt position, per layer and example."""
    torch = ev.torch
    out = {L: {"k": [], "o": [], "g": []} for L in layers}
    for b in range(0, len(examples), ev.batch):
        chunk = examples[b: b + ev.batch]
        ids, mask, lens = ev._pad([e["prompt"] for e in chunk])
        cap, hooks = {}, []
        for L in layers:
            def hook(mod, args, o, L=L):
                d = torch.zeros_like(o, dtype=torch.float32, requires_grad=True)
                cap[L] = (args[0].detach(), o.detach(), d)
                return o + d.to(o.dtype)
            hooks.append(model.mlp_down(L).register_forward_hook(hook))
        try:
            with torch.enable_grad():
                h = model.hidden(ids, mask)
                last = torch.stack([h[i, lens[i] - 1] for i in range(len(chunk))])
                lp = torch.log_softmax(model.logits_at(last)[:, ev.answer_ids], dim=-1)
                ans = torch.tensor([e["answer"] for e in chunk], device=lp.device)
                rew = torch.tensor([e["reward"] for e in chunk], device=lp.device)
                obj = (rew * lp.gather(1, ans[:, None]).squeeze(1)).sum()
                obj.backward()
        finally:
            for hk in hooks:
                hk.remove()
        for L in layers:
            k, o, d = cap[L]
            for i in range(len(chunk)):
                p = lens[i] - 1
                out[L]["k"].append(k[i, p].float().cpu())
                out[L]["o"].append(o[i, p].float().cpu())
                out[L]["g"].append(d.grad[i, p].float().cpu())
    return out


def low_rank(grads, rank, background=None, ridge=0.1):
    """U (out x r), V (r x in) for sum_i ||o_i|| g^_i v_i^T, truncated by SVD.

    v_i is k_i / ||k_i||^2 without background keys, and the whitened key
    C^-1 k_i / (k_i . C^-1 k_i) with them, so that v_i . k_i = 1 either way."""
    import torch
    G = torch.stack([g / (g.norm() + 1e-12) * o.norm() for g, o in zip(grads["g"], grads["o"])], dim=1)
    K0 = torch.stack(grads["k"], dim=0)
    if background is None:
        K = K0 / (K0.norm(dim=1, keepdim=True) ** 2 + 1e-12)
    else:
        Kw = whiten(K0, background, ridge)
        K = Kw / (Kw * K0).sum(dim=1, keepdim=True)
    # dW = G @ K; keep the top singular directions without forming dW
    Qg, Rg = torch.linalg.qr(G)
    Qk, Rk = torch.linalg.qr(K.t())
    u, s, vh = torch.linalg.svd(Rg @ Rk.t())
    r = min(rank, s.numel())
    U = Qg @ u[:, :r] * s[:r]
    V = vh[:r] @ Qk.t()
    return U.contiguous(), V.contiguous()


def split_runs(runs, holdout_every=4):
    train, hold = [], []
    for i, r in enumerate(sorted(runs, key=lambda r: r["id"])):
        (hold if i % holdout_every == 0 else train).append(r)
    return train, hold


def dream(model, ev, runs, layers=(40, 41), rank=16, strengths=(0.02, 0.05, 0.1, 0.2), max_train=64,
          max_kl=0.02, max_nll_rise=0.02, min_agree=0.97, seed=7, log=print):
    t0 = time.time()
    train_runs, hold_runs = split_runs(runs)
    rnd = random.Random(seed)
    train = dedupe(examples_from(train_runs))
    rnd.shuffle(train)
    train = train[:max_train]
    hold = dedupe(examples_from(hold_runs))
    log("examples: %d to learn from, %d held out" % (len(train), len(hold)))
    base_train, _ = ev.task(train)
    base_hold, _ = ev.task(hold)
    base_gold, base_saved = ev.golden(GOLDEN_TEXTS)
    log("before: learn=%s held-out=%s golden_nll=%.4f (%.0fs)" % (base_train, base_hold, base_gold["nll"], time.time() - t0))
    grads = gradients(model, ev, train, layers)
    bg = background_keys(model, ev, train, GOLDEN_TEXTS, layers)
    factors = {L: low_rank(grads[L], rank, with_shared_key(bg[L], grads[L]["k"])) for L in layers}
    log("gradients and low-rank factors ready (%.0fs)" % (time.time() - t0))
    trials, best = [], None
    for c in strengths:
        model.clear_overlays()
        for L in layers:
            U, V = factors[L]
            model.add_overlay(L, U, V, c)
        tr, _ = ev.task(train)
        ho, _ = ev.task(hold)
        go, _ = ev.golden(GOLDEN_TEXTS, base_saved)
        gates = {
            "golden_kl": go["kl_per_token"] <= max_kl,
            "golden_nll": go["nll"] <= base_gold["nll"] * (1 + max_nll_rise),
            "golden_agree": go["top1_agree"] >= min_agree,
            "task_learn": tr["score_bp"] > base_train["score_bp"],
            "task_held_out": ho["score_bp"] >= base_hold["score_bp"],
            # neither half of the task may get worse to pay for the other:
            # good steps keep their accuracy and likelihood, loops do not rise
            "good_steps": (tr.get("good_acc", 0) >= base_train.get("good_acc", 0)
                           and tr.get("good_logp", 0) >= base_train.get("good_logp", 0) - 0.05
                           and ho.get("good_acc", 0) >= base_hold.get("good_acc", 0)),
            "loops": tr.get("loop_prob", 0) <= base_train.get("loop_prob", 0) + 1e-6,
        }
        t = {"strength": c, "learn": tr, "held_out": ho, "golden": go, "gates": gates, "pass": all(gates.values())}
        trials.append(t)
        log("strength %g: learn=%d (good %.3f, logp %.2f, loop %.3f) held-out=%d (good %.3f) kl=%.4f agree=%.3f "
            "nll=%.4f gates=%s (%.0fs)" % (
            c, tr["score_bp"], tr.get("good_acc", 0), tr.get("good_logp", 0), tr.get("loop_prob", 0), ho["score_bp"],
            ho.get("good_acc", 0), go["kl_per_token"], go["top1_agree"], go["nll"],
            ",".join(k for k, v in gates.items() if not v) or "all pass", time.time() - t0))
        if t["pass"] and (best is None or tr["score_bp"] > best["learn"]["score_bp"]):
            best = t
    model.clear_overlays()
    return {"layers": list(layers), "rank": rank, "train_n": len(train), "hold_n": len(hold),
            "before": {"learn": base_train, "held_out": base_hold, "golden": base_gold},
            "trials": trials, "chosen": best, "factors": factors, "seconds": time.time() - t0}
