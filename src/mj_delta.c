/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_hash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t mj_float_repr(double x, char out[32]) {
    char buf[40], digits[20];
    int prec, exp10 = 0, decpt, nd = 0, neg;
    const char *p;
    size_t o = 0;
    if (!isfinite(x)) return 0;
    for (prec = 1; prec <= 17; ++prec) {
        snprintf(buf, sizeof buf, "%.*e", prec - 1, x);
        if (strtod(buf, NULL) == x) break;
    }
    p = buf;
    neg = (*p == '-');
    if (neg) ++p;
    while (*p && *p != 'e') {
        if (*p >= '0' && *p <= '9' && nd < 19) digits[nd++] = *p;
        ++p;
    }
    if (*p == 'e') exp10 = atoi(p + 1);
    while (nd > 1 && digits[nd - 1] == '0') --nd; /* only 0e+00 can carry one */
    digits[nd] = '\0';
    decpt = exp10 + 1;
    if (neg) out[o++] = '-';
    if (decpt <= -4 || decpt > 16) {
        int e = decpt - 1, k;
        out[o++] = digits[0];
        if (nd > 1) {
            out[o++] = '.';
            for (k = 1; k < nd; ++k) out[o++] = digits[k];
        }
        o += (size_t)snprintf(out + o, 32 - o, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
    } else if (decpt <= 0) {
        int k;
        out[o++] = '0';
        out[o++] = '.';
        for (k = 0; k < -decpt; ++k) out[o++] = '0';
        for (k = 0; k < nd; ++k) out[o++] = digits[k];
    } else if (decpt >= nd) {
        int k;
        for (k = 0; k < nd; ++k) out[o++] = digits[k];
        for (k = nd; k < decpt; ++k) out[o++] = '0';
        out[o++] = '.';
        out[o++] = '0';
    } else {
        int k;
        for (k = 0; k < decpt; ++k) out[o++] = digits[k];
        out[o++] = '.';
        for (k = decpt; k < nd; ++k) out[o++] = digits[k];
    }
    out[o] = '\0';
    return o;
}

static int name_ok(const char *s, size_t n) {
    size_t i;
    if (n < 1 || n > MJ_DELTA_NAME_MAX) return 0;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
            return 0;
    }
    return 1;
}

static int hex64_ok(const char *s, size_t n) {
    size_t i;
    if (n != 64) return 0;
    for (i = 0; i < 64; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

static void put_f32(uint8_t *p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8); p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24);
}

