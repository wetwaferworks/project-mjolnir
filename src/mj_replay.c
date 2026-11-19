/* Mjolnir: replays from recorded PCHIE runs, read-only (see mj_replay.h).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_replay.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_hash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define Q20 1048576.0


const char *mj_auto_label_name(mj_auto_label a) {
    switch (a) {
    case MJ_AUTO_PRODUCTIVE: return "productive";
    case MJ_AUTO_NOT_YET: return "not-yet";
    default: return "good";
    }
}

void mj_safe_id(const char *s, size_t limit, char *out, size_t cap) {
    const unsigned char *u = (const unsigned char *)s;
    size_t n = strlen(s), i = 0, k = 0, start = 0;
    char *tmp;
    if (cap == 0) return;
    tmp = (char *)malloc(n + 2);
    if (tmp == NULL) { out[0] = '\0'; return; }
    while (i < n) {
        uint32_t cp;
        size_t l = mj_utf8_len(u + i, n - i, &cp);
        unsigned char c = u[i];
        if (l == 1 && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-'))
            tmp[k++] = (char)c;
        else
            tmp[k++] = '_';
        i += l ? l : 1; /* a byte that is not UTF-8 counts as one character, as Python's surrogateescape */
    }
    tmp[k] = '\0';
    while (start < k && (tmp[start] == '.' || tmp[start] == '-')) ++start;
    if (start == k) {
        tmp[0] = 'x';
        tmp[1] = '\0';
        start = 0;
        k = 1;
    }
    k -= start;
    if (k > limit) k = limit;
    if (k >= cap) k = cap - 1;
    memcpy(out, tmp + start, k);
    out[k] = '\0';
    free(tmp);
}

static void errf(char err[MJ_REPLAY_ERR_MAX], const char *fmt, unsigned long line) {
    if (err) snprintf(err, MJ_REPLAY_ERR_MAX, fmt, line);
}

static int is_bytes_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0b || c == 0x0c; }

/* Next non-blank line (Python: data.split(b"\n"), keeping l.strip()). */
static int next_line(const uint8_t *data, size_t n, size_t *pos, size_t *ls, size_t *ll) {
    while (*pos <= n) {
        size_t s = *pos, e = s, k;
        int blank = 1;
        while (e < n && data[e] != '\n') ++e;
        *pos = e + 1;
        for (k = s; k < e; ++k)
            if (!is_bytes_space(data[k])) { blank = 0; break; }
        if (!blank) {
            *ls = s;
            *ll = e - s;
            return 1;
        }
        if (e >= n) break;
    }
    return 0;
}

static size_t rfind_sha(const char *s, size_t n) {
    static const char pat[] = ",\"sha256\"";
    size_t m = sizeof pat - 1, i;
    if (n < m) return (size_t)-1;
    for (i = n - m + 1; i-- > 0;)
        if (memcmp(s + i, pat, m) == 0) return i;
    return (size_t)-1;
}

static mj_status push_prop(mj_replay_run *r, uint32_t tok, double v) {
    if (r->n_props == r->cap_props) {
        uint32_t cap = r->cap_props ? r->cap_props * 2u : 64u;
        mj_replay_prop *np;
        if (cap > (1u << 24)) return MJ_E_LIMIT;
        np = (mj_replay_prop *)mj_balloc(r->b, (size_t)cap * sizeof *np);
        if (np == NULL) return MJ_E_LIMIT;
        if (r->n_props) memcpy(np, r->props, (size_t)r->n_props * sizeof *np);
        if (r->props) mj_bfree(r->b, r->props, (size_t)r->cap_props * sizeof *r->props);
        r->props = np;
        r->cap_props = cap;
    }
    r->props[r->n_props].token = tok;
    r->props[r->n_props].score = v;
    r->n_props++;
    return MJ_OK;
}

