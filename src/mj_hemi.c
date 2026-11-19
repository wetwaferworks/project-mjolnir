/* The left and right hemispheres: verdicts, sample sets, the combiner, the
 * toy hemisphere and one round on toy policies. See mj_hemi.h.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_hemi.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mj_hemi_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint8_t HEMI_MAGIC[8] = {'M', 'J', 'H', 'E', 'M', 'I', '1', 0};
#define HEMI_VERSION 1u

uint64_t mj_hemi_next(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* ---- picking and judging ------------------------------------------------- */

uint32_t mj_hemi_pick(const uint16_t probs[MJ_HEMI_MAX_ACTIONS], uint32_t n_actions, uint32_t legal) {
    uint32_t a, best = MJ_HEMI_MAX_ACTIONS;
    if (n_actions > MJ_HEMI_MAX_ACTIONS) return MJ_HEMI_MAX_ACTIONS;
    for (a = 0; a < n_actions; ++a) {
        if (!(legal & (1u << a))) continue;
        if (best == MJ_HEMI_MAX_ACTIONS || probs[a] > probs[best]) best = a;
    }
    return best;
}

/* (diff * 10000) / 65535 toward zero; diff is within +-65535. */
static int32_t to_bp(int32_t diff) {
    int64_t v = (int64_t)diff * 10000;
    return (int32_t)(v / (int64_t)MJ_HEMI_PROB_ONE);
}

void mj_hemi_judge(const uint16_t watcher_probs[MJ_HEMI_MAX_ACTIONS], uint32_t n_actions, uint32_t legal,
                   uint32_t actor_move, mj_hemi_verdict *out) {
    uint32_t a, top = MJ_HEMI_MAX_ACTIONS, second = MJ_HEMI_MAX_ACTIONS;
    int32_t p_top = 0, p_second = 0, p_actor, p_other = 0;
    int any_other = 0;
    memset(out, 0, sizeof *out);
    if (n_actions > MJ_HEMI_MAX_ACTIONS) n_actions = MJ_HEMI_MAX_ACTIONS;
    for (a = 0; a < n_actions; ++a) {
        int32_t p;
        if (!(legal & (1u << a))) continue;
        p = watcher_probs[a];
        if (top == MJ_HEMI_MAX_ACTIONS || p > p_top) {
            second = top; p_second = p_top;
            top = a; p_top = p;
        } else if (second == MJ_HEMI_MAX_ACTIONS || p > p_second) {
            second = a; p_second = p;
        }
    }
    if (second == MJ_HEMI_MAX_ACTIONS) p_second = 0;
    out->choice = (uint8_t)(top == MJ_HEMI_MAX_ACTIONS ? 0 : top);
    out->confidence_bp = (uint16_t)to_bp(p_top - p_second);
    if (actor_move >= n_actions || !(legal & (1u << actor_move))) {
        out->reason = MJ_HEMI_ILLEGAL;
        out->score_bp = -10000;
        return;
    }
    p_actor = watcher_probs[actor_move];
    for (a = 0; a < n_actions; ++a) {
        if (a == actor_move || !(legal & (1u << a))) continue;
        if (!any_other || (int32_t)watcher_probs[a] > p_other) p_other = watcher_probs[a];
        any_other = 1;
    }
    out->score_bp = (int16_t)to_bp(p_actor - p_other);
    if (top == actor_move) out->reason = MJ_HEMI_AGREE;
    else if (out->confidence_bp < MJ_HEMI_UNSURE_BP) out->reason = MJ_HEMI_UNSURE;
    else out->reason = MJ_HEMI_PREFER_OTHER;
}

/* ---- sample sets --------------------------------------------------------- */

void mj_hemi_set_init(mj_hemi_set *s, mj_budget *b, const mj_hemi_header *h) {
    memset(s, 0, sizeof *s);
    s->budget = b;
    if (h) s->h = *h;
}

void mj_hemi_set_free(mj_hemi_set *s) {
    if (s->r) mj_bfree(s->budget, s->r, (size_t)s->cap * sizeof *s->r);
    s->r = NULL;
    s->n = s->cap = 0;
}

mj_status mj_hemi_set_add(mj_hemi_set *s, const mj_hemi_record *r) {
    mj_status st = mj_hemi_record_check(&s->h, r);
    if (st != MJ_OK) return st;
    if (s->n > 0) {
        const mj_hemi_record *p = &s->r[s->n - 1];
        if (r->episode < p->episode || (r->episode == p->episode && r->step <= p->step)) return MJ_E_INVAL;
    }
    if (s->n == s->cap) {
        uint32_t ncap = s->cap ? s->cap * 2u : 64u;
        mj_hemi_record *nr;
        if (s->cap >= MJ_HEMI_MAX_RECORDS) return MJ_E_LIMIT;
        if (ncap > MJ_HEMI_MAX_RECORDS) ncap = MJ_HEMI_MAX_RECORDS;
        nr = (mj_hemi_record *)mj_balloc(s->budget, (size_t)ncap * sizeof *nr);
        if (nr == NULL) return MJ_E_LIMIT;
        if (s->n) memcpy(nr, s->r, (size_t)s->n * sizeof *nr);
        if (s->r) mj_bfree(s->budget, s->r, (size_t)s->cap * sizeof *s->r);
        s->r = nr;
        s->cap = ncap;
    }
    s->r[s->n++] = *r;
    return MJ_OK;
}

uint32_t mj_hemi_record_bytes(const mj_hemi_header *h) {
    return MJ_HEMI_RECORD_FIXED + (uint32_t)h->n_features + 4u * (uint32_t)h->n_actions;
}

mj_status mj_hemi_header_check(const mj_hemi_header *h) {
    if (h == NULL) return MJ_E_INVAL;
    if (h->pass != 1 && h->pass != 2) return MJ_E_INVAL;
    if (h->actor_side > MJ_HEMI_RIGHT) return MJ_E_INVAL;
    if (h->world < MJ_HEMI_WORLD_TOY_MAZE || h->world > MJ_HEMI_WORLD_CIRCUIT) return MJ_E_INVAL;
    if (h->n_features < 1 || h->n_features > MJ_HEMI_MAX_FEATURES) return MJ_E_INVAL;
    if (h->n_actions < 2 || h->n_actions > MJ_HEMI_MAX_ACTIONS) return MJ_E_INVAL;
    return MJ_OK;
}

static int probs_ok(const uint16_t *p, uint32_t na) {
    uint32_t a, sum = 0;
    for (a = 0; a < MJ_HEMI_MAX_ACTIONS; ++a) {
        if (a >= na && p[a] != 0) return 0;
        sum += p[a];
    }
    return sum + na >= MJ_HEMI_PROB_ONE && sum <= MJ_HEMI_PROB_ONE + na;
}