static float get_f32(const uint8_t *p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

mj_status mj_delta_pack(const char *base_hex, const mj_delta_entry *e, uint32_t count, mj_budget *b,
                        uint8_t **out, size_t *n) {
    size_t head = 0, body = 0, total, o;
    uint32_t i;
    char line[400];
    uint8_t *buf;
    *out = NULL;
    *n = 0;
    if (base_hex == NULL || !hex64_ok(base_hex, strlen(base_hex)) || count == 0 || count > MJ_DELTA_MAX_ENTRIES)
        return MJ_E_INVAL;
    head = strlen("MJDELTA1\nbase=\ncount=") + 64 + (size_t)snprintf(line, sizeof line, "%u", count);
    for (i = 0; i < count; ++i) {
        char rep[32];
        if (!name_ok(e[i].name, strlen(e[i].name)) || e[i].rows < 1 || e[i].rows > MJ_DELTA_DIM_MAX ||
            e[i].cols < 1 || e[i].cols > MJ_DELTA_DIM_MAX || e[i].rank < 1 || e[i].rank > MJ_DELTA_MAX_RANK ||
            e[i].U == NULL || e[i].V == NULL || mj_float_repr(e[i].scale, rep) == 0)
            return MJ_E_INVAL;
        head += 1 + (size_t)snprintf(line, sizeof line, "tensor=%s rows=%u cols=%u rank=%u scale=%s", e[i].name,
                                     e[i].rows, e[i].cols, e[i].rank, rep);
        body += 4u * ((size_t)e[i].rows * e[i].rank + (size_t)e[i].rank * e[i].cols);
    }
    if (head > MJ_DELTA_HEAD_MAX) return MJ_E_LIMIT; /* unpack would refuse it */
    head += 5; /* "\nend\n" */
    total = head + body + MJ_SHA256_LEN;
    buf = (uint8_t *)mj_balloc(b, total);
    if (buf == NULL) return MJ_E_LIMIT;
    o = (size_t)sprintf((char *)buf, "MJDELTA1\nbase=%s\ncount=%u", base_hex, count);
    for (i = 0; i < count; ++i) {
        char rep[32];
        (void)mj_float_repr(e[i].scale, rep);
        o += (size_t)sprintf((char *)buf + o, "\ntensor=%s rows=%u cols=%u rank=%u scale=%s", e[i].name, e[i].rows,
                             e[i].cols, e[i].rank, rep);
    }
    memcpy(buf + o, "\nend\n", 5);
    o += 5;
    for (i = 0; i < count; ++i) {
        size_t k, nu = (size_t)e[i].rows * e[i].rank, nv = (size_t)e[i].rank * e[i].cols;
        for (k = 0; k < nu; ++k, o += 4) put_f32(buf + o, e[i].U[k]);
        for (k = 0; k < nv; ++k, o += 4) put_f32(buf + o, e[i].V[k]);
    }
    mj_sha256(buf, o, buf + o);
    *out = buf;
    *n = total;
    return MJ_OK;
}

/* [1-9][0-9]{0,maxd-1} */
static int dec_field(const char *s, size_t n, size_t maxd, uint32_t *v) {
    size_t i;
    uint32_t x = 0;
    if (n < 1 || n > maxd || s[0] < '1' || s[0] > '9') return 0;
    for (i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        x = x * 10u + (uint32_t)(s[i] - '0');
    }
    *v = x;
    return 1;
}

/* What Python's float() accepts from the characters [-+0-9.eE]. Non-finite
 * results (1e999) are refused here; Python would let them through. */
static int scale_field(const char *s, size_t n, double *v) {
    char buf[40];
    size_t i = 0, digits = 0;
    char *end;
    if (n < 1 || n > 32) return 0;
    for (i = 0; i < n; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            return 0;
    i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++digits; }
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++digits; }
    }
    if (digits == 0) return 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        size_t ed = 0;
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++ed; }
        if (ed == 0) return 0;
    }
    if (i != n) return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';
    *v = strtod(buf, &end);
    if (end != buf + n || !isfinite(*v)) return 0;
    return 1;
}

/* tensor=NAME rows=R cols=C rank=K scale=S, exactly. */
static int entry_line(const char *s, size_t n, mj_delta_entry *e) {
    static const char *keys[] = {"tensor=", " rows=", " cols=", " rank=", " scale="};
    const char *f[5];
    size_t fl[5], i, pos = 0;
    for (i = 0; i < 5; ++i) {
        size_t kl = strlen(keys[i]), st;
        if (n - pos < kl || memcmp(s + pos, keys[i], kl) != 0) return 0;
        pos += kl;
        st = pos;
        while (pos < n && s[pos] != ' ') ++pos;
        f[i] = s + st;
        fl[i] = pos - st;
    }
    if (pos != n) return 0;
    if (!name_ok(f[0], fl[0])) return 0;
    if (!dec_field(f[1], fl[1], 6, &e->rows) || !dec_field(f[2], fl[2], 6, &e->cols) ||
        !dec_field(f[3], fl[3], 3, &e->rank) || !scale_field(f[4], fl[4], &e->scale))
        return 0;
    memcpy(e->name, f[0], fl[0]);
    e->name[fl[0]] = '\0';
    return 1;
}