/* Compare two integers (bools, int64 or big) from the run's document. */
static int int_cmp(const mj_json *d, uint32_t a, uint32_t b) {
    const mj_jnode *x = &d->v[a], *y = &d->v[b];
    int bx = x->kind == MJ_J_BIGINT, by = y->kind == MJ_J_BIGINT;
    if (!bx && !by) {
        int64_t i = x->kind == MJ_J_INT ? x->i : x->kind == MJ_J_TRUE;
        int64_t j = y->kind == MJ_J_INT ? y->i : y->kind == MJ_J_TRUE;
        return i < j ? -1 : i > j;
    }
    if (bx && by) {
        int nx = d->t[x->s_off] == '-', ny = d->t[y->s_off] == '-';
        int c;
        if (nx != ny) return nx ? -1 : 1;
        if (x->s_len != y->s_len) c = x->s_len < y->s_len ? -1 : 1;
        else c = memcmp(d->t + x->s_off, d->t + y->s_off, x->s_len) < 0 ? -1 : (memcmp(d->t + x->s_off, d->t + y->s_off, x->s_len) > 0);
        return nx ? -c : c;
    }
    /* one big integer: it lies beyond the int64 range on its own side */
    if (bx) return d->t[x->s_off] == '-' ? -1 : 1;
    return d->t[y->s_off] == '-' ? 1 : -1;
}

static const mj_json *g_sort_doc;
static int prop_cmp(const void *pa, const void *pb) {
    const mj_replay_prop *a = (const mj_replay_prop *)pa, *b = (const mj_replay_prop *)pb;
    if (a->score > b->score) return -1;
    if (a->score < b->score) return 1;
    return int_cmp(g_sort_doc, a->token, b->token);
}

/* _best_per_token: one entry per token with its best score, highest first. */
static mj_status best_per_token(mj_replay_run *r, const mj_json *src, uint32_t props, mj_replay_step *s,
                                char err[MJ_REPLAY_ERR_MAX], unsigned long line) {
    uint32_t c;
    mj_status st;
    s->prop_first = r->n_props;
    s->prop_n = 0;
    if (props == MJ_JSON_NONE) return MJ_OK; /* missing: Python's default [] */
    if (src->v[props].kind == MJ_J_OBJ || src->v[props].kind == MJ_J_STR) return MJ_OK; /* iterates to nothing usable */
    if (src->v[props].kind != MJ_J_ARR) {
        errf(err, "line %lu has proposals that are not a list", line);
        return MJ_E_INVAL;
    }
    for (c = src->v[props].first; c != MJ_JSON_NONE; c = src->v[c].next) {
        uint32_t t, v, k;
        double score;
        int found = 0;
        if (src->v[c].kind != MJ_J_ARR || src->v[c].n < 2) continue;
        t = src->v[c].first;
        v = src->v[t].next;
        if (!mj_json_is_integer(src, t) || !mj_json_is_number(src, v)) continue;
        score = mj_json_to_double(src, v) / Q20;
        for (k = s->prop_first; k < r->n_props; ++k) {
            if (mj_json_eq(&r->doc, r->props[k].token, src, t)) {
                if (score > r->props[k].score) r->props[k].score = score;
                found = 1;
                break;
            }
        }
        if (!found) {
            uint32_t tok;
            st = mj_json_copy(&r->doc, src, t, &tok);
            if (st == MJ_OK) st = push_prop(r, tok, score);
            if (st != MJ_OK) {
                errf(err, "line %lu has more proposals than the limit", line);
                return st;
            }
        }
    }
    s->prop_n = r->n_props - s->prop_first;
    g_sort_doc = &r->doc;
    qsort(r->props + s->prop_first, s->prop_n, sizeof *r->props, prop_cmp);
    return MJ_OK;
}

/* A set of hashable values from the run's document. */
typedef struct vset {
    uint32_t *slot;
    uint32_t cap;
} vset;

static int vset_has(const vset *s, const mj_json *d, uint32_t v) {
    uint64_t h = mj_json_hash(d, v);
    uint32_t i = (uint32_t)h & (s->cap - 1u);
    while (s->slot[i] != MJ_JSON_NONE - 1u) {
        uint32_t w = s->slot[i];
        if (mj_json_eq(d, w == MJ_JSON_NONE - 2u ? MJ_JSON_NONE : w, d, v)) return 1;
        i = (i + 1u) & (s->cap - 1u);
    }
    return 0;
}

