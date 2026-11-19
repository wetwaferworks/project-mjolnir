/* Mjolnir: a small JSON reader for PCHIE run logs (see mj_json.h).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_json.h"
#include "mjolnir/mj_delta.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mj_json_init(mj_json *d, mj_budget *b, uint32_t max_nodes, uint32_t max_text) {
    memset(d, 0, sizeof *d);
    d->b = b;
    d->max_nodes = max_nodes;
    d->max_text = max_text;
}

void mj_json_free(mj_json *d) {
    if (d->v) mj_bfree(d->b, d->v, (size_t)d->cap_v * sizeof *d->v);
    if (d->t) mj_bfree(d->b, d->t, d->cap_t);
    d->v = NULL;
    d->t = NULL;
    d->nv = d->cap_v = d->nt = d->cap_t = 0;
}

void mj_json_reset(mj_json *d) {
    d->nv = 0;
    d->nt = 0;
}

static mj_status grow_nodes(mj_json *d) {
    uint32_t cap;
    mj_jnode *nv;
    if (d->nv < d->cap_v) return MJ_OK;
    if (d->nv >= d->max_nodes) return MJ_E_LIMIT;
    cap = d->cap_v ? d->cap_v * 2u : 64u;
    if (cap > d->max_nodes || cap < d->cap_v) cap = d->max_nodes;
    nv = (mj_jnode *)mj_balloc(d->b, (size_t)cap * sizeof *nv);
    if (nv == NULL) return MJ_E_LIMIT;
    if (d->nv) memcpy(nv, d->v, (size_t)d->nv * sizeof *nv);
    if (d->v) mj_bfree(d->b, d->v, (size_t)d->cap_v * sizeof *d->v);
    d->v = nv;
    d->cap_v = cap;
    return MJ_OK;
}

static mj_status need_text(mj_json *d, size_t more) {
    size_t want = (size_t)d->nt + more, cap;
    char *nt;
    if (want <= d->cap_t) return MJ_OK;
    if (want > d->max_text) return MJ_E_LIMIT;
    cap = d->cap_t ? d->cap_t : 256u;
    while (cap < want) cap *= 2u;
    if (cap > d->max_text) cap = d->max_text;
    nt = (char *)mj_balloc(d->b, cap);
    if (nt == NULL) return MJ_E_LIMIT;
    if (d->nt) memcpy(nt, d->t, d->nt);
    if (d->t) mj_bfree(d->b, d->t, d->cap_t);
    d->t = nt;
    d->cap_t = (uint32_t)cap;
    return MJ_OK;
}

static mj_status new_node(mj_json *d, uint32_t kind, uint32_t *out) {
    mj_status st = grow_nodes(d);
    mj_jnode *x;
    if (st != MJ_OK) return st;
    x = &d->v[d->nv];
    memset(x, 0, sizeof *x);
    x->kind = kind;
    x->first = MJ_JSON_NONE;
    x->next = MJ_JSON_NONE;
    *out = d->nv++;
    return MJ_OK;
}

/* ---- parsing ---- */

typedef struct jp {
    const unsigned char *s;
    size_t i, n;
    mj_json *d;
    mj_status err;
} jp;

static void ws(jp *p) {
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) ++p->i;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex4(jp *p, uint32_t *v) {
    uint32_t r = 0;
    int k, h;
    if (p->n - p->i < 4) return 0;
    for (k = 0; k < 4; ++k) {
        h = hexval(p->s[p->i + (size_t)k]);
        if (h < 0) return 0;
        r = (r << 4) | (uint32_t)h;
    }
    p->i += 4;
    *v = r;
    return 1;
}

