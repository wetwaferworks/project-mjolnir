/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_toy.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_hash.h"

#include <stdio.h>
#include <string.h>

static const uint8_t MAGIC[8] = {'M', 'J', 'T', 'O', 'Y', '1', 0, 0};

uint8_t mj_toy_act(const mj_toy_model *m, const int8_t x[MJ_TOY_F]) {
    int64_t best = 0;
    uint8_t a, arg = 0;
    uint32_t i;
    for (a = 0; a < MJ_TOY_A; ++a) {
        int64_t z = 0;
        for (i = 0; i < MJ_TOY_F; ++i) z += (int64_t)m->w[a][i] * (int64_t)x[i];
        if (a == 0 || z > best) {
            best = z;
            arg = a;
        }
    }
    return arg;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int weight_ok(int32_t v) { return v >= -MJ_TOY_WEIGHT_LIMIT && v <= MJ_TOY_WEIGHT_LIMIT; }

mj_status mj_toy_save(const mj_toy_model *m, uint8_t out[MJ_TOY_BLOB_LEN]) {
    uint32_t a, i, p = 12;
    if (m == NULL || out == NULL) return MJ_E_INVAL;
    memcpy(out, MAGIC, 8);
    out[8] = (uint8_t)MJ_TOY_F;
    out[9] = (uint8_t)MJ_TOY_A;
    out[10] = 0;
    out[11] = 0;
    for (a = 0; a < MJ_TOY_A; ++a)
        for (i = 0; i < MJ_TOY_F; ++i) {
            if (!weight_ok(m->w[a][i])) return MJ_E_INVAL;
            put32(out + p, (uint32_t)m->w[a][i]);
            p += 4;
        }
    put32(out + p, mj_crc32(out, p));
    return MJ_OK;
}

mj_status mj_toy_load(const uint8_t *buf, size_t n, mj_toy_model *m) {
    mj_toy_model t;
    uint32_t a, i, p = 12;
    if (buf == NULL || m == NULL || n != MJ_TOY_BLOB_LEN) return MJ_E_INVAL;
    if (memcmp(buf, MAGIC, 8) != 0 || buf[8] != MJ_TOY_F || buf[9] != MJ_TOY_A || buf[10] || buf[11]) return MJ_E_INVAL;
    if (get32(buf + MJ_TOY_BLOB_LEN - 4) != mj_crc32(buf, MJ_TOY_BLOB_LEN - 4)) return MJ_E_CORRUPT;
    for (a = 0; a < MJ_TOY_A; ++a)
        for (i = 0; i < MJ_TOY_F; ++i) {
            t.w[a][i] = (int32_t)get32(buf + p);
            if (!weight_ok(t.w[a][i])) return MJ_E_INVAL;
            p += 4;
        }
    *m = t;
    return MJ_OK;
}

mj_status mj_toy_run_validate(const mj_toy_run *r) {
    mj_maze_score s;
    uint32_t i;
    mj_status st;
    if (r == NULL || !mj_id_valid(r->id) || r->n == 0 || r->n > MJ_TOY_MAX_SEGMENTS) return MJ_E_INVAL;
    st = mj_score_maze(r->seg, r->n, NULL, &s);
    if (st != MJ_OK) return st;
    for (i = 0; i < r->n; ++i)
        if (r->seg[i].result != MJ_RUN_NOT_REACHED && r->taken[i] >= MJ_TOY_A) return MJ_E_INVAL;
    return MJ_OK;
}

static mj_status maze_validate(const mj_toy_maze *mz) {
    uint32_t i;
    if (mz->n == 0 || mz->n > MJ_TOY_MAX_SEGMENTS) return MJ_E_INVAL;
    for (i = 0; i < mz->n; ++i)
        if (mz->correct[i] >= MJ_TOY_A || mz->type[i] < MJ_SEG_PIT || mz->type[i] > MJ_SEG_GOAL) return MJ_E_INVAL;
    return MJ_OK;
}

mj_status mj_toy_play(const mj_toy_model *m, const mj_toy_maze *mz, const char *run_id, mj_toy_run *run,
                      mj_maze_score *score) {
    uint32_t i;
    int failed = 0;
    mj_status st;
    if (m == NULL || mz == NULL || run == NULL || !mj_id_valid(run_id)) return MJ_E_INVAL;
    st = maze_validate(mz);
    if (st != MJ_OK) return st;
    memset(run, 0, sizeof *run);
    memcpy(run->id, run_id, strlen(run_id) + 1);
    run->n = mz->n;
    for (i = 0; i < mz->n; ++i) {
        memcpy(run->x[i], mz->x[i], MJ_TOY_F);
        run->seg[i].type = mz->type[i];
        if (failed) {
            run->seg[i].result = MJ_RUN_NOT_REACHED;
            continue;
        }
        run->taken[i] = mj_toy_act(m, mz->x[i]);
        if (run->taken[i] == mz->correct[i]) {
            run->seg[i].result = MJ_RUN_CLEARED;
        } else {
            run->seg[i].result = MJ_RUN_FAILED;
            failed = 1;
        }
    }
    if (score != NULL) return mj_score_maze(run->seg, run->n, NULL, score);
    return MJ_OK;
}

mj_status mj_toy_eval(const mj_toy_model *m, const mj_toy_maze *mz, uint32_t n, uint32_t *score_bp) {
    mj_score_accum acc = {0, 0};
    mj_toy_run run;
    mj_maze_score s;
    uint32_t i;
    if (m == NULL || mz == NULL || n == 0 || score_bp == NULL) return MJ_E_INVAL;
    for (i = 0; i < n; ++i) {
        mj_status st = mj_toy_play(m, &mz[i], "eval", &run, &s);
        if (st != MJ_OK) return st;
        mj_score_accum_add(&acc, s.total, s.success);
    }
    *score_bp = mj_score_accum_bp(&acc);
    return MJ_OK;
}

void mj_update_default_cfg(mj_update_cfg *c) {
    c->lr = 4096;           /* 1/16 per full-reward, full-feature sample: small nudges */
    c->step_cap = 65536;    /* no weight moves more than 1.0 in one dream */
    c->max_shift_bp = 500;  /* at most 5% of golden actions may change */
    c->replay_old_per_new = 1;
    c->epochs = 8;
    c->max_steps = 1u << 20;
    c->max_ms = 10000;
    c->reward_success = 10000;
    c->reward_not_yet = -10000;
}

typedef struct sample {
    int8_t x[MJ_TOY_F];
    uint8_t a;
    int32_t r;
} sample;

static mj_status build_samples(const mj_toy_run *runs, uint32_t n, const mj_update_cfg *c, mj_budget *b,
                               sample **out, uint32_t *count, size_t *alloc) {
    uint32_t i, j, k = 0, total = 0;
    sample *s;
    mj_credit lab[MJ_TOY_MAX_SEGMENTS];
    *out = NULL;
    *count = 0;
    *alloc = 0;
    for (i = 0; i < n; ++i) {
        mj_status st = mj_toy_run_validate(&runs[i]);
        if (st != MJ_OK) return st;
        total += runs[i].n;
        if (total > MJ_TOY_MAX_SAMPLES) return MJ_E_LIMIT;
    }
    if (total == 0) return MJ_OK;
    s = (sample *)mj_balloc(b, (size_t)total * sizeof *s);
    if (s == NULL) return MJ_E_LIMIT;
    *alloc = (size_t)total * sizeof *s;
    for (i = 0; i < n; ++i) {
        mj_maze_score sc;
        (void)mj_score_maze(runs[i].seg, runs[i].n, lab, &sc);
        for (j = 0; j < runs[i].n; ++j) {
            if (lab[j] == MJ_CREDIT_UNGRADED) continue;
            memcpy(s[k].x, runs[i].x[j], MJ_TOY_F);
            s[k].a = runs[i].taken[j];
            s[k].r = lab[j] == MJ_CREDIT_SUCCESS ? c->reward_success : c->reward_not_yet;
            ++k;
        }
    }
    *out = s;
    *count = k;
    return MJ_OK;
}

static int32_t clamp32(int64_t v, int64_t lo, int64_t hi) {
    return (int32_t)(v < lo ? lo : v > hi ? hi : v);
}

static void nudge(mj_toy_model *cand, const mj_toy_model *live, const sample *s, const mj_update_cfg *c) {
    uint32_t i;
    for (i = 0; i < MJ_TOY_F; ++i) {
        int64_t d = ((int64_t)c->lr * (int64_t)s->r * (int64_t)s->x[i]) / (10000 * 127);
        int64_t lo = (int64_t)live->w[s->a][i] - c->step_cap, hi = (int64_t)live->w[s->a][i] + c->step_cap;
        if (lo < -MJ_TOY_WEIGHT_LIMIT) lo = -MJ_TOY_WEIGHT_LIMIT;
        if (hi > MJ_TOY_WEIGHT_LIMIT) hi = MJ_TOY_WEIGHT_LIMIT;
        cand->w[s->a][i] = clamp32((int64_t)cand->w[s->a][i] + d, lo, hi);
    }
}

static uint32_t golden_acc(const mj_toy_model *m, const mj_toy_golden *g, uint32_t n) {
    uint32_t i, ok = 0;
    for (i = 0; i < n; ++i) ok += (mj_toy_act(m, g[i].x) == g[i].expected);
    return (uint32_t)(((uint64_t)ok * MJ_BP_FULL) / n);
}

static uint32_t golden_shift(const mj_toy_model *a, const mj_toy_model *b, const mj_toy_golden *g, uint32_t n) {
    uint32_t i, ch = 0;
    for (i = 0; i < n; ++i) ch += (mj_toy_act(a, g[i].x) != mj_toy_act(b, g[i].x));
    return (uint32_t)(((uint64_t)ch * MJ_BP_FULL) / n);
}

static const char *pf(int ok) { return ok ? "pass" : "fail"; }

mj_status mj_update_dream(const mj_toy_model *live, const mj_toy_run *new_runs, uint32_t n_new,
                          const mj_toy_run *old_runs, uint32_t n_old, const mj_toy_golden *golden, uint32_t n_golden,
                          const mj_toy_maze *eval, uint32_t n_eval, const mj_update_cfg *c,
                          mj_toy_model *cand, mj_update_report *rep) {
    mj_budget b;
    sample *sn = NULL, *so = NULL;
    size_t an = 0, ao = 0;
    uint32_t nn = 0, no = 0, e, j, k, cursor = 0, a, i;
    uint64_t t0;
    mj_status st;
    mj_update_report r;
    if (live == NULL || cand == NULL || rep == NULL || c == NULL || golden == NULL || eval == NULL) return MJ_E_INVAL;
    *cand = *live;
    memset(&r, 0, sizeof r);
    *rep = r;
    if (n_new == 0 || new_runs == NULL || n_golden == 0 || n_golden > MJ_TOY_MAX_SAMPLES || n_eval == 0 ||
        n_eval > 1024 || (n_old > 0 && old_runs == NULL))
        return MJ_E_INVAL;
    if (c->lr <= 0 || c->lr > 65536 || c->step_cap <= 0 || c->step_cap > MJ_TOY_WEIGHT_LIMIT ||
        c->max_shift_bp > MJ_BP_FULL || c->epochs == 0 || c->epochs > 1024 || c->replay_old_per_new > 64 ||
        c->max_steps == 0 || c->reward_success <= 0 || c->reward_success > 10000 || c->reward_not_yet >= 0 ||
        c->reward_not_yet < -10000)
        return MJ_E_INVAL;
    for (i = 0; i < n_golden; ++i)
        if (golden[i].expected >= MJ_TOY_A) return MJ_E_INVAL;
    for (a = 0; a < MJ_TOY_A; ++a)
        for (i = 0; i < MJ_TOY_F; ++i)
            if (!weight_ok(live->w[a][i])) return MJ_E_INVAL;

    mj_budget_init(&b, 4u * 1024u * 1024u); /* hard memory ceiling for one toy dream */
    st = build_samples(new_runs, n_new, c, &b, &sn, &nn, &an);
    if (st == MJ_OK) st = build_samples(old_runs, n_old, c, &b, &so, &no, &ao);
    if (st != MJ_OK) goto done;
    r.samples_new = nn;
    r.samples_old = no;

    t0 = mj_clock_ms();
    for (e = 0; e < c->epochs && st == MJ_OK; ++e) {
        for (j = 0; j < nn && st == MJ_OK; ++j) {
            nudge(cand, live, &sn[j], c);
            ++r.steps;
            for (k = 0; k < c->replay_old_per_new && no > 0; ++k) {
                nudge(cand, live, &so[cursor], c);
                cursor = (cursor + 1u) % no;
                ++r.steps;
            }
            if (r.steps >= c->max_steps) st = MJ_E_TIMEOUT;
            else if (c->max_ms && (r.steps & 255u) == 0 && mj_clock_ms() - t0 > c->max_ms) st = MJ_E_TIMEOUT;
        }
    }
    if (st != MJ_OK) {
        *cand = *live; /* a dream that runs out of time leaves nothing behind */
        snprintf(r.why, sizeof r.why, "time ceiling: stopped after %lu steps, nothing kept", (unsigned long)r.steps);
        snprintf(r.gates, sizeof r.gates, "time=fail");
        goto done;
    }

    for (a = 0; a < MJ_TOY_A; ++a)
        for (i = 0; i < MJ_TOY_F; ++i) {
            int64_t d = (int64_t)cand->w[a][i] - (int64_t)live->w[a][i];
            int32_t ad = (int32_t)(d < 0 ? -d : d);
            if (ad > r.max_abs_delta) r.max_abs_delta = ad;
        }
    r.shift_bp = golden_shift(live, cand, golden, n_golden);
    r.golden_before_bp = golden_acc(live, golden, n_golden);
    r.golden_after_bp = golden_acc(cand, golden, n_golden);
    st = mj_toy_eval(live, eval, n_eval, &r.task_before_bp);
    if (st == MJ_OK) st = mj_toy_eval(cand, eval, n_eval, &r.task_after_bp);
    if (st != MJ_OK) {
        *cand = *live;
        goto done;
    }
    r.gate_step = r.max_abs_delta <= c->step_cap;
    r.gate_shift = r.shift_bp <= c->max_shift_bp;
    r.gate_golden = r.golden_after_bp >= r.golden_before_bp;
    r.gate_improve = r.task_after_bp > r.task_before_bp;
    snprintf(r.gates, sizeof r.gates, "step=%s,shift=%s(%lubp/%lubp),golden=%s(%lu->%lu),improve=%s(%lu->%lu)",
             pf(r.gate_step), pf(r.gate_shift), (unsigned long)r.shift_bp, (unsigned long)c->max_shift_bp,
             pf(r.gate_golden), (unsigned long)r.golden_before_bp, (unsigned long)r.golden_after_bp,
             pf(r.gate_improve), (unsigned long)r.task_before_bp, (unsigned long)r.task_after_bp);
    if (!r.gate_step)
        snprintf(r.why, sizeof r.why, "step gate: a weight moved %ld, cap %ld", (long)r.max_abs_delta, (long)c->step_cap);
    else if (!r.gate_shift)
        snprintf(r.why, sizeof r.why,
                 "shift gate: %lubp of golden actions changed, cap %lubp; next dream: smaller steps, more old replay",
                 (unsigned long)r.shift_bp, (unsigned long)c->max_shift_bp);
    else if (!r.gate_golden)
        snprintf(r.why, sizeof r.why, "golden gate: accuracy fell %lu->%lu (forgetting); next dream: more old replay",
                 (unsigned long)r.golden_before_bp, (unsigned long)r.golden_after_bp);
    else if (!r.gate_improve)
        snprintf(r.why, sizeof r.why, "improve gate: task score %lu->%lu did not rise; next dream: more material",
                 (unsigned long)r.task_before_bp, (unsigned long)r.task_after_bp);
    st = (r.gate_step && r.gate_shift && r.gate_golden && r.gate_improve) ? MJ_OK : MJ_E_GATE;
done:
    mj_bfree(&b, sn, an);
    mj_bfree(&b, so, ao);
    *rep = r;
    return st;
}