static void vset_add(vset *s, const mj_json *d, uint32_t v) {
    uint64_t h;
    uint32_t i;
    if (vset_has(s, d, v)) return;
    h = mj_json_hash(d, v);
    i = (uint32_t)h & (s->cap - 1u);
    while (s->slot[i] != MJ_JSON_NONE - 1u) i = (i + 1u) & (s->cap - 1u);
    s->slot[i] = v == MJ_JSON_NONE ? MJ_JSON_NONE - 2u : v; /* a missing value is Python's None */
}

static mj_status auto_label(mj_replay_run *r, char err[MJ_REPLAY_ERR_MAX]) {
    vset seen;
    uint32_t i, cap = 16;
    while (cap < 4u * (r->n_steps + 1u)) cap *= 2u;
    seen.cap = cap;
    seen.slot = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t));
    if (seen.slot == NULL) return MJ_E_LIMIT;
    for (i = 0; i < cap; ++i) seen.slot[i] = MJ_JSON_NONE - 1u;
    for (i = 0; i < r->n_steps; ++i) {
        mj_replay_step *s = &r->steps[i];
        int rev;
        if (!mj_json_hashable(&r->doc, s->state) || !mj_json_hashable(&r->doc, s->next)) {
            free(seen.slot);
            snprintf(err, MJ_REPLAY_ERR_MAX, "step %u has a state that is a list or object", (unsigned)i);
            return MJ_E_INVAL;
        }
        vset_add(&seen, &r->doc, s->state);
        rev = vset_has(&seen, &r->doc, s->next);
        if (s->reward > 0) s->autolab = MJ_AUTO_PRODUCTIVE;
        else if (rev) s->autolab = MJ_AUTO_NOT_YET;
        else s->autolab = MJ_AUTO_GOOD;
        s->revisit = rev;
        vset_add(&seen, &r->doc, s->next);
        s->margin = s->prop_n >= 2 ? r->props[s->prop_first].score - r->props[s->prop_first + 1].score : 1.0;
    }
    free(seen.slot);
    return MJ_OK;
}

static void hex_of(const uint8_t *h, char out[65]) {
    static const char x[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; ++i) {
        out[2 * i] = x[h[i] >> 4];
        out[2 * i + 1] = x[h[i] & 15];
    }
    out[64] = '\0';
}

static const char *base_name(const char *p) {
    const char *b = p, *q;
    for (q = p; *q; ++q) {
        if (*q == '/') b = q + 1;
#if defined(_WIN32)
        if (*q == '\\' || *q == ':') b = q + 1;
#endif
    }
    return b;
}

/* os.path.splitext on a file name: the root, without the last extension
 * unless the name is all dots before it. */
static void name_root(const char *name, char *out, size_t cap) {
    size_t n = strlen(name), dot = n, i, lead = 0;
    while (lead < n && name[lead] == '.') ++lead;
    for (i = n; i-- > lead;)
        if (name[i] == '.') { dot = i; break; }
    if (dot >= cap) dot = cap - 1;
    memcpy(out, name, dot);
    out[dot] = '\0';
}

#define LINE_NODES(n) ((uint32_t)((n) + 16u))

static mj_status copy_field(mj_replay_run *r, const mj_json *src, uint32_t obj, const char *key, uint32_t *out) {
    return mj_json_copy(&r->doc, src, mj_json_get(src, obj, key), out);
}

void mj_replay_free(mj_replay_run *run) {
    if (run->steps) mj_bfree(run->b, run->steps, (size_t)run->cap_steps * sizeof *run->steps);
    if (run->props) mj_bfree(run->b, run->props, (size_t)run->cap_props * sizeof *run->props);
    mj_json_free(&run->doc);
    run->steps = NULL;
    run->props = NULL;
    run->n_steps = run->cap_steps = run->n_props = run->cap_props = 0;
}

