/* Mjolnir: a nap for the toy policy (see mj_toytrain.h).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_toytrain.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_hash.h"
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_session.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mj_tt_default_cfg(mj_tt_cfg *c) {
    c->steps = 300;
    c->lr = 0.5f;
    c->step_cap = 0.5f;
    c->replay_old_per_new = 1;
    c->seed = 1;
    c->max_shift_bp = 2000;
}

void mj_tt_set_free(mj_tt_set *s) {
    free(s->s);
    memset(s, 0, sizeof *s);
}

static mj_status push(mj_tt_set *s, const mj_tt_sample *x) {
    if (s->n == s->cap) {
        uint32_t cap = s->cap ? s->cap * 2u : 256u;
        mj_tt_sample *ns;
        if (s->n >= MJ_TT_MAX_SAMPLES) return MJ_E_LIMIT;
        ns = (mj_tt_sample *)realloc(s->s, (size_t)cap * sizeof *ns);
        if (ns == NULL) return MJ_E_LIMIT;
        s->s = ns;
        s->cap = cap;
    }
    s->s[s->n++] = *x;
    return MJ_OK;
}

/* ---- features ---- */

/* The low four bits of Python's int(v) for a step's "open" value. */
static int open_bits(const mj_json *d, uint32_t v, unsigned *bits) {
    const mj_jnode *x;
    if (!mj_json_truthy(d, v)) { *bits = 0; return 1; } /* int(open or 0) */
    x = &d->v[v];
    switch (x->kind) {
    case MJ_J_TRUE: *bits = 1; return 1;
    case MJ_J_INT: *bits = (unsigned)((uint64_t)x->i & 15u); return 1;
    case MJ_J_FLOAT: {
        double t;
        if (!isfinite(x->f)) return 0;
        t = trunc(x->f);
        /* past 2^63 a double is a multiple of 2^11, so its low bits are 0 */
        *bits = fabs(t) < 9223372036854775808.0 ? (unsigned)((uint64_t)(int64_t)t & 15u) : 0u;
        return 1;
    }
    case MJ_J_BIGINT: {
        const char *s = d->t + x->s_off;
        uint32_t i = 0, m = 0;
        int neg = s[0] == '-';
        for (i = neg ? 1u : 0u; i < x->s_len; ++i) m = (m * 10u + (uint32_t)(s[i] - '0')) & 15u;
        *bits = neg ? ((16u - m) & 15u) : m;
        return 1;
    }
    case MJ_J_STR: { /* int("5"): optional spaces, sign, digits with single underscores */
        const char *s = d->t + x->s_off;
        size_t a = 0, e = x->s_len;
        int neg = 0, any = 0, us = 0;
        uint32_t m = 0;
        while (a < e && (s[a] == ' ' || (s[a] >= '\t' && s[a] <= '\r'))) ++a;
        while (e > a && (s[e - 1] == ' ' || (s[e - 1] >= '\t' && s[e - 1] <= '\r'))) --e;
        if (a < e && (s[a] == '+' || s[a] == '-')) neg = s[a++] == '-';
        for (; a < e; ++a) {
            if (s[a] == '_') {
                if (!any || us) return 0;
                us = 1;
                continue;
            }
            if (s[a] < '0' || s[a] > '9') return 0;
            m = (m * 10u + (uint32_t)(s[a] - '0')) & 15u;
            any = 1;
            us = 0;
        }
        if (!any || us) return 0;
        *bits = neg ? ((16u - m) & 15u) : m;
        return 1;
    }
    default: return 0;
    }
}

static int action_of(const mj_json *d, uint32_t v, uint8_t *a) {
    double f;
    if (!mj_json_is_number(d, v) || d->v[v].kind == MJ_J_BIGINT) return 0;
    f = mj_json_to_double(d, v);
    if (f == 0.0 || f == 1.0 || f == 2.0 || f == 3.0) {
        *a = (uint8_t)f;
        return 1;
    }
    return 0;
}