void mj_delta_free(mj_delta *d) {
    if (d && d->budget && d->data) mj_bfree(d->budget, d->data, d->data_n ? d->data_n * sizeof(float) : 1u);
    if (d) {
        d->data = NULL;
        d->budget = NULL;
    }
}

mj_status mj_delta_unpack(const uint8_t *data, size_t n, mj_budget *b, mj_delta *d) {
    uint8_t h[MJ_SHA256_LEN];
    size_t end, i, line, pos, lim, body_end, total = 0;
    const char *s = (const char *)data;
    uint32_t k, count;
    memset(d, 0, sizeof *d);
    if (data == NULL || b == NULL) return MJ_E_INVAL;
    if (n < 64) return MJ_E_CORRUPT;
    mj_sha256(data, n - 32, h);
    if (memcmp(h, data + n - 32, 32) != 0) return MJ_E_CORRUPT;
    /* the first "\nend\n" in the whole buffer must start at or before 64 KiB */
    lim = n - 5 < MJ_DELTA_HEAD_MAX ? n - 5 : MJ_DELTA_HEAD_MAX;
    for (end = 0; end <= lim; ++end)
        if (memcmp(s + end, "\nend\n", 5) == 0) break;
    if (end > lim) return MJ_E_CORRUPT;
    for (i = 0; i < end; ++i)
        if ((unsigned char)s[i] >= 0x80) return MJ_E_CORRUPT;
    /* line 0, 1, 2 */
    if (end < 9 || memcmp(s, "MJDELTA1\n", 9) != 0) return MJ_E_CORRUPT;
    pos = 9;
    if (end - pos < 5 + 64 + 1 || memcmp(s + pos, "base=", 5) != 0 || !hex64_ok(s + pos + 5, 64) ||
        s[pos + 69] != '\n')
        return MJ_E_CORRUPT;
    memcpy(d->base, s + pos + 5, 64);
    d->base[64] = '\0';
    pos += 70;
    if (end - pos < 7 || memcmp(s + pos, "count=", 6) != 0) return MJ_E_CORRUPT;
    pos += 6;
    line = pos;
    while (pos < end && s[pos] != '\n') ++pos;
    if (!dec_field(s + line, pos - line, 3, &count) || count == 0 || count > MJ_DELTA_MAX_ENTRIES) return MJ_E_CORRUPT;
    d->count = count;
    for (k = 0; k < count; ++k) {
        if (pos >= end) return MJ_E_CORRUPT; /* fewer lines than count */
        ++pos;                                /* the '\n' */
        line = pos;
        while (pos < end && s[pos] != '\n') ++pos;
        if (!entry_line(s + line, pos - line, &d->e[k])) return MJ_E_CORRUPT;
    }
    if (pos != end) return MJ_E_CORRUPT; /* more lines than count */
    body_end = n - 32;
    pos = end + 5;
    for (k = 0; k < count; ++k) {
        uint64_t nu = (uint64_t)d->e[k].rows * d->e[k].rank, nv = (uint64_t)d->e[k].rank * d->e[k].cols;
        if (pos > body_end || nu + nv > (uint64_t)(body_end - pos) / 4u) return MJ_E_CORRUPT;
        pos += (size_t)(nu + nv) * 4;
        total += (size_t)(nu + nv);
    }
    if (pos != body_end) return MJ_E_CORRUPT; /* extra bytes */
    d->data_n = total;
    d->data = (float *)mj_balloc(b, total ? total * sizeof(float) : 1u);
    if (d->data == NULL) return MJ_E_LIMIT;
    d->budget = b;
    for (i = 0; i < total; ++i) d->data[i] = get_f32(data + end + 5 + 4 * i);
    total = 0;
    for (k = 0; k < count; ++k) {
        size_t nu = (size_t)d->e[k].rows * d->e[k].rank, nv = (size_t)d->e[k].rank * d->e[k].cols;
        d->e[k].U = d->data + total;
        d->e[k].V = d->data + total + nu;
        total += nu + nv;
    }
    return MJ_OK;
}
