/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_safet.h"
#include "mjolnir/mj_file.h"

#include <stdlib.h>
#include <string.h>

static const struct { const char *name; mj_st_dtype d; size_t size; } DTYPES[] = {
    {"BF16", MJ_ST_BF16, 2}, {"F16", MJ_ST_F16, 2}, {"F32", MJ_ST_F32, 4}, {"I64", MJ_ST_I64, 8},
    {"I32", MJ_ST_I32, 4},   {"U8", MJ_ST_U8, 1},   {"I8", MJ_ST_I8, 1},   {"BOOL", MJ_ST_BOOL, 1}};
#define N_DTYPES (sizeof DTYPES / sizeof DTYPES[0])

size_t mj_st_dtype_size(mj_st_dtype d) {
    size_t i;
    for (i = 0; i < N_DTYPES; ++i) if (DTYPES[i].d == d) return DTYPES[i].size;
    return 0;
}

const char *mj_st_dtype_name(mj_st_dtype d) {
    size_t i;
    for (i = 0; i < N_DTYPES; ++i) if (DTYPES[i].d == d) return DTYPES[i].name;
    return "?";
}

typedef struct parser {
    const char *s;
    size_t n, i;
} parser;

static void ws(parser *p) {
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')) ++p->i;
}

static int peek(parser *p) { return p->i < p->n ? (unsigned char)p->s[p->i] : -1; }

static int hex4(parser *p, uint32_t *v) {
    uint32_t x = 0;
    int k;
    if (p->n - p->i < 4) return 0;
    for (k = 0; k < 4; ++k) {
        char c = p->s[p->i++];
        x <<= 4;
        if (c >= '0' && c <= '9') x |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') x |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') x |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *v = x;
    return 1;
}

/* Append code point cp as UTF-8 (out may be NULL: validate only). */
static int put_cp(char *out, size_t cap, size_t *len, uint32_t cp) {
    uint8_t b[4];
    size_t k, m;
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    if (cp < 0x80) { b[0] = (uint8_t)cp; m = 1; }
    else if (cp < 0x800) { b[0] = (uint8_t)(0xC0 | (cp >> 6)); b[1] = (uint8_t)(0x80 | (cp & 63)); m = 2; }
    else if (cp < 0x10000) {
        b[0] = (uint8_t)(0xE0 | (cp >> 12)); b[1] = (uint8_t)(0x80 | ((cp >> 6) & 63)); b[2] = (uint8_t)(0x80 | (cp & 63)); m = 3;
    } else {
        b[0] = (uint8_t)(0xF0 | (cp >> 18)); b[1] = (uint8_t)(0x80 | ((cp >> 12) & 63));
        b[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); b[3] = (uint8_t)(0x80 | (cp & 63)); m = 4;
    }
    if (out) {
        if (*len + m + 1 > cap) return 0;
        for (k = 0; k < m; ++k) out[*len + k] = (char)b[k];
    }
    *len += m;
    return 1;
}