mj_status mj_hemi_record_check(const mj_hemi_header *h, const mj_hemi_record *r) {
    uint32_t f;
    if (mj_hemi_header_check(h) != MJ_OK || r == NULL) return MJ_E_INVAL;
    if (r->actor_move >= h->n_actions || r->verdict.choice >= h->n_actions) return MJ_E_INVAL;
    if (r->grade < MJ_CREDIT_SUCCESS || r->grade > MJ_CREDIT_UNGRADED) return MJ_E_INVAL;
    if (r->verdict.reason < MJ_HEMI_AGREE || r->verdict.reason > MJ_HEMI_ILLEGAL) return MJ_E_INVAL;
    if (r->verdict.score_bp < -10000 || r->verdict.score_bp > 10000) return MJ_E_INVAL;
    if (r->verdict.confidence_bp > 10000) return MJ_E_INVAL;
    for (f = h->n_features; f < MJ_HEMI_MAX_FEATURES; ++f)
        if (r->features[f] != 0) return MJ_E_INVAL;
    if (!probs_ok(r->actor_probs, h->n_actions) || !probs_ok(r->watcher_probs, h->n_actions)) return MJ_E_INVAL;
    return MJ_OK;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }

mj_status mj_hemi_encode(const mj_hemi_set *s, mj_budget *b, uint8_t **out, size_t *len) {
    uint32_t rb, i, a;
    size_t total;
    uint8_t *p, *q;
    if (s == NULL || out == NULL || len == NULL) return MJ_E_INVAL;
    *out = NULL;
    *len = 0;
    if (mj_hemi_header_check(&s->h) != MJ_OK || s->n > MJ_HEMI_MAX_RECORDS) return MJ_E_INVAL;
    rb = mj_hemi_record_bytes(&s->h);
    total = MJ_HEMI_HEADER_BYTES + (size_t)s->n * rb + MJ_HEMI_TRAILER_BYTES;
    for (i = 0; i < s->n; ++i) {
        if (mj_hemi_record_check(&s->h, &s->r[i]) != MJ_OK) return MJ_E_INVAL;
        if (i && (s->r[i].episode < s->r[i - 1].episode ||
                  (s->r[i].episode == s->r[i - 1].episode && s->r[i].step <= s->r[i - 1].step)))
            return MJ_E_INVAL;
    }
    p = (uint8_t *)mj_balloc(b, total);
    if (p == NULL) return MJ_E_LIMIT;
    memcpy(p, HEMI_MAGIC, 8);
    put16(p + 8, HEMI_VERSION);
    put16(p + 10, MJ_HEMI_HEADER_BYTES);
    put32(p + 12, s->h.round);
    p[16] = s->h.pass;
    p[17] = s->h.actor_side;
    p[18] = s->h.world;
    p[19] = s->h.n_features;
    p[20] = s->h.n_actions;
    put64(p + 24, s->h.seed);
    memcpy(p + 32, s->h.actor, MJ_HEMI_FINGERPRINT);
    memcpy(p + 48, s->h.observer, MJ_HEMI_FINGERPRINT);
    put32(p + 64, s->n);
    put32(p + 68, rb);
    q = p + MJ_HEMI_HEADER_BYTES;
    for (i = 0; i < s->n; ++i) {
        const mj_hemi_record *r = &s->r[i];
        put32(q, r->episode);
        put16(q + 4, r->step);
        q[6] = r->actor_move;
        q[7] = r->grade;
        q[8] = r->verdict.choice;
        q[9] = r->verdict.reason;
        put16(q + 10, (uint32_t)(uint16_t)r->verdict.score_bp);
        put16(q + 12, r->verdict.confidence_bp);
        q += MJ_HEMI_RECORD_FIXED;
        memcpy(q, r->features, s->h.n_features);
        q += s->h.n_features;
        for (a = 0; a < s->h.n_actions; ++a, q += 2) put16(q, r->actor_probs[a]);
        for (a = 0; a < s->h.n_actions; ++a, q += 2) put16(q, r->watcher_probs[a]);
    }
    mj_sha256(p, total - MJ_HEMI_TRAILER_BYTES, p + total - MJ_HEMI_TRAILER_BYTES);
    *out = p;
    *len = total;
    return MJ_OK;
}

mj_status mj_hemi_parse(const uint8_t *buf, size_t len, mj_budget *b, mj_hemi_set *out) {
    mj_hemi_header h;
    mj_hemi_set s;
    uint8_t sha[MJ_SHA256_LEN];
    uint32_t n, rb, i, a;
    const uint8_t *q;
    mj_status st;
    size_t k;
    if (buf == NULL || out == NULL) return MJ_E_INVAL;
    mj_hemi_set_init(out, b, NULL);
    if (len < MJ_HEMI_HEADER_BYTES + MJ_HEMI_TRAILER_BYTES) return MJ_E_INVAL;
    if (memcmp(buf, HEMI_MAGIC, 8) != 0) return MJ_E_INVAL;
    if (get16(buf + 8) != HEMI_VERSION || get16(buf + 10) != MJ_HEMI_HEADER_BYTES) return MJ_E_INVAL;
    mj_sha256(buf, len - MJ_HEMI_TRAILER_BYTES, sha);
    if (memcmp(sha, buf + len - MJ_HEMI_TRAILER_BYTES, MJ_SHA256_LEN) != 0) return MJ_E_CORRUPT;
    if (buf[21] || buf[22] || buf[23]) return MJ_E_INVAL;
    for (k = 72; k < MJ_HEMI_HEADER_BYTES; ++k)
        if (buf[k]) return MJ_E_INVAL;
    memset(&h, 0, sizeof h);
    h.round = get32(buf + 12);
    h.pass = buf[16];
    h.actor_side = buf[17];
    h.world = buf[18];
    h.n_features = buf[19];
    h.n_actions = buf[20];
    h.seed = get64(buf + 24);
    memcpy(h.actor, buf + 32, MJ_HEMI_FINGERPRINT);
    memcpy(h.observer, buf + 48, MJ_HEMI_FINGERPRINT);
    if (mj_hemi_header_check(&h) != MJ_OK) return MJ_E_INVAL;
    n = get32(buf + 64);
    rb = get32(buf + 68);
    if (rb != mj_hemi_record_bytes(&h) || n > MJ_HEMI_MAX_RECORDS) return MJ_E_INVAL;
    if ((uint64_t)len != (uint64_t)MJ_HEMI_HEADER_BYTES + (uint64_t)n * rb + MJ_HEMI_TRAILER_BYTES) return MJ_E_INVAL;
    mj_hemi_set_init(&s, b, &h);
    q = buf + MJ_HEMI_HEADER_BYTES;
    for (i = 0; i < n; ++i) {
        mj_hemi_record r;
        memset(&r, 0, sizeof r);
        r.episode = get32(q);
        r.step = (uint16_t)get16(q + 4);
        r.actor_move = q[6];
        r.grade = q[7];
        r.verdict.choice = q[8];
        r.verdict.reason = q[9];
        r.verdict.score_bp = (int16_t)(uint16_t)get16(q + 10);
        r.verdict.confidence_bp = (uint16_t)get16(q + 12);
        q += MJ_HEMI_RECORD_FIXED;
        memcpy(r.features, q, h.n_features);
        q += h.n_features;
        for (a = 0; a < h.n_actions; ++a, q += 2) r.actor_probs[a] = (uint16_t)get16(q);
        for (a = 0; a < h.n_actions; ++a, q += 2) r.watcher_probs[a] = (uint16_t)get16(q);
        st = mj_hemi_set_add(&s, &r);
        if (st != MJ_OK) {
            mj_hemi_set_free(&s);
            return st == MJ_E_LIMIT ? MJ_E_LIMIT : MJ_E_INVAL;
        }
    }
    *out = s;
    return MJ_OK;
}