typedef struct hset {
    uint32_t *slot;
    uint32_t cap;
} hset;
#define HS_EMPTY 0xFFFFFFFEu
#define HS_NONE 0xFFFFFFFDu

static int hs_has(const hset *h, const mj_json *d, uint32_t v) {
    uint32_t i = (uint32_t)mj_json_hash(d, v) & (h->cap - 1u);
    while (h->slot[i] != HS_EMPTY) {
        if (mj_json_eq(d, h->slot[i] == HS_NONE ? MJ_JSON_NONE : h->slot[i], d, v)) return 1;
        i = (i + 1u) & (h->cap - 1u);
    }
    return 0;
}

static void hs_add(hset *h, const mj_json *d, uint32_t v) {
    uint32_t i;
    if (hs_has(h, d, v)) return;
    i = (uint32_t)mj_json_hash(d, v) & (h->cap - 1u);
    while (h->slot[i] != HS_EMPTY) i = (i + 1u) & (h->cap - 1u);
    h->slot[i] = v == MJ_JSON_NONE ? HS_NONE : v;
}

mj_status mj_tt_features(const mj_replay_run *run, mj_tt_set *out, char msg[96]) {
    hset vis;
    uint32_t i, cap = 16;
    uint8_t prev_move = 0;
    mj_status st = MJ_OK;
    while (cap < 2u * (run->n_steps + 1u)) cap *= 2u;
    vis.cap = cap;
    vis.slot = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));
    if (vis.slot == NULL) return MJ_E_LIMIT;
    for (i = 0; i < cap; ++i) vis.slot[i] = HS_EMPTY;
    for (i = 0; i < run->n_steps && st == MJ_OK; ++i) {
        const mj_replay_step *s = &run->steps[i];
        mj_tt_sample x;
        unsigned bits, b;
        int here;
        uint8_t a;
        memset(&x, 0, sizeof x);
        here = hs_has(&vis, &run->doc, s->state); /* states were checked hashable by the replay */
        hs_add(&vis, &run->doc, s->state);
        if (!open_bits(&run->doc, s->open, &bits)) {
            if (msg) snprintf(msg, 96, "%.48s step %u: open is not a number", run->id, (unsigned)i);
            st = MJ_E_INVAL;
            break;
        }
        for (b = 0; b < 4; ++b) x.x[b] = (int8_t)(((bits >> b) & 1u) ? 127 : 0);
        x.x[4] = (int8_t)(mj_json_truthy(&run->doc, s->key) ? 127 : 0);
        x.x[5] = (int8_t)(here ? 127 : 0);
        x.x[6] = (int8_t)(prev_move * 32);
        x.x[7] = 127;
        if (action_of(&run->doc, s->action, &a)) {
            x.action = a;
            x.step = s->index;
            st = push(out, &x);
            prev_move = a;
        }
    }
    free(vis.slot);
    return st;
}

static int id_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

mj_status mj_tt_build_batch(const mj_replay_run *const *runs, uint32_t n_runs, const mj_grade_map *grades,
                            mj_tt_set *samples, char ***sources, uint32_t *n_sources, char msg[96]) {
    uint32_t r, k, ns = 0;
    char **src = NULL;
    mj_status st = MJ_OK;
    *sources = NULL;
    *n_sources = 0;
    src = (char **)malloc(((size_t)n_runs + 1u) * sizeof *src);
    if (src == NULL) return MJ_E_LIMIT;
    for (r = 0; r < n_runs && st == MJ_OK; ++r) {
        uint32_t before = samples->n;
        st = mj_tt_features(runs[r], samples, msg);
        for (k = before; k < samples->n && st == MJ_OK; ++k) {
            mj_tt_sample *x = &samples->s[k];
            const mj_grade *g = grades ? mj_grades_find(grades, runs[r]->id, x->step) : NULL;
            if (g) {
                x->reward = (float)mj_grade_reward(g->label);
                x->by_grader = 1;
            } else {
                x->reward = (float)mj_grade_auto_reward(mj_auto_label_name(runs[r]->steps[x->step].autolab));
                x->by_grader = 0;
            }
        }
        if (st == MJ_OK && samples->n > before) {
            uint32_t j;
            int dup = 0;
            for (j = 0; j < ns; ++j)
                if (strcmp(src[j], runs[r]->id) == 0) dup = 1;
            if (!dup) src[ns++] = (char *)runs[r]->id;
        }
    }
    if (st != MJ_OK) { free(src); return st; }
    qsort(src, ns, sizeof *src, id_cmp);
    *sources = src;
    *n_sources = ns;
    return MJ_OK;
}