mj_status mj_replay_load(const char *path, mj_budget *b, mj_replay_run *run, char err[MJ_REPLAY_ERR_MAX]) {
    uint8_t *data = NULL;
    size_t n = 0, pos, ls, ll;
    uint64_t fsize;
    mj_status st;
    mj_json cur, adv, dec;
    char prev[65], first_sha[65], stem[MJ_PATH_MAX], sid[49];
    unsigned long line = 0;
    int start_ok = 0, ended = 0, has_adv = 0, has_dec = 0;
    uint8_t h[32];
    uint32_t root;

    memset(run, 0, sizeof *run);
    run->b = b;
    run->flags = MJ_JSON_NONE;
    if (err) err[0] = '\0';
    if (mj_fs_size(path, &fsize) != MJ_OK) { errf(err, "run log could not be read", 0); return MJ_E_IO; }
    if (fsize > MJ_REPLAY_MAX_FILE) { errf(err, "run log is larger than the limit", 0); return MJ_E_LIMIT; }
    st = mj_fs_read_all(path, MJ_REPLAY_MAX_FILE, b, &data, &n);
    if (st != MJ_OK) {
        errf(err, st == MJ_E_LIMIT ? "run log is larger than the limit" : "run log could not be read", 0);
        return st;
    }
    mj_json_init(&cur, b, LINE_NODES(n), (uint32_t)n + 16u);
    mj_json_init(&adv, b, LINE_NODES(n), (uint32_t)n + 16u);
    mj_json_init(&dec, b, LINE_NODES(n), (uint32_t)n + 16u);
    mj_json_init(&run->doc, b, 64u * (MJ_REPLAY_MAX_STEPS + 2u) + LINE_NODES(n), (uint32_t)n + 4096u);

    /* pass 1: the whole chain, as Python checks it before anything else */
    memset(prev, '0', 64);
    prev[64] = '\0';
    pos = 0;
    while (next_line(data, n, &pos, &ls, &ll)) {
        const char *raw = (const char *)data + ls;
        size_t cut = rfind_sha(raw, ll), sl;
        const char *sv;
        uint32_t fmt, pv, sh, ev;
        ++line;
        if (cut == (size_t)-1) { errf(err, "line %lu has no sha256", line); st = MJ_E_CHAIN; goto out; }
        mj_json_reset(&cur);
        st = mj_json_parse(&cur, raw, ll, &root);
        if (st != MJ_OK) {
            errf(err, st == MJ_E_LIMIT ? "line %lu is too large or too deep to read" : "line %lu is not JSON", line);
            if (st != MJ_E_LIMIT) st = MJ_E_INVAL;
            goto out;
        }
        fmt = mj_json_get(&cur, root, "format");
        if (cur.v[root].kind != MJ_J_OBJ || !mj_json_str_is(&cur, fmt, MJ_REPLAY_FORMAT)) {
            errf(err, "line %lu is not " MJ_REPLAY_FORMAT, line);
            st = MJ_E_INVAL;
            goto out;
        }
        pv = mj_json_get(&cur, root, "prev");
        if (!mj_json_str_is(&cur, pv, prev)) { errf(err, "line %lu does not link to the line before", line); st = MJ_E_CHAIN; goto out; }
        mj_sha256(raw, cut, h);
        hex_of(h, prev);
        sh = mj_json_get(&cur, root, "sha256");
        sv = mj_json_str(&cur, sh, &sl);
        if (sv == NULL || sl != 64 || memcmp(sv, prev, 64) != 0) { errf(err, "line %lu hash does not match", line); st = MJ_E_CHAIN; goto out; }
        ev = mj_json_get(&cur, root, "event");
        if (line == 1) {
            uint32_t fl = mj_json_get(&cur, root, "flags");
            start_ok = mj_json_str_is(&cur, ev, "start");
            memcpy(first_sha, prev, 65);
            if (fl == MJ_JSON_NONE) st = mj_json_parse(&run->doc, "\"\"", 2, &run->flags);
            else st = mj_json_copy(&run->doc, &cur, fl, &run->flags);
            if (st != MJ_OK) { errf(err, "line %lu is too large to keep", line); goto out; }
        }
        ended = mj_json_str_is(&cur, ev, "end");
    }
    if (line == 0 || !start_ok) { errf(err, "run log does not begin with a start record", 0); st = MJ_E_INVAL; goto out; }

    mj_sha256_hex(data, n, run->source_sha256);
    {
        const char *bn = base_name(path);
        size_t bl = strlen(bn);
        if (bl >= sizeof run->source) bl = sizeof run->source - 1;
        memcpy(run->source, bn, bl);
        run->source[bl] = '\0';
        name_root(bn, stem, sizeof stem);
        mj_safe_id(stem, 48, sid, sizeof sid);
        snprintf(run->id, sizeof run->id, "%s.%.8s", sid, first_sha);
    }
    run->ended = ended;

    /* pass 2: the steps */
    pos = 0;
    line = 0;
    while (next_line(data, n, &pos, &ls, &ll)) {
        uint32_t ev;
        ++line;
        mj_json_reset(&cur);
        if (mj_json_parse(&cur, (const char *)data + ls, ll, &root) != MJ_OK) { st = MJ_E_INVAL; errf(err, "line %lu is not JSON", line); goto out; }
        ev = mj_json_get(&cur, root, "event");
        /* after a reset the top value is node 0, so a kept record's root is 0 */
        if (mj_json_str_is(&cur, ev, "advise")) {
            mj_json t = adv; adv = cur; cur = t;
            has_adv = 1;
            has_dec = 0;
        } else if (mj_json_str_is(&cur, ev, "decided") &&
                   mj_json_eq(&adv, has_adv ? mj_json_get(&adv, 0, "decision") : MJ_JSON_NONE, &cur, mj_json_get(&cur, root, "decision"))) {
            mj_json t = dec; dec = cur; cur = t;
            has_dec = 1;
        } else if (mj_json_str_is(&cur, ev, "transition") && has_dec) {
            mj_replay_step *s;
            uint32_t rq;
            if (!has_adv) { errf(err, "line %lu ends a decision that was never advised", line); st = MJ_E_INVAL; goto out; }
            if (run->n_steps == run->cap_steps) {
                uint32_t cap = run->cap_steps ? run->cap_steps * 2u : 64u;
                mj_replay_step *ns;
                if (cap > MJ_REPLAY_MAX_STEPS + 1u) cap = MJ_REPLAY_MAX_STEPS + 1u;
                ns = (mj_replay_step *)mj_balloc(b, (size_t)cap * sizeof *ns);
                if (ns == NULL) { errf(err, "run log is too large to read", 0); st = MJ_E_LIMIT; goto out; }
                if (run->n_steps) memcpy(ns, run->steps, (size_t)run->n_steps * sizeof *ns);
                if (run->steps) mj_bfree(b, run->steps, (size_t)run->cap_steps * sizeof *ns);
                run->steps = ns;
                run->cap_steps = cap;
            }
            s = &run->steps[run->n_steps];
            memset(s, 0, sizeof *s);
            s->index = run->n_steps;
            st = copy_field(run, &adv, 0, "decision", &s->decision);
            if (st == MJ_OK) st = copy_field(run, &adv, 0, "task", &s->task);
            if (st == MJ_OK) st = copy_field(run, &adv, 0, "x", &s->x);
            if (st == MJ_OK) st = copy_field(run, &adv, 0, "y", &s->y);
            if (st == MJ_OK) st = copy_field(run, &adv, 0, "key", &s->key);
            if (st == MJ_OK) st = copy_field(run, &adv, 0, "open", &s->open);
            if (st == MJ_OK) st = copy_field(run, &dec, 0, "token", &s->token);
            if (st == MJ_OK) st = copy_field(run, &cur, root, "action", &s->action);
            if (st == MJ_OK) st = copy_field(run, &cur, root, "state", &s->state);
            if (st == MJ_OK) st = copy_field(run, &cur, root, "next", &s->next);
            if (st == MJ_OK) st = copy_field(run, &cur, root, "ok", &s->ok);
            if (st == MJ_OK) st = copy_field(run, &cur, root, "sha256", &s->record);
            if (st != MJ_OK) { errf(err, "line %lu is too large to keep", line); goto out; }
            st = best_per_token(run, &adv, mj_json_get(&adv, 0, "proposals"), s, err, line);
            if (st != MJ_OK) goto out;
            rq = mj_json_get(&cur, root, "reward_q20");
            if (rq != MJ_JSON_NONE && !mj_json_is_number(&cur, rq)) { errf(err, "line %lu has a reward that is not a number", line); st = MJ_E_INVAL; goto out; }
            s->reward = rq == MJ_JSON_NONE ? 0.0 : mj_json_to_double(&cur, rq) / Q20;
            has_adv = has_dec = 0;
            run->n_steps++;
            if (run->n_steps > MJ_REPLAY_MAX_STEPS) { errf(err, "run has more steps than the limit", 0); st = MJ_E_LIMIT; goto out; }
        }
    }
    st = auto_label(run, err);
out:
    mj_json_free(&cur);
    mj_json_free(&adv);
    mj_json_free(&dec);
    mj_bfree(b, data, n ? n : 1);
    if (st != MJ_OK) mj_replay_free(run);
    return st;
}