mj_status mj_hemi_save(const mj_hemi_set *s, const char *path, uint8_t sha[MJ_SHA256_LEN]) {
    mj_budget b;
    uint8_t *buf;
    size_t n;
    mj_status st;
    mj_budget_init(&b, (size_t)1 << 30);
    st = mj_hemi_encode(s, &b, &buf, &n);
    if (st != MJ_OK) return st;
    if (sha) mj_sha256(buf, n, sha);
    st = mj_fs_write_atomic(path, buf, n, 0);
    mj_bfree(&b, buf, n);
    return st;
}

mj_status mj_hemi_load(const char *path, mj_budget *b, mj_hemi_set *out, uint8_t sha[MJ_SHA256_LEN]) {
    uint8_t *buf;
    size_t n;
    mj_status st;
    size_t max = MJ_HEMI_HEADER_BYTES + (size_t)MJ_HEMI_MAX_RECORDS * (MJ_HEMI_RECORD_FIXED + MJ_HEMI_MAX_FEATURES +
                                                                       4u * MJ_HEMI_MAX_ACTIONS) + MJ_HEMI_TRAILER_BYTES;
    st = mj_fs_read_all(path, max, b, &buf, &n);
    if (st != MJ_OK) return st;
    if (sha) mj_sha256(buf, n, sha);
    st = mj_hemi_parse(buf, n, b, out);
    mj_bfree(b, buf, n);
    return st;
}

void mj_hemi_tally_set(const mj_hemi_set *s, mj_hemi_tally *t) {
    uint32_t i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < s->n; ++i) {
        const mj_hemi_record *r = &s->r[i];
        int cleared = r->grade == MJ_CREDIT_SUCCESS, failed = r->grade == MJ_CREDIT_NOT_YET;
        ++t->steps;
        t->cleared += (uint32_t)cleared;
        t->not_yet += (uint32_t)failed;
        if (r->verdict.reason == MJ_HEMI_UNSURE) ++t->unsure;
        if (r->verdict.reason == MJ_HEMI_AGREE) {
            if (cleared) ++t->agree_right;
            if (failed) ++t->agree_wrong;
        } else if (r->verdict.reason == MJ_HEMI_PREFER_OTHER || r->verdict.reason == MJ_HEMI_ILLEGAL) {
            if (failed) ++t->object_right;
            if (cleared) ++t->object_wrong;
        }
    }
}

/* ---- the combiner -------------------------------------------------------- */

void mj_hemi_combine_default(mj_hemi_combine_cfg *c) {
    c->cleared_weight = 1.0f;
    c->not_yet_weight = 0.5f;
    c->watcher_weight = 0.75f;
}

mj_status mj_hemi_combine(const mj_hemi_set *const *sets, uint32_t n_sets, const mj_hemi_combine_cfg *cfg,
                          mj_budget *b, mj_hemi_target **out, uint32_t *n_out) {
    uint64_t total = 0;
    uint32_t i, j, k = 0;
    mj_hemi_target *t;
    if (sets == NULL || cfg == NULL || out == NULL || n_out == NULL || n_sets == 0) return MJ_E_INVAL;
    *out = NULL;
    *n_out = 0;
    for (i = 0; i < n_sets; ++i) {
        if (sets[i] == NULL || mj_hemi_header_check(&sets[i]->h) != MJ_OK) return MJ_E_INVAL;
        if (i && (sets[i]->h.n_features != sets[0]->h.n_features || sets[i]->h.n_actions != sets[0]->h.n_actions))
            return MJ_E_INVAL;
        total += 2u * (uint64_t)sets[i]->n;
    }
    if (total > 2u * (uint64_t)MJ_HEMI_MAX_RECORDS) return MJ_E_LIMIT;
    if (total == 0) return MJ_OK;
    t = (mj_hemi_target *)mj_balloc(b, (size_t)total * sizeof *t);
    if (t == NULL) return MJ_E_LIMIT;
    for (i = 0; i < n_sets; ++i) {
        /* how often this watcher's judgments matched the answer key in this
         * pass: an objection only counts as much as the watcher has earned */
        mj_hemi_tally tl;
        uint32_t judged, right;
        float trust = 0.0f;
        mj_hemi_tally_set(sets[i], &tl);
        judged = tl.agree_right + tl.agree_wrong + tl.object_right + tl.object_wrong;
        right = tl.agree_right + tl.object_right;
        if (judged) trust = 2.0f * (float)right / (float)judged - 1.0f;
        if (trust < 0.0f) trust = 0.0f;
        for (j = 0; j < sets[i]->n; ++j) {
            const mj_hemi_record *r = &sets[i]->r[j];
            if (r->grade == MJ_CREDIT_SUCCESS) {
                memcpy(t[k].features, r->features, MJ_HEMI_MAX_FEATURES);
                t[k].move = r->actor_move;
                t[k].from_watcher = 0;
                t[k].weight = cfg->cleared_weight;
                ++k;
            } else if (r->grade == MJ_CREDIT_NOT_YET) {
                memcpy(t[k].features, r->features, MJ_HEMI_MAX_FEATURES);
                t[k].move = r->actor_move;
                t[k].from_watcher = 0;
                t[k].weight = -cfg->not_yet_weight;
                ++k;
                /* the key failed the move and the watcher objected with
                 * confidence: the watcher's move is worth trying */
                if (cfg->watcher_weight > 0.0f && r->verdict.reason == MJ_HEMI_PREFER_OTHER &&
                    r->verdict.choice != r->actor_move) {
                    memcpy(t[k].features, r->features, MJ_HEMI_MAX_FEATURES);
                    t[k].move = r->verdict.choice;
                    t[k].from_watcher = 1;
                    t[k].weight = cfg->watcher_weight * trust * (float)r->verdict.confidence_bp / 10000.0f;
                    if (t[k].weight > 0.0f) ++k;
                }
            }
        }
    }
    *out = t;
    *n_out = k;
    return MJ_OK;
}