/* One raw UTF-8 sequence starting at p->i (already known to be >= 0x80). */
static int utf8_seq(parser *p, uint32_t *cp) {
    const uint8_t *u = (const uint8_t *)p->s + p->i;
    size_t left = p->n - p->i, m, k;
    uint32_t c, minv;
    if (u[0] >= 0xC2 && u[0] <= 0xDF) { m = 2; c = u[0] & 0x1Fu; minv = 0x80; }
    else if (u[0] >= 0xE0 && u[0] <= 0xEF) { m = 3; c = u[0] & 0x0Fu; minv = 0x800; }
    else if (u[0] >= 0xF0 && u[0] <= 0xF4) { m = 4; c = u[0] & 0x07u; minv = 0x10000; }
    else return 0;
    if (left < m) return 0;
    for (k = 1; k < m; ++k) {
        if ((u[k] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (u[k] & 0x3Fu);
    }
    if (c < minv || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    p->i += m;
    *cp = c;
    return 1;
}

/* A JSON string, decoded to UTF-8 into out (NUL-terminated) unless out is
 * NULL. NUL characters are refused: names are C strings here. */
static int string(parser *p, char *out, size_t cap, size_t *len) {
    *len = 0;
    if (peek(p) != '"') return 0;
    ++p->i;
    for (;;) {
        int c = peek(p);
        uint32_t cp;
        if (c < 0) return 0;
        if (c == '"') { ++p->i; break; }
        if (c < 0x20) return 0;
        if (c == '\\') {
            ++p->i;
            c = peek(p);
            ++p->i;
            switch (c) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = 8; break;
            case 'f': cp = 12; break;
            case 'n': cp = 10; break;
            case 'r': cp = 13; break;
            case 't': cp = 9; break;
            case 'u':
                if (!hex4(p, &cp)) return 0;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t lo;
                    if (p->n - p->i < 2 || p->s[p->i] != '\\' || p->s[p->i + 1] != 'u') return 0;
                    p->i += 2;
                    if (!hex4(p, &lo) || lo < 0xDC00 || lo > 0xDFFF) return 0;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                break;
            default: return 0;
            }
        } else if (c >= 0x80) {
            if (!utf8_seq(p, &cp)) return 0;
        } else {
            cp = (uint32_t)c;
            ++p->i;
        }
        if (!put_cp(out, cap, len, cp)) return 0;
    }
    if (out) {
        if (*len + 1 > cap) return 0;
        out[*len] = '\0';
    }
    return 1;
}

/* A whole non-negative integer with no sign, fraction or exponent. */
static int uint_value(parser *p, uint64_t *v) {
    uint64_t x = 0;
    size_t start = p->i;
    while (p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9') {
        uint64_t d = (uint64_t)(p->s[p->i] - '0');
        if (x > (UINT64_MAX - d) / 10u) return 0;
        x = x * 10u + d;
        ++p->i;
    }
    if (p->i == start) return 0;
    if (p->i - start > 1 && p->s[start] == '0') return 0; /* no leading zeros */
    if (p->i < p->n && (p->s[p->i] == '.' || p->s[p->i] == 'e' || p->s[p->i] == 'E')) return 0;
    *v = x;
    return 1;
}

static int literal(parser *p, const char *w) {
    size_t l = strlen(w);
    if (p->n - p->i < l || memcmp(p->s + p->i, w, l) != 0) return 0;
    p->i += l;
    return 1;
}

static int skip_number(parser *p) {
    size_t s;
    if (peek(p) == '-') ++p->i;
    if (peek(p) == '0') ++p->i;
    else {
        s = p->i;
        while (peek(p) >= '0' && peek(p) <= '9') ++p->i;
        if (s == p->i) return 0;
    }
    if (peek(p) == '.') {
        ++p->i;
        s = p->i;
        while (peek(p) >= '0' && peek(p) <= '9') ++p->i;
        if (s == p->i) return 0;
    }
    if (peek(p) == 'e' || peek(p) == 'E') {
        ++p->i;
        if (peek(p) == '+' || peek(p) == '-') ++p->i;
        s = p->i;
        while (peek(p) >= '0' && peek(p) <= '9') ++p->i;
        if (s == p->i) return 0;
    }
    return 1;
}

static int skip_value(parser *p, unsigned depth) {
    size_t len;
    int c;
    if (depth > MJ_ST_DEPTH_MAX) return 0;
    ws(p);
    c = peek(p);
    if (c == '"') return string(p, NULL, 0, &len);
    if (c == '{' || c == '[') {
        int close = c == '{' ? '}' : ']';
        ++p->i;
        ws(p);
        if (peek(p) == close) { ++p->i; return 1; }
        for (;;) {
            ws(p);
            if (c == '{') {
                if (!string(p, NULL, 0, &len)) return 0;
                ws(p);
                if (peek(p) != ':') return 0;
                ++p->i;
            }
            if (!skip_value(p, depth + 1)) return 0;
            ws(p);
            if (peek(p) == ',') { ++p->i; continue; }
            if (peek(p) == close) { ++p->i; return 1; }
            return 0;
        }
    }
    if (c == 't') return literal(p, "true");
    if (c == 'f') return literal(p, "false");
    if (c == 'n') return literal(p, "null");
    return skip_number(p);
}

static int expect(parser *p, char c) {
    ws(p);
    if (peek(p) != (unsigned char)c) return 0;
    ++p->i;
    return 1;
}

/* {"dtype": "...", "shape": [...], "data_offsets": [b, e]}, each key once,
 * nothing else. */
static int tensor_obj(parser *p, mj_st_tensor *t) {
    int have_dtype = 0, have_shape = 0, have_off = 0;
    if (!expect(p, '{')) return 0;
    for (;;) {
        char key[16], val[8];
        size_t len;
        ws(p);
        if (!string(p, key, sizeof key, &len)) return 0;
        if (!expect(p, ':')) return 0;
        ws(p);
        if (strcmp(key, "dtype") == 0 && !have_dtype) {
            size_t k;
            if (!string(p, val, sizeof val, &len)) return 0;
            for (k = 0; k < N_DTYPES; ++k) if (strcmp(val, DTYPES[k].name) == 0) break;
            if (k == N_DTYPES) return 0;
            t->dtype = DTYPES[k].d;
            have_dtype = 1;
        } else if (strcmp(key, "shape") == 0 && !have_shape) {
            if (!expect(p, '[')) return 0;
            ws(p);
            t->ndim = 0;
            if (peek(p) == ']') ++p->i;
            else {
                for (;;) {
                    ws(p);
                    if (t->ndim >= MJ_ST_DIMS_MAX || !uint_value(p, &t->shape[t->ndim])) return 0;
                    ++t->ndim;
                    ws(p);
                    if (peek(p) == ',') { ++p->i; continue; }
                    if (peek(p) == ']') { ++p->i; break; }
                    return 0;
                }
            }
            have_shape = 1;
        } else if (strcmp(key, "data_offsets") == 0 && !have_off) {
            if (!expect(p, '[')) return 0;
            ws(p);
            if (!uint_value(p, &t->begin) || !expect(p, ',')) return 0;
            ws(p);
            if (!uint_value(p, &t->end) || !expect(p, ']')) return 0;
            have_off = 1;
        } else {
            return 0; /* unknown or repeated key */
        }
        ws(p);
        if (peek(p) == ',') { ++p->i; continue; }
        if (peek(p) == '}') { ++p->i; break; }
        return 0;
    }
    return have_dtype && have_shape && have_off;
}

static int by_name(const void *a, const void *b) {
    return strcmp(((const mj_st_tensor *)a)->name, ((const mj_st_tensor *)b)->name);
}

static int by_offset(const void *a, const void *b) {
    const mj_st_tensor *x = *(const mj_st_tensor *const *)a, *y = *(const mj_st_tensor *const *)b;
    if (x->begin != y->begin) return x->begin < y->begin ? -1 : 1;
    if (x->end != y->end) return x->end < y->end ? -1 : 1;
    return 0;
}

void mj_st_free(mj_st_header *h) {
    if (h == NULL || h->budget == NULL) return;
    if (h->t) mj_bfree(h->budget, h->t, h->cap * sizeof *h->t);
    if (h->names) mj_bfree(h->budget, h->names, h->names_len);
    h->t = NULL;
    h->names = NULL;
    h->count = h->cap = 0;
}

static mj_status grow(mj_st_header *h) {
    size_t ncap = h->cap ? h->cap * 2 : 64;
    mj_st_tensor *nt;
    if (h->count >= MJ_ST_TENSORS_MAX) return MJ_E_LIMIT;
    if (ncap > MJ_ST_TENSORS_MAX) ncap = MJ_ST_TENSORS_MAX;
    nt = (mj_st_tensor *)mj_balloc(h->budget, ncap * sizeof *nt);
    if (nt == NULL) return MJ_E_LIMIT;
    if (h->t) {
        memcpy(nt, h->t, h->count * sizeof *nt);
        mj_bfree(h->budget, h->t, h->cap * sizeof *h->t);
    }
    h->t = nt;
    h->cap = ncap;
    return MJ_OK;
}

static mj_status check_tensors(mj_st_header *h) {
    uint64_t data_size = h->file_size - h->data_start, max_end = 0;
    const mj_st_tensor **order;
    size_t i;
    for (i = 0; i < h->count; ++i) {
        mj_st_tensor *t = &h->t[i];
        uint64_t c = 1, sz = mj_st_dtype_size(t->dtype);
        uint32_t d;
        for (d = 0; d < t->ndim; ++d) {
            if (t->shape[d] != 0 && c > UINT64_MAX / t->shape[d]) return MJ_E_CORRUPT;
            c *= t->shape[d];
        }
        if (c > UINT64_MAX / sz) return MJ_E_CORRUPT;
        if (t->begin > t->end || t->end > data_size || t->end - t->begin != c * sz) return MJ_E_CORRUPT;
        t->count = c;
        t->nbytes = t->end - t->begin;
        t->abs_offset = h->data_start + t->begin;
    }
    if (h->count == 0) return MJ_OK;
    qsort(h->t, h->count, sizeof *h->t, by_name);
    for (i = 1; i < h->count; ++i)
        if (strcmp(h->t[i - 1].name, h->t[i].name) == 0) return MJ_E_CORRUPT; /* a name twice */
    order = (const mj_st_tensor **)mj_balloc(h->budget, h->count * sizeof *order);
    if (order == NULL) return MJ_E_LIMIT;
    for (i = 0; i < h->count; ++i) order[i] = &h->t[i];
    qsort(order, h->count, sizeof *order, by_offset);
    for (i = 0; i < h->count; ++i) {
        if (order[i]->nbytes == 0) continue;
        if (order[i]->begin < max_end) {
            mj_bfree(h->budget, order, h->count * sizeof *order);
            return MJ_E_CORRUPT; /* two tensors share bytes */
        }
        max_end = order[i]->end;
    }
    mj_bfree(h->budget, order, h->count * sizeof *order);
    return MJ_OK;
}

mj_status mj_st_parse(const char *json, size_t header_len, uint64_t file_size, mj_budget *b, mj_st_header *out) {
    parser p;
    size_t used = 0;
    int seen_meta = 0;
    mj_status st = MJ_E_CORRUPT;
    memset(out, 0, sizeof *out);
    if (json == NULL || b == NULL) return MJ_E_INVAL;
    if (header_len < 2 || header_len > MJ_ST_HEADER_MAX) return MJ_E_CORRUPT;
    if (file_size < 8u || file_size - 8u < header_len) return MJ_E_CORRUPT;
    out->budget = b;
    out->header_len = header_len;
    out->data_start = 8u + header_len;
    out->file_size = file_size;
    out->names_len = header_len + 1;
    out->names = (char *)mj_balloc(b, out->names_len);
    if (out->names == NULL) { out->budget = NULL; return MJ_E_LIMIT; }
    p.s = json;
    p.n = header_len;
    p.i = 0;
    if (!expect(&p, '{')) goto fail;
    ws(&p);
    if (peek(&p) == '}') ++p.i;
    else {
        for (;;) {
            size_t len;
            char *name = out->names + used;
            ws(&p);
            if (!string(&p, name, out->names_len - used, &len)) goto fail;
            if (!expect(&p, ':')) goto fail;
            if (strcmp(name, "__metadata__") == 0) {
                if (seen_meta) goto fail;
                seen_meta = 1;
                ws(&p);
                if (peek(&p) != '{' || !skip_value(&p, 0)) goto fail;
            } else {
                mj_st_tensor *t;
                if (out->count == out->cap && (st = grow(out)) != MJ_OK) goto fail;
                st = MJ_E_CORRUPT;
                t = &out->t[out->count];
                memset(t, 0, sizeof *t);
                t->name = name;
                if (!tensor_obj(&p, t)) goto fail;
                ++out->count;
                used += len + 1;
            }
            ws(&p);
            if (peek(&p) == ',') { ++p.i; continue; }
            if (peek(&p) == '}') { ++p.i; break; }
            goto fail;
        }
    }
    ws(&p);
    if (p.i != p.n) goto fail; /* only padding may follow */
    if ((st = check_tensors(out)) != MJ_OK) goto fail;
    return MJ_OK;
fail:
    mj_st_free(out);
    out->budget = NULL;
    return st == MJ_OK ? MJ_E_CORRUPT : st;
}

mj_status mj_st_read_header(const char *path, mj_budget *b, mj_st_header *out) {
    mj_file f;
    uint8_t len8[8];
    uint64_t size, n = 0;
    char *buf;
    int k;
    mj_status st;
    memset(out, 0, sizeof *out);
    if ((st = mj_file_open(&f, path, 0)) != MJ_OK) return st;
    if ((st = mj_file_size(&f, &size)) != MJ_OK) goto done;
    if (size < 8) { st = MJ_E_CORRUPT; goto done; }
    if ((st = mj_file_pread(&f, 0, len8, 8)) != MJ_OK) goto done;
    for (k = 7; k >= 0; --k) n = (n << 8) | len8[k];
    if (n < 2 || n > MJ_ST_HEADER_MAX || n > size - 8) { st = MJ_E_CORRUPT; goto done; }
    buf = (char *)mj_balloc(b, (size_t)n);
    if (buf == NULL) { st = MJ_E_LIMIT; goto done; }
    st = mj_file_pread(&f, 8, buf, (size_t)n);
    if (st == MJ_OK) st = mj_st_parse(buf, (size_t)n, size, b, out);
    mj_bfree(b, buf, (size_t)n);
done:
    mj_file_close(&f);
    return st;
}

const mj_st_tensor *mj_st_find(const mj_st_header *h, const char *name) {
    size_t lo = 0, hi = h->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(h->t[mid].name, name);
        if (c == 0) return &h->t[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}