uint32_t mj_replay_score_bp(const mj_replay_run *run) {
    uint32_t i, good = 0;
    if (run->n_steps == 0) return 0;
    for (i = 0; i < run->n_steps; ++i)
        if (run->steps[i].autolab != MJ_AUTO_NOT_YET) ++good;
    return (uint32_t)((uint64_t)good * 10000u / run->n_steps);
}

int mj_replay_open_bits(const mj_json *d, uint32_t v, unsigned *bits) {
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

/* ---- the folder scan ---- */

#if defined(_WIN32)
#define SEP '\\'
static int is_sep(char c) { return c == '\\' || c == '/'; }
#else
#define SEP '/'
static int is_sep(char c) { return c == '/'; }
#endif

typedef struct scan {
    char *buf;
    size_t n, cap;
    uint32_t count;
    mj_status err;
} scan;

static int join(char *out, size_t cap, const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int need = la > 0 && !is_sep(a[la - 1]);
    if (la + (size_t)need + lb + 1 > cap) return 0;
    memcpy(out, a, la);
    if (need) out[la++] = SEP;
    memcpy(out + la, b, lb + 1);
    return 1;
}

static int ends_jsonl(const char *name) {
    size_t n = strlen(name);
    const char *e = ".jsonl";
    size_t i;
    if (n < 6) return 0;
    for (i = 0; i < 6; ++i) {
        char c = name[n - 6 + i];
#if defined(_WIN32)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); /* glob ignores case on Windows */
#endif
        if (c != e[i]) return 0;
    }
    return 1;
}

static int first_line_is_run(const char *path) {
    static const char marker[] = "\"format\":\"" MJ_REPLAY_FORMAT "\"";
    char buf[4097];
    size_t got = 0, k;
    FILE *f = NULL;
#if defined(_WIN32)
    wchar_t w[MJ_PATH_MAX];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, (int)MJ_PATH_MAX) > 0) f = _wfopen(w, L"rb");