/* ---- the toy hemisphere -------------------------------------------------- */

void mj_hemi_toy_probs(const mj_toy_model *m, const int8_t x[MJ_TOY_F], uint16_t probs[MJ_HEMI_MAX_ACTIONS]) {
    double z[MJ_TOY_A], e[MJ_TOY_A], sum = 0.0, zmax;
    uint32_t a, i, top = mj_toy_act(m, x), used = 0;
    memset(probs, 0, MJ_HEMI_MAX_ACTIONS * sizeof probs[0]);
    for (a = 0; a < MJ_TOY_A; ++a) {
        int64_t acc = 0;
        for (i = 0; i < MJ_TOY_F; ++i) acc += (int64_t)m->w[a][i] * (int64_t)x[i];
        z[a] = (double)acc / (127.0 * 65536.0);
    }
    zmax = z[top];
    for (a = 0; a < MJ_TOY_A; ++a) {
        e[a] = exp(z[a] - zmax);
        sum += e[a];
    }
    for (a = 0; a < MJ_TOY_A; ++a) {
        if (a == top) continue;
        probs[a] = (uint16_t)floor(e[a] / sum * (double)MJ_HEMI_PROB_ONE + 0.5);
        used += probs[a];
    }
    probs[top] = (uint16_t)(MJ_HEMI_PROB_ONE - used);
    /* keep the policy's own move on top after rounding (ties go to the lower
     * index in mj_hemi_pick, the integer argmax may be a higher one) */
    for (a = 0; a < MJ_TOY_A; ++a) {
        if (a == top) continue;
        while (probs[a] >= probs[top] && probs[a] > 0) {
            --probs[a];
            ++probs[top];
        }
    }
}

static mj_status toy_distribution(void *self, const mj_hemi_state *s, uint16_t probs[MJ_HEMI_MAX_ACTIONS]) {
    const mj_hemi_toy *t = (const mj_hemi_toy *)self;
    if (s == NULL || s->features == NULL || s->n_features != MJ_TOY_F || s->n_actions != MJ_TOY_A) return MJ_E_INVAL;
    mj_hemi_toy_probs(&t->m, s->features, probs);
    return MJ_OK;
}

mj_status mj_hemi_toy_bind(mj_hemi_toy *t, const char *name, mj_hemi_model *out) {
    uint8_t blob[MJ_TOY_BLOB_LEN], sha[MJ_SHA256_LEN];
    mj_status st;
    if (t == NULL || out == NULL || !mj_id_valid(name)) return MJ_E_INVAL;
    st = mj_toy_save(&t->m, blob);
    if (st != MJ_OK) return st;
    mj_sha256(blob, sizeof blob, sha);
    memset(out, 0, sizeof *out);
    out->self = t;
    out->distribution = toy_distribution;
    memcpy(out->fingerprint, sha, MJ_HEMI_FINGERPRINT);
    memcpy(out->name, name, strlen(name) + 1);
    return MJ_OK;
}

void mj_hemi_toy_teacher(uint64_t seed, mj_toy_model *t) {
    uint64_t s = seed ^ 0x7465616368657231ull; /* "teacher1" */
    uint32_t a, f;
    for (a = 0; a < MJ_TOY_A; ++a)
        for (f = 0; f < MJ_TOY_F; ++f) {
            /* uniform in [-2.0, 2.0) in Q16 */
            int32_t v = (int32_t)(mj_hemi_next(&s) % (4u * 65536u)) - 2 * 65536;
            t->w[a][f] = v;
        }
}

mj_status mj_hemi_toy_world_make(uint64_t seed, uint32_t n_mazes, uint32_t segments, const mj_toy_model *teacher,
                                 mj_budget *b, mj_hemi_toy_world *w) {
    uint64_t s = seed;
    uint32_t i, k, f;
    if (w == NULL || teacher == NULL || n_mazes == 0 || n_mazes > 65536u || segments == 0 ||
        segments > MJ_TOY_MAX_SEGMENTS)
        return MJ_E_INVAL;
    memset(w, 0, sizeof *w);
    w->mazes = (mj_toy_maze *)mj_balloc(b, (size_t)n_mazes * sizeof *w->mazes);
    if (w->mazes == NULL) return MJ_E_LIMIT;
    w->n = n_mazes;
    w->budget = b;
    w->teacher = *teacher;
    for (i = 0; i < n_mazes; ++i) {
        mj_toy_maze *mz = &w->mazes[i];
        mz->n = segments;
        for (k = 0; k < segments; ++k) {
            for (f = 0; f + 1 < MJ_TOY_F; ++f) mz->x[k][f] = (int8_t)((int32_t)(mj_hemi_next(&s) % 255u) - 127);
            mz->x[k][MJ_TOY_F - 1] = 64; /* the always-on bias feature */
            mz->correct[k] = mj_toy_act(teacher, mz->x[k]);
            mz->type[k] = (uint8_t)(k + 1 == segments ? MJ_SEG_GOAL : (k % 2 ? MJ_SEG_OBSTACLE : MJ_SEG_PIT));
        }
    }
    return MJ_OK;
}

void mj_hemi_toy_world_free(mj_hemi_toy_world *w) {
    if (w->mazes) mj_bfree(w->budget, w->mazes, (size_t)w->n * sizeof *w->mazes);
    w->mazes = NULL;
    w->n = 0;
}