mj_status mj_tt_golden(const mj_replay_run *const *runs, uint32_t n_runs, mj_tt_set *gold, char msg[96]) {
    uint32_t r, k, w;
    for (r = 0; r < n_runs; ++r) {
        uint32_t before = gold->n;
        mj_status st = mj_tt_features(runs[r], gold, msg);
        if (st != MJ_OK) return st;
        for (k = before, w = before; k < gold->n; ++k)
            if (runs[r]->steps[gold->s[k].step].autolab != MJ_AUTO_NOT_YET) {
                gold->s[w] = gold->s[k];
                gold->s[w].reward = 1.0f;
                ++w;
            }
        gold->n = w;
    }
    return MJ_OK;
}

uint32_t mj_tt_task_bp(const mj_toy_model *w, const mj_tt_set *samples) {
    uint32_t i;
    uint64_t ok = 0;
    if (samples->n == 0) return 0;
    for (i = 0; i < samples->n; ++i) {
        int hit = mj_toy_act(w, samples->s[i].x) == samples->s[i].action;
        ok += samples->s[i].reward > 0 ? (uint64_t)hit : (uint64_t)!hit;
    }
    return (uint32_t)(ok * 10000u / samples->n);
}

uint32_t mj_tt_golden_bp(const mj_toy_model *w, const mj_tt_set *gold) {
    uint32_t i;
    uint64_t ok = 0;
    if (gold->n == 0) return 0;
    for (i = 0; i < gold->n; ++i) ok += mj_toy_act(w, gold->s[i].x) == gold->s[i].action;
    return (uint32_t)(ok * 10000u / gold->n);
}

uint32_t mj_tt_shift_bp(const mj_toy_model *w0, const mj_toy_model *w1, const mj_tt_set *gold) {
    uint32_t i;
    uint64_t d = 0;
    if (gold->n == 0) return 0;
    for (i = 0; i < gold->n; ++i) d += mj_toy_act(w0, gold->s[i].x) != mj_toy_act(w1, gold->s[i].x);
    return (uint32_t)(d * 10000u / gold->n);
}

/* ---- training ---- */