#else
    f = fopen(path, "rb");
#endif
    if (f == NULL) return 0;
    got = fread(buf, 1, 4096, f);
    fclose(f);
    for (k = 0; k < got; ++k)
        if (buf[k] == '\n') { got = k + 1; break; }
    buf[got] = '\0';
    if (got < sizeof marker - 1) return 0;
    for (k = 0; k + sizeof marker - 1 <= got; ++k)
        if (memcmp(buf + k, marker, sizeof marker - 1) == 0) return 1;
    return 0;
}

static mj_status scan_add(scan *s, const char *path) {
    size_t l = strlen(path) + 1;
    if (s->count >= MJ_REPLAY_MAX_RUNS) return MJ_E_LIMIT;
    if (s->n + l > s->cap) {
        size_t cap = s->cap ? s->cap * 2u : 4096u;
        char *nb;
        while (cap < s->n + l) cap *= 2u;
        nb = (char *)realloc(s->buf, cap);
        if (nb == NULL) return MJ_E_LIMIT;
        s->buf = nb;
        s->cap = cap;
    }
    memcpy(s->buf + s->n, path, l);
    s->n += l;
    s->count++;
    return MJ_OK;
}

typedef struct walk_ctx {
    scan *s;
    const char *dir;
    unsigned depth;
    char **subdirs;
    uint32_t n_sub, cap_sub;
} walk_ctx;