mj_status mj_hemi_toy_pass(const mj_hemi_toy_world *w, const mj_hemi_model *actor, const mj_hemi_model *observer,
                           mj_hemi_set *out) {
    uint32_t e, k;
    mj_status st;
    if (w == NULL || actor == NULL || observer == NULL || out == NULL) return MJ_E_INVAL;
    if (out->h.n_features != MJ_TOY_F || out->h.n_actions != MJ_TOY_A) return MJ_E_INVAL;
    memcpy(out->h.actor, actor->fingerprint, MJ_HEMI_FINGERPRINT);
    memcpy(out->h.observer, observer->fingerprint, MJ_HEMI_FINGERPRINT);
    for (e = 0; e < w->n; ++e) {
        const mj_toy_maze *mz = &w->mazes[e];
        for (k = 0; k < mz->n; ++k) {
            mj_hemi_record r;
            mj_hemi_state s;
            uint32_t move;
            memset(&r, 0, sizeof r);
            s.features = mz->x[k];
            s.n_features = MJ_TOY_F;
            s.n_actions = MJ_TOY_A;
            s.legal = (1u << MJ_TOY_A) - 1u;
            s.rich = NULL;
            st = actor->distribution(actor->self, &s, r.actor_probs);
            if (st != MJ_OK) return st;
            st = observer->distribution(observer->self, &s, r.watcher_probs);
            if (st != MJ_OK) return st;
            move = mj_hemi_pick(r.actor_probs, MJ_TOY_A, s.legal);
            if (move >= MJ_TOY_A) return MJ_E_INVAL;
            mj_hemi_judge(r.watcher_probs, MJ_TOY_A, s.legal, move, &r.verdict);
            r.episode = e;
            r.step = (uint16_t)k;
            r.actor_move = (uint8_t)move;
            r.grade = (uint8_t)(move == mz->correct[k] ? MJ_CREDIT_SUCCESS : MJ_CREDIT_NOT_YET);
            memcpy(r.features, mz->x[k], MJ_TOY_F);
            st = mj_hemi_set_add(out, &r);
            if (st != MJ_OK) return st;
            if (r.grade != MJ_CREDIT_SUCCESS) break; /* partial credit stops at the first failure */
        }
    }
    return MJ_OK;
}

/* score from the actions taken at every segment of every maze (row-major) */
static uint32_t score_from_actions(const mj_hemi_toy_world *w, const uint8_t *act) {
    mj_score_accum acc = {0, 0};
    uint32_t e, k;
    size_t at = 0;
    for (e = 0; e < w->n; ++e) {
        const mj_toy_maze *mz = &w->mazes[e];
        uint32_t ok = 0;
        for (k = 0; k < mz->n; ++k) {
            if (act[at + k] != mz->correct[k]) break;
            ++ok;
        }
        at += mz->n;
        mj_score_accum_add(&acc, mz->n, ok);
    }
    return mj_score_accum_bp(&acc);
}

uint32_t mj_hemi_toy_score(const mj_toy_model *m, const mj_hemi_toy_world *w) {
    mj_score_accum acc = {0, 0};
    uint32_t e, k;
    for (e = 0; e < w->n; ++e) {
        const mj_toy_maze *mz = &w->mazes[e];
        uint32_t ok = 0;
        for (k = 0; k < mz->n; ++k) {
            if (mj_toy_act(m, mz->x[k]) != mz->correct[k]) break;
            ++ok;
        }
        mj_score_accum_add(&acc, mz->n, ok);
    }
    return mj_score_accum_bp(&acc);
}

mj_status mj_hemi_toy_act_batch_cpu(const mj_toy_model *models, uint32_t k, const int8_t *states, uint32_t n,
                                    uint8_t *actions) {
    uint32_t m, i;
    if (models == NULL || states == NULL || actions == NULL || k == 0) return MJ_E_INVAL;
    for (m = 0; m < k; ++m)
        for (i = 0; i < n; ++i) actions[(size_t)m * n + i] = mj_toy_act(&models[m], states + (size_t)i * MJ_TOY_F);
    return MJ_OK;
}

/* ---- training ------------------------------------------------------------ */

void mj_hemi_train_default(mj_hemi_train_cfg *c) {
    c->steps = 400;
    c->batch = 64;
    c->lr = 0.5f;
    c->step_cap = 4.0f;
    c->seed = 1;
}

void mj_hemi_toy_merge(const mj_toy_model *a, const mj_toy_model *b, uint32_t alpha_q16, mj_toy_model *out) {
    uint32_t i, f;
    if (alpha_q16 > 65536u) alpha_q16 = 65536u;
    for (i = 0; i < MJ_TOY_A; ++i)
        for (f = 0; f < MJ_TOY_F; ++f) {
            int64_t d = ((int64_t)b->w[i][f] - (int64_t)a->w[i][f]) * (int64_t)alpha_q16;
            int64_t step = d >= 0 ? (d + 32768) >> 16 : -((-d + 32768) >> 16);
            out->w[i][f] = (int32_t)((int64_t)a->w[i][f] + step);
        }
}

mj_status mj_hemi_toy_train(const mj_toy_model *start, const mj_hemi_target *t, uint32_t n,
                            const mj_hemi_train_cfg *cfg, mj_toy_model *out, float *final_loss) {
    float w[MJ_TOY_A][MJ_TOY_F], w0[MJ_TOY_A][MJ_TOY_F], g[MJ_TOY_A][MJ_TOY_F];
    const float limit = (float)MJ_TOY_WEIGHT_LIMIT / 65536.0f;
    uint64_t rng;
    uint32_t step, j, a, f, batch;
    float loss = 0.0f;
    if (start == NULL || out == NULL || cfg == NULL || (n && t == NULL)) return MJ_E_INVAL;
    if (!(cfg->lr >= 0.0f) || !(cfg->step_cap >= 0.0f) || cfg->batch == 0) return MJ_E_INVAL;
    for (a = 0; a < MJ_TOY_A; ++a)
        for (f = 0; f < MJ_TOY_F; ++f) w0[a][f] = w[a][f] = (float)start->w[a][f] / 65536.0f;
    rng = cfg->seed;
    batch = n < cfg->batch ? n : cfg->batch;
    for (step = 0; n && step < cfg->steps; ++step) {
        memset(g, 0, sizeof g);
        loss = 0.0f;
        for (j = 0; j < batch; ++j) {
            const mj_hemi_target *x = &t[mj_hemi_next(&rng) % n];
            float z[MJ_TOY_A], p[MJ_TOY_A], zmax, sum = 0.0f, dz[MJ_TOY_A], pm;
            for (a = 0; a < MJ_TOY_A; ++a) {
                float acc = 0.0f;
                for (f = 0; f < MJ_TOY_F; ++f) acc += w[a][f] * ((float)x->features[f] / 127.0f);
                z[a] = acc;
            }
            zmax = z[0];
            for (a = 1; a < MJ_TOY_A; ++a) if (z[a] > zmax) zmax = z[a];
            for (a = 0; a < MJ_TOY_A; ++a) { p[a] = expf(z[a] - zmax); sum += p[a]; }
            for (a = 0; a < MJ_TOY_A; ++a) p[a] /= sum;
            pm = p[x->move];
            if (x->weight > 0.0f) {
                loss += -x->weight * logf(pm > 1e-12f ? pm : 1e-12f);
                for (a = 0; a < MJ_TOY_A; ++a) dz[a] = x->weight * (p[a] - (a == x->move ? 1.0f : 0.0f));
            } else {
                float wn = -x->weight, rest = 1.0f - pm;
                if (rest < 1e-6f) rest = 1e-6f;
                loss += -wn * logf(rest);
                for (a = 0; a < MJ_TOY_A; ++a) dz[a] = wn * pm * ((a == x->move ? 1.0f : 0.0f) - p[a]) / rest;
            }
            for (a = 0; a < MJ_TOY_A; ++a)
                for (f = 0; f < MJ_TOY_F; ++f) g[a][f] += dz[a] * ((float)x->features[f] / 127.0f);
        }
        for (a = 0; a < MJ_TOY_A; ++a)
            for (f = 0; f < MJ_TOY_F; ++f) {
                float v = w[a][f] - cfg->lr * g[a][f] / (float)batch;
                float lo = w0[a][f] - cfg->step_cap, hi = w0[a][f] + cfg->step_cap;
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                if (v < -limit) v = -limit;
                if (v > limit) v = limit;
                w[a][f] = v;
            }
        loss /= (float)batch;
    }
    for (a = 0; a < MJ_TOY_A; ++a)
        for (f = 0; f < MJ_TOY_F; ++f) {
            double q = floor((double)w[a][f] * 65536.0 + 0.5);
            if (q > (double)MJ_TOY_WEIGHT_LIMIT) q = (double)MJ_TOY_WEIGHT_LIMIT;
            if (q < -(double)MJ_TOY_WEIGHT_LIMIT) q = -(double)MJ_TOY_WEIGHT_LIMIT;
            out->w[a][f] = (int32_t)q;
        }
    if (final_loss) *final_loss = loss;
    return MJ_OK;
}