static size_t put_utf8(uint32_t cp, unsigned char out[4]) {
    if (cp < 0x80) { out[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) { out[0] = (unsigned char)(0xC0 | (cp >> 6)); out[1] = (unsigned char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t mj_utf8_len(const unsigned char *s, size_t n, uint32_t *cp_out) {
    uint32_t c = s[0], cp;
    size_t len, k;
    if (c < 0x80) { *cp_out = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
    else return 0;
    if (n < len) return 0;
    for (k = 1; k < len; ++k) {
        if ((s[k] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (s[k] & 0x3Fu);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
    *cp_out = cp;
    return len;
}

static int parse_string(jp *p, uint32_t *off, uint32_t *len) {
    mj_json *d = p->d;
    size_t start;
    ++p->i; /* opening quote */
    if (need_text(d, 0) != MJ_OK) { p->err = MJ_E_LIMIT; return 0; }
    start = d->nt;
    for (;;) {
        unsigned char c;
        unsigned char buf[4];
        size_t bl;
        if (p->i >= p->n) return 0;
        c = p->s[p->i];
        if (c == '"') { ++p->i; break; }
        if (c < 0x20) return 0;
        if (c == '\\') {
            uint32_t cp;
            if (p->i + 1 >= p->n) return 0;
            c = p->s[p->i + 1];
            p->i += 2;
            switch (c) {
            case '"': buf[0] = '"'; bl = 1; break;
            case '\\': buf[0] = '\\'; bl = 1; break;
            case '/': buf[0] = '/'; bl = 1; break;
            case 'b': buf[0] = '\b'; bl = 1; break;
            case 'f': buf[0] = '\f'; bl = 1; break;
            case 'n': buf[0] = '\n'; bl = 1; break;
            case 'r': buf[0] = '\r'; bl = 1; break;
            case 't': buf[0] = '\t'; bl = 1; break;
            case 'u':
                if (!hex4(p, &cp)) return 0;
                if (cp >= 0xD800 && cp <= 0xDBFF && p->n - p->i >= 6 && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                    uint32_t lo;
                    size_t save = p->i;
                    p->i += 2;
                    if (hex4(p, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else p->i = save; /* a lone high surrogate, kept as is */
                }
                bl = put_utf8(cp, buf);
                break;
            default: return 0;
            }
        } else if (c < 0x80) {
            buf[0] = c;
            bl = 1;
            ++p->i;
        } else {
            uint32_t cp;
            bl = mj_utf8_len(p->s + p->i, p->n - p->i, &cp);
            if (bl == 0) return 0;
            memcpy(buf, p->s + p->i, bl);
            p->i += bl;
        }
        if (need_text(d, bl) != MJ_OK) { p->err = MJ_E_LIMIT; return 0; }
        memcpy(d->t + d->nt, buf, bl);
        d->nt += (uint32_t)bl;
    }
    *off = (uint32_t)start;
    *len = (uint32_t)(d->nt - start);
    return 1;
}

static int parse_number(jp *p, uint32_t *out) {
    size_t st = p->i, ds;
    int is_float = 0;
    char tmp[64];
    char *big = NULL;
    const char *txt;
    size_t len;
    mj_jnode *x;
    if (p->i < p->n && p->s[p->i] == '-') ++p->i;
    if (p->i >= p->n) return 0;
    ds = p->i;
    if (p->s[p->i] == '0') ++p->i;
    else if (p->s[p->i] >= '1' && p->s[p->i] <= '9') while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') ++p->i;
    else return 0;
    (void)ds;
    if (p->i < p->n && p->s[p->i] == '.' && p->i + 1 < p->n && p->s[p->i + 1] >= '0' && p->s[p->i + 1] <= '9') {
        is_float = 1;
        ++p->i;
        while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') ++p->i;
    }
    if (p->i < p->n && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        size_t save = p->i;
        ++p->i;
        if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-')) ++p->i;
        if (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') {
            is_float = 1;
            while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') ++p->i;
        } else {
            p->i = save; /* "1e" is the number 1 followed by junk, as in Python */
        }
    }
    len = p->i - st;
    if (len < sizeof tmp) {
        memcpy(tmp, p->s + st, len);
        tmp[len] = '\0';
        txt = tmp;
    } else {
        big = (char *)malloc(len + 1);
        if (big == NULL) { p->err = MJ_E_LIMIT; return 0; }
        memcpy(big, p->s + st, len);
        big[len] = '\0';
        txt = big;
    }
    if (is_float) {
        if (new_node(p->d, MJ_J_FLOAT, out) != MJ_OK) { p->err = MJ_E_LIMIT; free(big); return 0; }
        p->d->v[*out].f = strtod(txt, NULL);
    } else {
        long long v;
        errno = 0;
        v = strtoll(txt, NULL, 10);
        if (errno == ERANGE) {
            if (new_node(p->d, MJ_J_BIGINT, out) != MJ_OK || need_text(p->d, len) != MJ_OK) {
                p->err = MJ_E_LIMIT;
                free(big);
                return 0;
            }
            x = &p->d->v[*out];
            x->s_off = p->d->nt;
            x->s_len = (uint32_t)len;
            memcpy(p->d->t + p->d->nt, txt, len);
            p->d->nt += (uint32_t)len;
            x->f = strtod(txt, NULL);
        } else {
            if (new_node(p->d, MJ_J_INT, out) != MJ_OK) { p->err = MJ_E_LIMIT; free(big); return 0; }
            p->d->v[*out].i = (int64_t)v;
        }
    }
    free(big);
    return 1;
}

static int lit(jp *p, const char *w) {
    size_t k = strlen(w);
    if (p->n - p->i < k || memcmp(p->s + p->i, w, k) != 0) return 0;
    p->i += k;
    return 1;
}

static int parse_value(jp *p, unsigned depth, uint32_t *out) {
    int c;
    ws(p);
    if (p->i >= p->n) return 0;
    c = p->s[p->i];
    if (c == '{' || c == '[') {
        uint32_t me, last = MJ_JSON_NONE;
        int obj = c == '{';
        if (depth >= MJ_JSON_MAX_DEPTH) { p->err = MJ_E_LIMIT; return 0; }
        if (new_node(p->d, obj ? MJ_J_OBJ : MJ_J_ARR, &me) != MJ_OK) { p->err = MJ_E_LIMIT; return 0; }
        ++p->i;
        ws(p);
        if (p->i < p->n && p->s[p->i] == (obj ? '}' : ']')) { ++p->i; *out = me; return 1; }
        for (;;) {
            uint32_t koff = 0, klen = 0, child;
            if (obj) {
                ws(p);
                if (p->i >= p->n || p->s[p->i] != '"') return 0;
                if (!parse_string(p, &koff, &klen)) return 0;
                ws(p);
                if (p->i >= p->n || p->s[p->i] != ':') return 0;
                ++p->i;
            }
            if (!parse_value(p, depth + 1, &child)) return 0;
            p->d->v[child].key_off = koff;
            p->d->v[child].key_len = klen;
            if (last == MJ_JSON_NONE) p->d->v[me].first = child;
            else p->d->v[last].next = child;
            last = child;
            p->d->v[me].n++;
            ws(p);
            if (p->i >= p->n) return 0;
            if (p->s[p->i] == ',') { ++p->i; continue; }
            if (p->s[p->i] == (obj ? '}' : ']')) { ++p->i; break; }
            return 0;
        }
        *out = me;
        return 1;
    }
    if (c == '"') {
        uint32_t off, len, me;
        if (!parse_string(p, &off, &len)) return 0;
        if (new_node(p->d, MJ_J_STR, &me) != MJ_OK) { p->err = MJ_E_LIMIT; return 0; }
        p->d->v[me].s_off = off;
        p->d->v[me].s_len = len;
        *out = me;
        return 1;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(p, out);
    if (lit(p, "true")) return new_node(p->d, MJ_J_TRUE, out) == MJ_OK || ((p->err = MJ_E_LIMIT), 0);
    if (lit(p, "false")) return new_node(p->d, MJ_J_FALSE, out) == MJ_OK || ((p->err = MJ_E_LIMIT), 0);
    if (lit(p, "null")) return new_node(p->d, MJ_J_NULL, out) == MJ_OK || ((p->err = MJ_E_LIMIT), 0);
    return 0;
}

mj_status mj_json_parse(mj_json *d, const char *s, size_t n, uint32_t *root) {
    jp p;
    uint32_t r, nv0 = d->nv, nt0 = d->nt;
    if (d == NULL || (s == NULL && n) || root == NULL) return MJ_E_INVAL;
    p.s = (const unsigned char *)s;
    p.i = 0;
    p.n = n;
    p.d = d;
    p.err = MJ_OK;
    if (!parse_value(&p, 0, &r)) goto fail;
    ws(&p);
    if (p.i != p.n) goto fail;
    *root = r;
    return MJ_OK;
fail:
    d->nv = nv0;
    d->nt = nt0;
    return p.err != MJ_OK ? p.err : MJ_E_INVAL;
}

/* ---- questions ---- */

static int key_is(const mj_json *d, const mj_jnode *x, const char *k, size_t kl) {
    return x->key_len == kl && (kl == 0 || memcmp(d->t + x->key_off, k, kl) == 0);
}

static uint32_t get_n(const mj_json *d, uint32_t obj, const char *key, size_t kl) {
    uint32_t c, found = MJ_JSON_NONE;
    if (obj == MJ_JSON_NONE || d->v[obj].kind != MJ_J_OBJ) return MJ_JSON_NONE;
    for (c = d->v[obj].first; c != MJ_JSON_NONE; c = d->v[c].next)
        if (key_is(d, &d->v[c], key, kl)) found = c;
    return found;
}

uint32_t mj_json_get(const mj_json *d, uint32_t obj, const char *key) { return get_n(d, obj, key, strlen(key)); }

const char *mj_json_str(const mj_json *d, uint32_t v, size_t *len) {
    if (v == MJ_JSON_NONE || d->v[v].kind != MJ_J_STR) return NULL;
    if (len) *len = d->v[v].s_len;
    return d->t + d->v[v].s_off;
}

int mj_json_str_is(const mj_json *d, uint32_t v, const char *s) {
    size_t n, k = strlen(s);
    const char *p = mj_json_str(d, v, &n);
    return p != NULL && n == k && memcmp(p, s, k) == 0;
}

static uint32_t kind_of(const mj_json *d, uint32_t v) { return v == MJ_JSON_NONE ? MJ_J_NULL : d->v[v].kind; }

int mj_json_truthy(const mj_json *d, uint32_t v) {
    const mj_jnode *x;
    if (v == MJ_JSON_NONE) return 0;
    x = &d->v[v];
    switch (x->kind) {
    case MJ_J_TRUE: case MJ_J_BIGINT: return 1;
    case MJ_J_INT: return x->i != 0;
    case MJ_J_FLOAT: return x->f != 0.0;
    case MJ_J_STR: return x->s_len != 0;
    case MJ_J_ARR: case MJ_J_OBJ: return x->n != 0;
    default: return 0;
    }
}

int mj_json_is_number(const mj_json *d, uint32_t v) {
    uint32_t k = kind_of(d, v);
    return v != MJ_JSON_NONE && (k == MJ_J_FALSE || k == MJ_J_TRUE || k == MJ_J_INT || k == MJ_J_BIGINT || k == MJ_J_FLOAT);
}

int mj_json_is_integer(const mj_json *d, uint32_t v) {
    uint32_t k = kind_of(d, v);
    return v != MJ_JSON_NONE && (k == MJ_J_FALSE || k == MJ_J_TRUE || k == MJ_J_INT || k == MJ_J_BIGINT);
}

int mj_json_hashable(const mj_json *d, uint32_t v) {
    uint32_t k = kind_of(d, v);
    return k != MJ_J_ARR && k != MJ_J_OBJ;
}

double mj_json_to_double(const mj_json *d, uint32_t v) {
    switch (kind_of(d, v)) {
    case MJ_J_TRUE: return 1.0;
    case MJ_J_INT: return (double)d->v[v].i;
    case MJ_J_BIGINT: case MJ_J_FLOAT: return d->v[v].f;
    default: return 0.0;
    }
}

/* Integer value of a bool or int; 0 if not one. */
static int as_i64(const mj_json *d, uint32_t v, int64_t *out) {
    switch (kind_of(d, v)) {
    case MJ_J_FALSE: *out = 0; return 1;
    case MJ_J_TRUE: *out = 1; return 1;
    case MJ_J_INT: *out = d->v[v].i; return 1;
    default: return 0;
    }
}

static int float_eq_int(double f, int64_t i) {
    int64_t t;
    if (!(f >= -9223372036854775808.0 && f < 9223372036854775808.0)) return 0;
    t = (int64_t)f;
    return (double)t == f && t == i;
}

static int float_eq_big(double f, const char *digits, size_t n) {
    char buf[400];
    int k;
    if (!isfinite(f) || floor(f) != f) return 0;
    k = snprintf(buf, sizeof buf, "%.0f", f);
    return k > 0 && (size_t)k == n && memcmp(buf, digits, n) == 0;
}

static uint32_t unique_count(const mj_json *d, uint32_t obj) {
    uint32_t c, n = 0;
    for (c = d->v[obj].first; c != MJ_JSON_NONE; c = d->v[c].next)
        if (get_n(d, obj, d->t + d->v[c].key_off, d->v[c].key_len) == c) ++n;
    return n;
}

int mj_json_eq(const mj_json *da, uint32_t a, const mj_json *db, uint32_t b) {
    uint32_t ka = kind_of(da, a), kb = kind_of(db, b);
    int64_t ia, ib;
    if (mj_json_is_number(da, a) && mj_json_is_number(db, b)) {
        int ha = as_i64(da, a, &ia), hb = as_i64(db, b, &ib);
        if (ha && hb) return ia == ib;
        if (ka == MJ_J_BIGINT && kb == MJ_J_BIGINT)
            return da->v[a].s_len == db->v[b].s_len && memcmp(da->t + da->v[a].s_off, db->t + db->v[b].s_off, da->v[a].s_len) == 0;
        if (ka == MJ_J_FLOAT && kb == MJ_J_FLOAT) return da->v[a].f == db->v[b].f;
        if (ka == MJ_J_FLOAT && hb) return float_eq_int(da->v[a].f, ib);
        if (kb == MJ_J_FLOAT && ha) return float_eq_int(db->v[b].f, ia);
        if (ka == MJ_J_FLOAT && kb == MJ_J_BIGINT) return float_eq_big(da->v[a].f, db->t + db->v[b].s_off, db->v[b].s_len);
        if (kb == MJ_J_FLOAT && ka == MJ_J_BIGINT) return float_eq_big(db->v[b].f, da->t + da->v[a].s_off, da->v[a].s_len);
        return 0; /* an int64 and a big integer are never equal */
    }
    if (ka != kb) return 0;
    if (ka == MJ_J_NULL) return 1;
    if (ka == MJ_J_STR)
        return da->v[a].s_len == db->v[b].s_len && memcmp(da->t + da->v[a].s_off, db->t + db->v[b].s_off, da->v[a].s_len) == 0;
    if (ka == MJ_J_ARR) {
        uint32_t x = da->v[a].first, y = db->v[b].first;
        if (da->v[a].n != db->v[b].n) return 0;
        for (; x != MJ_JSON_NONE; x = da->v[x].next, y = db->v[y].next)
            if (!mj_json_eq(da, x, db, y)) return 0;
        return 1;
    }
    if (ka == MJ_J_OBJ) {
        uint32_t c;
        if (unique_count(da, a) != unique_count(db, b)) return 0;
        for (c = da->v[a].first; c != MJ_JSON_NONE; c = da->v[c].next) {
            uint32_t other;
            if (get_n(da, a, da->t + da->v[c].key_off, da->v[c].key_len) != c) continue; /* an overridden repeat */
            other = get_n(db, b, da->t + da->v[c].key_off, da->v[c].key_len);
            if (other == MJ_JSON_NONE || !mj_json_eq(da, c, db, other)) return 0;
        }
        return 1;
    }
    return 0;
}

uint64_t mj_json_hash(const mj_json *d, uint32_t v) {
    uint64_t h = 1469598103934665603ull;
    uint32_t k = kind_of(d, v);
    if (k == MJ_J_STR) {
        uint32_t i;
        for (i = 0; i < d->v[v].s_len; ++i) h = (h ^ (unsigned char)d->t[d->v[v].s_off + i]) * 1099511628211ull;
        return h ^ 0x5354ull;
    }
    if (mj_json_is_number(d, v)) {
        double f = mj_json_to_double(d, v);
        uint64_t bits;
        if (f == 0.0) f = 0.0; /* -0.0 and 0.0 are equal */
        memcpy(&bits, &f, sizeof bits);
        return (bits ^ (bits >> 29)) * 0x9E3779B97F4A7C15ull;
    }
    return 0x4E554C4Cull; /* null */
}

static mj_status copy_rec(mj_json *dst, const mj_json *src, uint32_t sv, unsigned depth, uint32_t *out) {
    uint32_t me, c, last = MJ_JSON_NONE;
    const mj_jnode *x = &src->v[sv];
    mj_status st;
    if (depth > MJ_JSON_MAX_DEPTH) return MJ_E_LIMIT;
    st = new_node(dst, x->kind, &me);
    if (st != MJ_OK) return st;
    dst->v[me].i = x->i;
    dst->v[me].f = x->f;
    if (x->kind == MJ_J_STR || x->kind == MJ_J_BIGINT) {
        st = need_text(dst, x->s_len);
        if (st != MJ_OK) return st;
        if (x->s_len) memcpy(dst->t + dst->nt, src->t + x->s_off, x->s_len);
        dst->v[me].s_off = dst->nt;
        dst->v[me].s_len = x->s_len;
        dst->nt += x->s_len;
    }
    for (c = x->first; c != MJ_JSON_NONE; c = src->v[c].next) {
        uint32_t ch;
        st = copy_rec(dst, src, c, depth + 1, &ch);
        if (st != MJ_OK) return st;
        st = need_text(dst, src->v[c].key_len);
        if (st != MJ_OK) return st;
        if (src->v[c].key_len) memcpy(dst->t + dst->nt, src->t + src->v[c].key_off, src->v[c].key_len);
        dst->v[ch].key_off = dst->nt;
        dst->v[ch].key_len = src->v[c].key_len;
        dst->nt += src->v[c].key_len;
        if (last == MJ_JSON_NONE) dst->v[me].first = ch;
        else dst->v[last].next = ch;
        dst->v[me].n++;
        last = ch;
    }
    *out = me;
    return MJ_OK;
}

mj_status mj_json_copy(mj_json *dst, const mj_json *src, uint32_t src_v, uint32_t *out) {
    uint32_t nv0 = dst->nv, nt0 = dst->nt;
    mj_status st;
    if (src_v == MJ_JSON_NONE) { *out = MJ_JSON_NONE; return MJ_OK; }
    st = copy_rec(dst, src, src_v, 0, out);
    if (st != MJ_OK) {
        dst->nv = nv0;
        dst->nt = nt0;
    }
    return st;
}

/* ---- writing ---- */

void mj_jout_init(mj_jout *o, size_t max) {
    memset(o, 0, sizeof *o);
    o->max = max;
}

void mj_jout_free(mj_jout *o) {
    free(o->p);
    memset(o, 0, sizeof *o);
}

void mj_jout_raw(mj_jout *o, const char *s, size_t n) {
    if (o->failed) return;
    if (o->n + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 1024u;
        char *np;
        while (cap < o->n + n + 1) cap *= 2u;
        if (cap > o->max) cap = o->max;
        if (o->n + n + 1 > cap) { o->failed = 1; return; }
        np = (char *)realloc(o->p, cap);
        if (np == NULL) { o->failed = 1; return; }
        o->p = np;
        o->cap = cap;
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
    o->p[o->n] = '\0';
}

void mj_jout_cstr(mj_jout *o, const char *s) { mj_jout_raw(o, s, strlen(s)); }

void mj_jout_string(mj_jout *o, const char *s, size_t n) {
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0, run;
    char esc[16];
    mj_jout_raw(o, "\"", 1);
    while (i < n) {
        unsigned char c = u[i];
        if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
            run = i;
            while (run < n && u[run] >= 0x20 && u[run] < 0x7F && u[run] != '"' && u[run] != '\\') ++run;
            mj_jout_raw(o, s + i, run - i);
            i = run;
            continue;
        }
        if (c == '"') mj_jout_raw(o, "\\\"", 2);
        else if (c == '\\') mj_jout_raw(o, "\\\\", 2);
        else if (c == '\n') mj_jout_raw(o, "\\n", 2);
        else if (c == '\r') mj_jout_raw(o, "\\r", 2);
        else if (c == '\t') mj_jout_raw(o, "\\t", 2);
        else if (c == '\b') mj_jout_raw(o, "\\b", 2);
        else if (c == '\f') mj_jout_raw(o, "\\f", 2);
        else if (c < 0x80) {
            snprintf(esc, sizeof esc, "\\u%04x", c);
            mj_jout_raw(o, esc, 6);
        } else {
            uint32_t cp;
            size_t l = mj_utf8_len(u + i, n - i, &cp);
            if (l == 0) { cp = 0xFFFD; l = 1; } /* not reached for parsed text */
            if (cp >= 0x10000) {
                uint32_t v = cp - 0x10000;
                snprintf(esc, sizeof esc, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
                mj_jout_raw(o, esc, 12);
            } else {
                snprintf(esc, sizeof esc, "\\u%04x", cp);
                mj_jout_raw(o, esc, 6);
            }
            i += l;
            continue;
        }
        ++i;
    }
    mj_jout_raw(o, "\"", 1);
}

void mj_jout_int(mj_jout *o, int64_t v) {
    char b[32];
    int k = snprintf(b, sizeof b, "%lld", (long long)v);
    mj_jout_raw(o, b, (size_t)k);
}

void mj_jout_float(mj_jout *o, double v) {
    char b[32];
    size_t k;
    if (isinf(v)) { mj_jout_cstr(o, v > 0 ? "Infinity" : "-Infinity"); return; }
    if (isnan(v)) { mj_jout_cstr(o, "NaN"); return; }
    k = mj_float_repr(v, b);
    mj_jout_raw(o, b, k);
}

static int key_cmp(const mj_json *d, uint32_t a, uint32_t b) {
    uint32_t la = d->v[a].key_len, lb = d->v[b].key_len;
    int c = memcmp(d->t + d->v[a].key_off, d->t + d->v[b].key_off, la < lb ? la : lb);
    if (c) return c;
    return la < lb ? -1 : la > lb;
}

void mj_jout_value(mj_jout *o, const mj_json *d, uint32_t v) {
    const mj_jnode *x;
    if (v == MJ_JSON_NONE) { mj_jout_cstr(o, "null"); return; }
    x = &d->v[v];
    switch (x->kind) {
    case MJ_J_NULL: mj_jout_cstr(o, "null"); break;
    case MJ_J_TRUE: mj_jout_cstr(o, "true"); break;
    case MJ_J_FALSE: mj_jout_cstr(o, "false"); break;
    case MJ_J_INT: mj_jout_int(o, x->i); break;
    case MJ_J_BIGINT: mj_jout_raw(o, d->t + x->s_off, x->s_len); break;
    case MJ_J_FLOAT: mj_jout_float(o, x->f); break;
    case MJ_J_STR: mj_jout_string(o, d->t + x->s_off, x->s_len); break;
    case MJ_J_ARR: {
        uint32_t c;
        int first = 1;
        mj_jout_raw(o, "[", 1);
        for (c = x->first; c != MJ_JSON_NONE; c = d->v[c].next) {
            if (!first) mj_jout_raw(o, ",", 1);
            first = 0;
            mj_jout_value(o, d, c);
        }
        mj_jout_raw(o, "]", 1);
        break;
    }
    case MJ_J_OBJ: {
        /* unique keys (last value wins), written in sorted order; a simple
         * selection pass keeps this allocation-free */
        uint32_t prev = MJ_JSON_NONE, c, best;
        int first = 1;
        mj_jout_raw(o, "{", 1);
        for (;;) {
            best = MJ_JSON_NONE;
            for (c = x->first; c != MJ_JSON_NONE; c = d->v[c].next) {
                if (get_n(d, v, d->t + d->v[c].key_off, d->v[c].key_len) != c) continue;
                if (prev != MJ_JSON_NONE && key_cmp(d, c, prev) <= 0) continue;
                if (best == MJ_JSON_NONE || key_cmp(d, c, best) < 0) best = c;
            }
            if (best == MJ_JSON_NONE) break;
            if (!first) mj_jout_raw(o, ",", 1);
            first = 0;
            mj_jout_string(o, d->t + d->v[best].key_off, d->v[best].key_len);
            mj_jout_raw(o, ":", 1);
            mj_jout_value(o, d, best);
            prev = best;
        }
        mj_jout_raw(o, "}", 1);
        break;
    }
    default: break;
    }
}