uint64_t mj_tt_next(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#define A MJ_TOY_A
#define F MJ_TOY_F

mj_status mj_tt_train(const mj_toy_model *w0, const mj_tt_set *samples, const mj_tt_set *gold, const mj_tt_cfg *cfg,
                      const uint32_t *idx, mj_toy_model *w1, float *loss_out) {
    uint64_t n64 = (uint64_t)samples->n + (uint64_t)gold->n * cfg->replay_old_per_new;
    uint32_t n, bs, i, a, f, step, k, r;
    float (*X)[F] = NULL;
    uint8_t *Y = NULL;
    float *R = NULL;
    float W0[A][F], W[A][F], buf[A][F], G[A][F];
    float loss = 0.0f;
    uint64_t rng = cfg->seed;
    if (n64 == 0 || n64 > MJ_TT_MAX_SAMPLES * 2ull || cfg->replay_old_per_new > 64u) return MJ_E_LIMIT;
    n = (uint32_t)n64;
    bs = n < 256u ? n : 256u;
    X = (float (*)[F])malloc((size_t)n * sizeof *X);
    Y = (uint8_t *)malloc(n);
    R = (float *)malloc((size_t)n * sizeof *R);
    if (X == NULL || Y == NULL || R == NULL) { free(X); free(Y); free(R); return MJ_E_LIMIT; }
    /* new samples, then the golden set replay_old_per_new times over */
    for (i = 0; i < samples->n; ++i) {
        for (f = 0; f < F; ++f) X[i][f] = (float)samples->s[i].x[f] / 127.0f;
        Y[i] = samples->s[i].action;
        R[i] = samples->s[i].reward;
    }
    for (r = 0, k = samples->n; r < cfg->replay_old_per_new; ++r)
        for (i = 0; i < gold->n; ++i, ++k) {
            for (f = 0; f < F; ++f) X[k][f] = (float)gold->s[i].x[f] / 127.0f;
            Y[k] = gold->s[i].action;
            R[k] = 1.0f;
        }
    for (a = 0; a < A; ++a)
        for (f = 0; f < F; ++f) {
            W0[a][f] = (float)w0->w[a][f] / 65536.0f;
            W[a][f] = W0[a][f];
            buf[a][f] = 0.0f;
        }
    for (step = 0; step < cfg->steps; ++step) {
        float lsum = 0.0f;
        memset(G, 0, sizeof G);
        for (k = 0; k < bs; ++k) {
            uint32_t j = idx ? idx[(size_t)step * bs + k] : (uint32_t)(mj_tt_next(&rng) % n);
            float z[A], m, se = 0.0f, lse, logp, p, q, term, g, dlogp;
            uint8_t y = Y[j];
            for (a = 0; a < A; ++a) {
                float s = 0.0f;
                for (f = 0; f < F; ++f) s += X[j][f] * W[a][f];
                z[a] = s * 4.0f;
            }
            m = z[0];
            for (a = 1; a < A; ++a) if (z[a] > m) m = z[a];
            for (a = 0; a < A; ++a) se += expf(z[a] - m);
            lse = logf(se);
            logp = z[y] - m - lse;
            p = expf(logp);
            q = p > 0.999999f ? 0.999999f : p;
            if (R[j] > 0) {
                term = R[j] * logp;
                dlogp = R[j];
            } else {
                term = -R[j] * log1pf(-q);
                dlogp = p <= 0.999999f ? R[j] * p / (1.0f - q) : 0.0f; /* no gradient through the clamp */
            }
            lsum += term;
            g = -dlogp / (float)bs;
            for (a = 0; a < A; ++a) {
                float sm = expf(z[a] - m - lse);
                float dz = (a == y ? g : 0.0f) - sm * g;
                for (f = 0; f < F; ++f) G[a][f] += dz * 4.0f * X[j][f];
            }
        }
        loss = -(lsum / (float)bs);
        for (a = 0; a < A; ++a)
            for (f = 0; f < F; ++f) {
                float lo = W0[a][f] - cfg->step_cap, hi = W0[a][f] + cfg->step_cap, v;
                buf[a][f] = step == 0 ? G[a][f] : buf[a][f] * 0.9f + G[a][f];
                v = W[a][f] - cfg->lr * buf[a][f];
                if (v > hi) v = hi;
                if (v < lo) v = lo;
                if (v > 63.0f) v = 63.0f;
                if (v < -63.0f) v = -63.0f;
                W[a][f] = v;
            }
    }
    for (a = 0; a < A; ++a)
        for (f = 0; f < F; ++f) w1->w[a][f] = (int32_t)rintf(W[a][f] * 65536.0f); /* half to even, as torch.round */
    if (loss_out) *loss_out = loss;
    free(X);
    free(Y);
    free(R);
    return MJ_OK;
}

/* ---- the nap ---- */

static void set_msg(char msg[160], const char *s) {
    if (msg) snprintf(msg, 160, "%s", s);
}

mj_status mj_tt_nap(mj_store *store, const char *ledger_path, mj_budget *b, const mj_replay_run *const *learn,
                    uint32_t n_learn, const mj_replay_run *const *hold, uint32_t n_hold, const mj_grade_map *grades,
                    const char *dream_id, int integrate, const mj_tt_cfg *cfg, mj_tt_result *res, char msg[160]) {
    uint64_t t0 = mj_clock_ms();
    mj_ledger_state lst;
    uint64_t bad = 0;
    char live[MJ_ID_MAX + 1], snap[MJ_ID_MAX + 8], cand[MJ_ID_MAX + 8], label[MJ_TEXT_MAX + 1], m96[96];
    uint8_t *blob = NULL, sv[MJ_TOY_BLOB_LEN];
    size_t blen = 0;
    mj_toy_model w0, w1;
    mj_tt_set samples, gold;
    char **sources = NULL;
    uint32_t n_sources = 0, i, graded = 0;
    float loss = 0.0f;
    int64_t moved = 0, cap_q;
    mj_dream_record rec;
    mj_status st;
    char when[21];

    memset(res, 0, sizeof *res);
    memset(&samples, 0, sizeof samples);
    memset(&gold, 0, sizeof gold);
    if (msg) msg[0] = '\0';
    m96[0] = '\0';
    if (dream_id) {
        if (!mj_id_valid(dream_id)) { set_msg(msg, "bad dream id"); return MJ_E_INVAL; }
        snprintf(res->dream, sizeof res->dream, "%s", dream_id);
    } else {
        char stamp[17];
        st = mj_utc_stamp(stamp);
        if (st != MJ_OK) return st;
        snprintf(res->dream, sizeof res->dream, "toy-nap-%s", stamp);
    }
    st = mj_ledger_verify_file(ledger_path, b, &lst, &bad); /* refuse to dream on a broken chain */
    if (st != MJ_OK) { set_msg(msg, "the dream ledger chain is broken"); return st; }
    st = mj_store_head(store, live);
    if (st == MJ_E_NOTFOUND) { set_msg(msg, "the toy store has no live version; seed it first (toy-seed)"); return MJ_E_NOTFOUND; }
    if (st != MJ_OK) return st;
    st = mj_store_get(store, live, &blob, &blen);
    if (st != MJ_OK) return st;
    st = mj_toy_load(blob, blen, &w0);
    mj_bfree(b, blob, blen ? blen : 1);
    if (st != MJ_OK) { set_msg(msg, "the live toy weights do not load"); return st; }
    st = mj_tt_build_batch(learn, n_learn, grades, &samples, &sources, &n_sources, m96);
    if (st == MJ_OK) st = mj_tt_golden(hold, n_hold, &gold, m96);
    if (st != MJ_OK) { set_msg(msg, m96[0] ? m96 : "could not build the dream batch"); goto out; }
    if (samples.n == 0 || gold.n == 0) { set_msg(msg, "no replay steps or no golden steps to dream on"); st = MJ_E_INVAL; goto out; }
    if (snprintf(snap, sizeof snap, "%s.snap", res->dream) >= (int)MJ_ID_MAX + 1 ||
        snprintf(cand, sizeof cand, "%s.cand", res->dream) >= (int)MJ_ID_MAX + 1) {
        set_msg(msg, "the dream id is too long for its snapshot name");
        st = MJ_E_INVAL;
        goto out;
    }
    st = mj_toy_save(&w0, sv);
    snprintf(label, sizeof label, "before %s", res->dream);
    if (st == MJ_OK) st = mj_store_put(store, snap, MJ_KIND_SNAPSHOT, live, label, sv, sizeof sv, NULL);
    if (st != MJ_OK) { set_msg(msg, st == MJ_E_EXISTS ? "the snapshot already exists" : "the snapshot could not be saved"); goto out; }
    st = mj_tt_train(&w0, &samples, &gold, cfg, NULL, &w1, &loss);
    if (st != MJ_OK) { set_msg(msg, "training failed"); goto out; }
    cap_q = (int64_t)((double)cfg->step_cap * 65536.0) + 1;
    {
        uint32_t a, f;
        for (a = 0; a < A; ++a)
            for (f = 0; f < F; ++f) {
                int64_t d = (int64_t)w1.w[a][f] - (int64_t)w0.w[a][f];
                if (d < 0) d = -d;
                if (d > moved) moved = d;
            }
    }
    res->task_before = mj_tt_task_bp(&w0, &samples);
    res->task_after = mj_tt_task_bp(&w1, &samples);
    res->golden_before = mj_tt_golden_bp(&w0, &gold);
    res->golden_after = mj_tt_golden_bp(&w1, &gold);
    res->shift_bp = mj_tt_shift_bp(&w0, &w1, &gold);
    res->gate_step = moved <= cap_q;
    res->gate_shift = res->shift_bp <= cfg->max_shift_bp;
    res->gate_golden = res->golden_after >= res->golden_before;
    res->gate_task = res->task_after > res->task_before;
    res->max_move_q16 = moved;
    st = mj_toy_save(&w1, sv);
    snprintf(label, sizeof label, "toy nap on %u replay steps", (unsigned)samples.n);
    if (st == MJ_OK) st = mj_store_put(store, cand, MJ_KIND_CANDIDATE, live, label, sv, sizeof sv, NULL);
    if (st != MJ_OK) { set_msg(msg, "the candidate could not be saved"); goto out; }
    {
        int passed = res->gate_step && res->gate_shift && res->gate_golden && res->gate_task;
        if (passed && integrate) {
            st = mj_store_set_head(store, cand);
            if (st != MJ_OK) { set_msg(msg, "HEAD could not be moved"); goto out; }
            snprintf(res->outcome, sizeof res->outcome, "integrated");
        } else {
            snprintf(res->outcome, sizeof res->outcome, "%s", passed ? "dry-run-pass" : "rejected");
        }
    }
    for (i = 0; i < samples.n; ++i) graded += samples.s[i].by_grader;
    mj_ledger_record_init(&rec);
    snprintf(rec.dream_id, sizeof rec.dream_id, "%s", res->dream);
    st = mj_utc_now(when);
    if (st != MJ_OK) goto out;
    snprintf(rec.when, sizeof rec.when, "%s", when);
    snprintf(rec.trigger, sizeof rec.trigger, "nap");
    snprintf(rec.grader, sizeof rec.grader, "%s", graded ? "JD" : "auto-maze");
    {
        size_t tot = 0;
        for (i = 0; i < n_sources; ++i) tot += strlen(sources[i]) + 1;
        if (n_sources <= 16) {
            char *joined = (char *)malloc(tot + 1);
            size_t o = 0;
            if (joined == NULL) { st = MJ_E_LIMIT; goto out; }
            for (i = 0; i < n_sources; ++i) {
                size_t l = strlen(sources[i]);
                if (i) joined[o++] = ',';
                memcpy(joined + o, sources[i], l);
                o += l;
            }
            joined[o] = '\0';
            if (o > MJ_TEXT_MAX) { free(joined); set_msg(msg, "ledger field inputs is not valid"); st = MJ_E_INVAL; goto out; }
            memcpy(rec.inputs, joined, o + 1);
            free(joined);
        } else {
            mj_sha256_ctx c;
            uint8_t h[32];
            char hex[65];
            static const char xd[] = "0123456789abcdef";
            int k;
            mj_sha256_init(&c);
            for (i = 0; i < n_sources; ++i) {
                if (i) mj_sha256_update(&c, ",", 1);
                mj_sha256_update(&c, sources[i], strlen(sources[i]));
            }
            mj_sha256_final(&c, h);
            for (k = 0; k < 32; ++k) {
                hex[2 * k] = xd[h[k] >> 4];
                hex[2 * k + 1] = xd[h[k] & 15];
            }
            hex[64] = '\0';
            snprintf(rec.inputs, sizeof rec.inputs, "set-%.60s", hex);
        }
    }
    snprintf(rec.weights_before, sizeof rec.weights_before, "%s", live);
    memcpy(rec.weights_candidate, cand, strlen(cand) + 1); /* length checked above */
    snprintf(rec.edit, sizeof rec.edit, "gpu=cpu steps=%u lr=%g cap=%g samples=%u jd=%u gold=%u loss=%.4f",
             (unsigned)cfg->steps, (double)cfg->lr, (double)cfg->step_cap, (unsigned)samples.n, (unsigned)graded,
             (unsigned)gold.n, (double)loss);
    rec.score_before_bp = res->task_before;
    rec.score_after_bp = res->task_after;
    rec.shift_bp = res->shift_bp;
    snprintf(rec.gates, sizeof rec.gates, "step=%s,shift=%s,golden=%s,task=%s", res->gate_step ? "pass" : "fail",
             res->gate_shift ? "pass" : "fail", res->gate_golden ? "pass" : "fail", res->gate_task ? "pass" : "fail");
    snprintf(rec.outcome, sizeof rec.outcome, "%s", res->outcome);
    {
        char notes[64] = "";
        size_t o = 0;
        const char *names[4] = {"step", "shift", "golden", "task"};
        int pass[4];
        int k;
        pass[0] = res->gate_step;
        pass[1] = res->gate_shift;
        pass[2] = res->gate_golden;
        pass[3] = res->gate_task;
        for (k = 0; k < 4; ++k)
            if (!pass[k]) o += (size_t)snprintf(notes + o, sizeof notes - o, "%s%s", o ? "," : "failed: ", names[k]);
        snprintf(rec.notes, sizeof rec.notes, "%s", o ? notes : "-");
    }
    st = mj_ledger_append(ledger_path, b, &rec);
    if (st != MJ_OK) { set_msg(msg, "the ledger line could not be written"); goto out; }
    res->ledger_seq = rec.seq;
    res->samples = samples.n;
    res->graded = graded;
    res->gold = gold.n;
    memcpy(res->candidate, cand, strlen(cand) + 1);
    if (mj_store_head(store, res->live) != MJ_OK) res->live[0] = '\0';
    res->seconds = (double)(mj_clock_ms() - t0) / 1000.0;
out:
    free(sources);
    mj_tt_set_free(&samples);
    mj_tt_set_free(&gold);
    return st;
}

mj_status mj_tt_split(const mj_replay_run *runs, uint32_t n, uint32_t holdout_every, const mj_replay_run ***learn,
                      uint32_t *n_learn, const mj_replay_run ***hold, uint32_t *n_hold) {
    const mj_replay_run **order, **l, **h;
    uint32_t i, j, nl = 0, nh = 0;
    *learn = *hold = NULL;
    *n_learn = *n_hold = 0;
    if (holdout_every == 0) return MJ_E_INVAL;
    order = (const mj_replay_run **)malloc(((size_t)n + 1u) * sizeof *order);
    l = (const mj_replay_run **)malloc(((size_t)n + 1u) * sizeof *l);
    h = (const mj_replay_run **)malloc(((size_t)n + 1u) * sizeof *h);
    if (order == NULL || l == NULL || h == NULL) { free(order); free(l); free(h); return MJ_E_LIMIT; }
    for (i = 0; i < n; ++i) { /* insertion sort: stable, as Python's sorted */
        const mj_replay_run *r = &runs[i];
        for (j = i; j > 0 && strcmp(order[j - 1]->id, r->id) > 0; --j) order[j] = order[j - 1];
        order[j] = r;
    }
    for (i = 0; i < n; ++i) {
        if (i % holdout_every == 0) h[nh++] = order[i];
        else l[nl++] = order[i];
    }
    free(order);
    *learn = l;
    *n_learn = nl;
    *hold = h;
    *n_hold = nh;
    return MJ_OK;
}