/* ---- one round ----------------------------------------------------------- */

mj_status mj_hemi_save_toy(const char *dir, const char *name, const mj_toy_model *m, uint8_t sha[MJ_SHA256_LEN]) {
    char p[MJ_PATH_MAX];
    uint8_t blob[MJ_TOY_BLOB_LEN];
    mj_status st = mj_toy_save(m, blob);
    if (st != MJ_OK) return st;
    st = mj_path_join(p, sizeof p, dir, name);
    if (st != MJ_OK) return st;
    mj_sha256(blob, sizeof blob, sha);
    return mj_fs_write_atomic(p, blob, sizeof blob, 1);
}

mj_status mj_hemi_restore_toy(const char *dir, const char *name, const uint8_t want[MJ_SHA256_LEN], mj_budget *b,
                             mj_toy_model *m) {
    char p[MJ_PATH_MAX];
    uint8_t *buf, sha[MJ_SHA256_LEN];
    size_t n;
    mj_toy_model t;
    mj_status st = mj_path_join(p, sizeof p, dir, name);
    if (st != MJ_OK) return st;
    st = mj_fs_read_all(p, MJ_TOY_BLOB_LEN, b, &buf, &n);
    if (st != MJ_OK) return st;
    mj_sha256(buf, n, sha);
    if (memcmp(sha, want, MJ_SHA256_LEN) != 0) {
        mj_bfree(b, buf, n);
        return MJ_E_CORRUPT;
    }
    st = mj_toy_load(buf, n, &t);
    mj_bfree(b, buf, n);
    if (st == MJ_OK) *m = t;
    return st;
}

mj_status mj_hemi_write_disagreements(const char *path, const mj_hemi_set *const *sets, uint32_t n_sets) {
    /* one line per step where the watcher and the key had something to say */
    size_t cap = 4096, len = 0;
    char *buf = NULL;
    uint32_t i, j;
    mj_status st;
    mj_budget b;
    mj_budget_init(&b, (size_t)1 << 30);
    for (i = 0; i < n_sets; ++i) cap += (size_t)sets[i]->n * 64u;
    buf = (char *)mj_balloc(&b, cap);
    if (buf == NULL) return MJ_E_LIMIT;
    len += (size_t)snprintf(buf + len, cap - len, "pass\tepisode\tstep\tactor_move\twatcher_move\treason\tgrade\tkind\n");
    for (i = 0; i < n_sets; ++i)
        for (j = 0; j < sets[i]->n; ++j) {
            const mj_hemi_record *r = &sets[i]->r[j];
            const char *kind = NULL;
            int failed = r->grade == MJ_CREDIT_NOT_YET, cleared = r->grade == MJ_CREDIT_SUCCESS;
            if (r->verdict.reason == MJ_HEMI_AGREE && failed) kind = "shared-mistake";
            else if (r->verdict.reason == MJ_HEMI_PREFER_OTHER && failed) kind = "watcher-right";
            else if (r->verdict.reason == MJ_HEMI_PREFER_OTHER && cleared) kind = "watcher-wrong";
            else if (r->verdict.reason == MJ_HEMI_UNSURE) kind = failed ? "unsure-failed" : "unsure-cleared";
            if (kind == NULL) continue;
            if (cap - len < 128) break;
            len += (size_t)snprintf(buf + len, cap - len, "%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\n", (unsigned)sets[i]->h.pass,
                                    (unsigned)r->episode, (unsigned)r->step, (unsigned)r->actor_move,
                                    (unsigned)r->verdict.choice, (unsigned)r->verdict.reason, (unsigned)r->grade, kind);
        }
    st = mj_fs_write_atomic(path, buf, len, 1);
    mj_bfree(&b, buf, cap);
    return st;
}

void mj_hemi_hex12(const uint8_t *sha, char out[13]) {
    char full[MJ_SHA256_HEX];
    mj_hex_encode(sha, MJ_SHA256_LEN, full);
    memcpy(out, full, 12);
    out[12] = '\0';
}