static int walk_entry(const char *name, int is_dir, void *user) {
    walk_ctx *w = (walk_ctx *)user;
    char full[MJ_PATH_MAX];
    if (name[0] == '.') return 0; /* glob leaves hidden names out */
    if (!join(full, sizeof full, w->dir, name)) return 0;
    if (is_dir) {
        if (w->n_sub == w->cap_sub) {
            uint32_t cap = w->cap_sub ? w->cap_sub * 2u : 16u;
            char **ns = (char **)realloc(w->subdirs, cap * sizeof *ns);
            if (ns == NULL) { w->s->err = MJ_E_LIMIT; return 1; }
            w->subdirs = ns;
            w->cap_sub = cap;
        }
        w->subdirs[w->n_sub] = (char *)malloc(strlen(full) + 1);
        if (w->subdirs[w->n_sub] == NULL) { w->s->err = MJ_E_LIMIT; return 1; }
        strcpy(w->subdirs[w->n_sub++], full);
        return 0;
    }
    if (ends_jsonl(name) && first_line_is_run(full)) {
        mj_status st = scan_add(w->s, full);
        if (st != MJ_OK) { w->s->err = st; return 1; }
    }
    return 0;
}

static void walk(scan *s, const char *dir, unsigned depth) {
    walk_ctx w;
    uint32_t i;
    memset(&w, 0, sizeof w);
    w.s = s;
    w.dir = dir;
    w.depth = depth;
    if (mj_fs_list_typed(dir, 100000u, walk_entry, &w) == MJ_E_LIMIT && s->err == MJ_OK) s->err = MJ_E_LIMIT;
    for (i = 0; i < w.n_sub; ++i) {
        if (s->err == MJ_OK && depth + 1u < MJ_REPLAY_MAX_DEPTH) walk(s, w.subdirs[i], depth + 1u);
        free(w.subdirs[i]);
    }
    free(w.subdirs);
}

static int str_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

mj_status mj_replay_find(const char *const *roots, uint32_t n_roots, char **paths, uint32_t *n_paths) {
    scan s;
    uint32_t i;
    char **idx;
    char *out;
    size_t off = 0;
    memset(&s, 0, sizeof s);
    *paths = NULL;
    *n_paths = 0;
    for (i = 0; i < n_roots && s.err == MJ_OK; ++i) walk(&s, roots[i], 0);
    if (s.err != MJ_OK) { free(s.buf); return s.err; }
    if (s.count == 0) { free(s.buf); return MJ_OK; }
    idx = (char **)malloc(s.count * sizeof *idx);
    out = (char *)malloc(s.n);
    if (idx == NULL || out == NULL) { free(idx); free(out); free(s.buf); return MJ_E_LIMIT; }
    for (i = 0; i < s.count; ++i) {
        idx[i] = s.buf + off;
        off += strlen(s.buf + off) + 1;
    }
    qsort(idx, s.count, sizeof *idx, str_cmp);
    off = 0;
    for (i = 0; i < s.count; ++i) {
        size_t l = strlen(idx[i]) + 1;
        memcpy(out + off, idx[i], l);
        off += l;
    }
    free(idx);
    free(s.buf);
    *paths = out;
    *n_paths = s.count;
    return MJ_OK;
}

mj_status mj_replay_load_all(const char *const *roots, uint32_t n_roots, mj_budget *b, mj_replay_error_fn on_error,
                             void *user, mj_replay_set *out) {
    char *paths = NULL, *p;
    uint32_t n = 0, i, j;
    mj_status st;
    memset(out, 0, sizeof *out);
    st = mj_replay_find(roots, n_roots, &paths, &n);
    if (st != MJ_OK) return st;
    if (n == 0) return MJ_OK;
    out->runs = (mj_replay_run *)calloc(n, sizeof *out->runs);
    if (out->runs == NULL) { free(paths); return MJ_E_LIMIT; }
    for (i = 0, p = paths; i < n; ++i, p += strlen(p) + 1) {
        char err[MJ_REPLAY_ERR_MAX];
        mj_replay_run *r = &out->runs[out->n];
        int dup = 0;
        st = mj_replay_load(p, b, r, err);
        if (st != MJ_OK) {
            if (on_error) on_error(p, err, user);
            continue;
        }
        for (j = 0; j < out->n; ++j)
            if (strcmp(out->runs[j].source_sha256, r->source_sha256) == 0) { dup = 1; break; }
        if (dup) { mj_replay_free(r); continue; } /* the same run copied to two places */
        out->n++;
    }
    free(paths);
    return MJ_OK;
}