uint32_t mj_hemi_holdout_scores(const mj_hemi_toy_world *w, const mj_toy_model *models, uint32_t n_models, int use_gpu,
                               uint32_t *out, mj_budget *b) {
    /* gather every holdout state once, then act for all the policies */
    uint32_t e, k, total = 0, m;
    int8_t *states;
    uint8_t *act;
    size_t at = 0;
    for (e = 0; e < w->n; ++e) total += w->mazes[e].n;
    states = (int8_t *)mj_balloc(b, (size_t)total * MJ_TOY_F);
    act = (uint8_t *)mj_balloc(b, (size_t)total * n_models);
    if (states == NULL || act == NULL) {
        if (states) mj_bfree(b, states, (size_t)total * MJ_TOY_F);
        if (act) mj_bfree(b, act, (size_t)total * n_models);
        for (m = 0; m < n_models; ++m) out[m] = mj_hemi_toy_score(&models[m], w);
        return 0;
    }
    for (e = 0; e < w->n; ++e)
        for (k = 0; k < w->mazes[e].n; ++k, ++at) memcpy(states + at * MJ_TOY_F, w->mazes[e].x[k], MJ_TOY_F);
    {
        char err[128];
        int gpu_ok = use_gpu && mj_hemi_toy_act_batch_gpu(models, n_models, states, total, act, err) == MJ_OK;
        if (!gpu_ok) (void)mj_hemi_toy_act_batch_cpu(models, n_models, states, total, act);
        for (m = 0; m < n_models; ++m) out[m] = score_from_actions(w, act + (size_t)m * total);
        mj_bfree(b, states, (size_t)total * MJ_TOY_F);
        mj_bfree(b, act, (size_t)total * n_models);
        return gpu_ok ? 1u : 0u;
    }
}

mj_status mj_hemisphere_swap_toy(const char *dir, const char *ledger_path, const mj_toy_model *teacher,
                                 mj_toy_model *left, mj_toy_model *right, const mj_hemi_round_cfg *cfg, mj_budget *b,
                                 mj_hemi_round_report *rep) {
    char rdir[MJ_PATH_MAX], name[32], path[MJ_PATH_MAX];
    uint8_t left_sha[MJ_SHA256_LEN], right_sha[MJ_SHA256_LEN];
    mj_hemi_toy_world train, hold;
    mj_hemi_toy lt, rt;
    mj_hemi_model lm, rm;
    mj_hemi_set sets[2];
    const mj_hemi_set *sp[2];
    mj_hemi_header h;
    mj_hemi_target *targets = NULL;
    uint32_t n_targets = 0, i, scores[3];
    mj_toy_model start, child, three[3];
    mj_dream_record rec;
    mj_status st;
    uint64_t sum;
    if (dir == NULL || ledger_path == NULL || teacher == NULL || left == NULL || right == NULL || cfg == NULL ||
        b == NULL || rep == NULL)
        return MJ_E_INVAL;
    memset(rep, 0, sizeof *rep);
    rep->round = cfg->round;
    snprintf(name, sizeof name, "swap-%03u", (unsigned)cfg->round);
    if ((st = mj_path_join(rdir, sizeof rdir, dir, name)) != MJ_OK) return st;
    if ((st = mj_fs_mkdir(dir)) != MJ_OK || (st = mj_fs_mkdir(rdir)) != MJ_OK) return st;

    /* phase 0: checksummed snapshots of both parents, the rollback point */
    if ((st = mj_hemi_save_toy(rdir, "left.mjtoy", left, left_sha)) != MJ_OK) return st;
    if ((st = mj_hemi_save_toy(rdir, "right.mjtoy", right, right_sha)) != MJ_OK) return st;

    st = mj_hemi_toy_world_make(cfg->train_seed, cfg->train_mazes, cfg->segments, teacher, b, &train);
    if (st != MJ_OK) return st;
    st = mj_hemi_toy_world_make(cfg->holdout_seed, cfg->holdout_mazes, cfg->segments, teacher, b, &hold);
    if (st != MJ_OK) { mj_hemi_toy_world_free(&train); return st; }

    /* the parents work on copies; nothing below writes to *left or *right
     * until the gate has spoken */
    lt.m = *left;
    rt.m = *right;
    if ((st = mj_hemi_toy_bind(&lt, "left", &lm)) != MJ_OK || (st = mj_hemi_toy_bind(&rt, "right", &rm)) != MJ_OK)
        goto out_worlds;

    /* phase 1 and 2: each side plays while the other watches */
    for (i = 0; i < 2; ++i) {
        memset(&h, 0, sizeof h);
        h.round = cfg->round;
        h.pass = (uint8_t)(i + 1);
        h.actor_side = (uint8_t)(i == 0 ? MJ_HEMI_LEFT : MJ_HEMI_RIGHT);
        h.world = MJ_HEMI_WORLD_TOY_MAZE;
        h.n_features = MJ_TOY_F;
        h.n_actions = MJ_TOY_A;
        h.seed = cfg->train_seed;
        mj_hemi_set_init(&sets[i], b, &h);
    }
    st = mj_hemi_toy_pass(&train, &lm, &rm, &sets[0]);
    if (st == MJ_OK) st = mj_hemi_toy_pass(&train, &rm, &lm, &sets[1]);
    if (st != MJ_OK) goto out_sets;
    for (i = 0; i < 2; ++i) {
        snprintf(name, sizeof name, "pass%u.mjhemi", (unsigned)(i + 1));
        if ((st = mj_path_join(path, sizeof path, rdir, name)) != MJ_OK) goto out_sets;
        (void)mj_fs_remove(path);
        if ((st = mj_hemi_save(&sets[i], path, rep->pass_sha[i])) != MJ_OK) goto out_sets;
        mj_hemi_tally_set(&sets[i], &rep->tally[i]);
        sp[i] = &sets[i];
    }
    rep->left_pass_bp = mj_hemi_toy_score(left, &train);
    rep->right_pass_bp = mj_hemi_toy_score(right, &train);
    if ((st = mj_path_join(path, sizeof path, rdir, "disagreements.tsv")) != MJ_OK) goto out_sets;
    if ((st = mj_hemi_write_disagreements(path, sp, 2)) != MJ_OK) goto out_sets;

    /* phase 3: combine, weighted by the answer key */
    if ((st = mj_hemi_combine(sp, 2, &cfg->combine, b, &targets, &n_targets)) != MJ_OK) goto out_sets;
    rep->targets = n_targets;
    for (i = 0; i < n_targets; ++i) rep->watcher_targets += targets[i].from_watcher;
    sum = (uint64_t)rep->left_pass_bp + rep->right_pass_bp;
    rep->alpha_q16 = sum ? (uint32_t)(((uint64_t)rep->right_pass_bp * 65536u + sum / 2) / sum) : 32768u;
    if (cfg->no_merge) start = rep->right_pass_bp > rep->left_pass_bp ? *right : *left;
    else mj_hemi_toy_merge(left, right, rep->alpha_q16, &start);
    st = mj_hemi_toy_train(&start, targets, n_targets, &cfg->train, &child, &rep->loss);
    if (targets) mj_bfree(b, targets, (size_t)(2u * (sets[0].n + sets[1].n)) * sizeof *targets);
    if (st != MJ_OK) goto out_sets;
    if ((st = mj_hemi_save_toy(rdir, "child.mjtoy", &child, rep->child_sha)) != MJ_OK) goto out_sets;

    /* phase 4: the gate, on mazes no one has played */
    three[0] = *left;
    three[1] = *right;
    three[2] = child;
    (void)mj_hemi_holdout_scores(&hold, three, 3, cfg->use_gpu, scores, b);
    rep->left_holdout_bp = scores[0];
    rep->right_holdout_bp = scores[1];
    rep->child_holdout_bp = scores[2];
    rep->kept = scores[2] > scores[0] && scores[2] > scores[1];
    rep->weaker_side = (uint8_t)(scores[0] <= scores[1] ? MJ_HEMI_LEFT : MJ_HEMI_RIGHT);
    if (rep->kept) {
        snprintf(rep->why, sizeof rep->why, "child %u bp beat left %u and right %u; replaces the %s side's parent",
                 (unsigned)scores[2], (unsigned)scores[0], (unsigned)scores[1],
                 rep->weaker_side == MJ_HEMI_LEFT ? "left" : "right");
        /* the stronger parent moves across; the child takes its old side */
        if (rep->weaker_side == MJ_HEMI_LEFT) {
            *left = *right;
            *right = child;
        } else {
            *right = *left;
            *left = child;
        }
    } else {
        mj_toy_model rl, rr;
        snprintf(rep->why, sizeof rep->why, "child %u bp did not beat %s (left %u, right %u); parents restored",
                 (unsigned)scores[2],
                 (scores[2] <= scores[0] && scores[2] <= scores[1]) ? "either parent"
                 : scores[2] <= scores[0]                           ? "the left parent"
                                                                    : "the right parent",
                 (unsigned)scores[0], (unsigned)scores[1]);
        if ((st = mj_hemi_restore_toy(rdir, "left.mjtoy", left_sha, b, &rl)) != MJ_OK) goto out_sets;
        if ((st = mj_hemi_restore_toy(rdir, "right.mjtoy", right_sha, b, &rr)) != MJ_OK) goto out_sets;
        *left = rl;
        *right = rr;
    }

    /* phase 5: the journal */
    {
        char a[13], c[13], l[13], r[13], when[21];
        mj_ledger_record_init(&rec);
        snprintf(rec.dream_id, sizeof rec.dream_id, "hemi-swap-%03u", (unsigned)cfg->round);
        if (mj_utc_now(when) == MJ_OK) memcpy(rec.when, when, sizeof when);
        memcpy(rec.trigger, "hemispheres", 12);
        memcpy(rec.grader, "answer-key", 11);
        mj_hemi_hex12(rep->pass_sha[0], a);
        mj_hemi_hex12(rep->pass_sha[1], c);
        snprintf(rec.inputs, sizeof rec.inputs, "pass1-%s,pass2-%s", a, c);
        mj_hemi_hex12(left_sha, l);
        mj_hemi_hex12(right_sha, r);
        snprintf(rec.weights_before, sizeof rec.weights_before, "left-%s", l);
        mj_hemi_hex12(rep->child_sha, c);
        snprintf(rec.weights_candidate, sizeof rec.weights_candidate, "child-%s", c);
        snprintf(rec.edit, sizeof rec.edit,
                 "%s right-share=%u/65536 then %u answer-key targets (%u from watchers); right parent %s; loss %.4f",
                 cfg->no_merge ? "no-merge" : "merge", (unsigned)rep->alpha_q16, (unsigned)n_targets, (unsigned)rep->watcher_targets, r,
                 (double)rep->loss);
        rec.score_before_bp = scores[0] > scores[1] ? scores[0] : scores[1];
        rec.score_after_bp = scores[2];
        snprintf(rec.gates, sizeof rec.gates, "beats-left=%s,beats-right=%s", scores[2] > scores[0] ? "pass" : "fail",
                 scores[2] > scores[1] ? "pass" : "fail");
        memcpy(rec.outcome, rep->kept ? "dry-run-pass" : "rolled-back", rep->kept ? 13 : 12);
        snprintf(rec.notes, sizeof rec.notes, "%s", rep->why);
        st = mj_ledger_append(ledger_path, b, &rec);
    }

out_sets:
    mj_hemi_set_free(&sets[0]);
    mj_hemi_set_free(&sets[1]);
out_worlds:
    mj_hemi_toy_world_free(&train);
    mj_hemi_toy_world_free(&hold);
    return st;
}

/* ---- starting parents ---------------------------------------------------- */

static int32_t noise_q16(uint64_t *s, int32_t half_range) {
    return (int32_t)(mj_hemi_next(s) % (uint64_t)(2 * half_range)) - half_range;
}

mj_status mj_hemi_toy_parents(const char *scenario, uint64_t seed, const mj_toy_model *teacher, mj_toy_model *left,
                              mj_toy_model *right) {
    uint64_t s = seed ^ 0x706172656e747321ull; /* "parents!" */
    uint32_t a, f;
    if (scenario == NULL || teacher == NULL || left == NULL || right == NULL) return MJ_E_INVAL;
    if (strcmp(scenario, "complement") == 0) {
        *left = *teacher;
        *right = *teacher;
        for (a = 0; a < MJ_TOY_A; ++a)
            for (f = 0; f < MJ_TOY_F - 1; ++f) {
                if (f >= 4) left->w[a][f] = noise_q16(&s, 2 * 65536);
                else right->w[a][f] = noise_q16(&s, 2 * 65536);
            }
        return MJ_OK;
    }
    if (strcmp(scenario, "ternary") == 0) {
        for (a = 0; a < MJ_TOY_A; ++a)
            for (f = 0; f < MJ_TOY_F; ++f) right->w[a][f] = teacher->w[a][f] + noise_q16(&s, 65536 / 2);
        for (a = 0; a < MJ_TOY_A; ++a) {
            int64_t sum = 0, gamma;
            for (f = 0; f < MJ_TOY_F; ++f) sum += right->w[a][f] < 0 ? -(int64_t)right->w[a][f] : right->w[a][f];
            gamma = (sum + MJ_TOY_F / 2) / MJ_TOY_F;
            for (f = 0; f < MJ_TOY_F; ++f) {
                int64_t w = right->w[a][f], q;
                /* round(w / gamma) clipped to [-1, 1]: |w| > gamma / 2 keeps the sign */
                if (gamma == 0) q = 0;
                else if (2 * w > gamma) q = 1;
                else if (2 * w < -gamma) q = -1;
                else q = 0;
                left->w[a][f] = (int32_t)(q * gamma);
            }
        }
        return MJ_OK;
    }
    return MJ_E_INVAL;
}