void mj_replay_set_free(mj_replay_set *s) {
    uint32_t i;
    for (i = 0; i < s->n; ++i) mj_replay_free(&s->runs[i]);
    free(s->runs);
    s->runs = NULL;
    s->n = 0;
}

/* ---- the dump ---- */

static void kv(mj_jout *o, const char *k, int first) {
    if (!first) mj_jout_raw(o, ",", 1);
    mj_jout_string(o, k, strlen(k));
    mj_jout_raw(o, ":", 1);
}

void mj_replay_dump(mj_jout *o, const mj_replay_run *runs, uint32_t n) {
    uint32_t i, k, p;
    mj_jout_raw(o, "[", 1);
    for (i = 0; i < n; ++i) {
        const mj_replay_run *r = &runs[i];
        if (i) mj_jout_raw(o, ",", 1);
        mj_jout_raw(o, "{", 1);
        kv(o, "ended", 1);
        mj_jout_cstr(o, r->ended ? "true" : "false");
        kv(o, "flags", 0);
        mj_jout_value(o, &r->doc, r->flags);
        kv(o, "id", 0);
        mj_jout_string(o, r->id, strlen(r->id));
        kv(o, "source", 0);
        mj_jout_string(o, r->source, strlen(r->source));
        kv(o, "source_sha256", 0);
        mj_jout_string(o, r->source_sha256, 64);
        kv(o, "steps", 0);
        mj_jout_raw(o, "[", 1);
        for (k = 0; k < r->n_steps; ++k) {
            const mj_replay_step *s = &r->steps[k];
            const char *al = mj_auto_label_name(s->autolab);
            if (k) mj_jout_raw(o, ",", 1);
            mj_jout_raw(o, "{", 1);
            kv(o, "action", 1); mj_jout_value(o, &r->doc, s->action);
            kv(o, "auto", 0); mj_jout_string(o, al, strlen(al));
            kv(o, "decision", 0); mj_jout_value(o, &r->doc, s->decision);
            kv(o, "index", 0); mj_jout_int(o, s->index);
            kv(o, "key", 0); mj_jout_value(o, &r->doc, s->key);
            kv(o, "margin", 0); mj_jout_float(o, s->margin);
            kv(o, "next", 0); mj_jout_value(o, &r->doc, s->next);
            kv(o, "ok", 0); mj_jout_value(o, &r->doc, s->ok);
            kv(o, "open", 0); mj_jout_value(o, &r->doc, s->open);
            kv(o, "proposals", 0);
            mj_jout_raw(o, "[", 1);
            for (p = 0; p < s->prop_n; ++p) {
                if (p) mj_jout_raw(o, ",", 1);
                mj_jout_raw(o, "[", 1);
                mj_jout_value(o, &r->doc, r->props[s->prop_first + p].token);
                mj_jout_raw(o, ",", 1);
                mj_jout_float(o, r->props[s->prop_first + p].score);
                mj_jout_raw(o, "]", 1);
            }
            mj_jout_raw(o, "]", 1);
            kv(o, "record", 0); mj_jout_value(o, &r->doc, s->record);
            kv(o, "revisit", 0); mj_jout_cstr(o, s->revisit ? "true" : "false");
            kv(o, "reward", 0); mj_jout_float(o, s->reward);
            kv(o, "state", 0); mj_jout_value(o, &r->doc, s->state);
            kv(o, "task", 0); mj_jout_value(o, &r->doc, s->task);
            kv(o, "token", 0); mj_jout_value(o, &r->doc, s->token);
            kv(o, "x", 0); mj_jout_value(o, &r->doc, s->x);
            kv(o, "y", 0); mj_jout_value(o, &r->doc, s->y);
            mj_jout_raw(o, "}", 1);
        }
        mj_jout_raw(o, "]}", 2);
    }
    mj_jout_raw(o, "]", 1);
}
