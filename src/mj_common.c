/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_common.h"

#include <stdlib.h>
#include <string.h>

const char *mj_status_name(mj_status s) {
    switch (s) {
    case MJ_OK: return "ok";
    case MJ_E_INVAL: return "invalid-input";
    case MJ_E_IO: return "io-error";
    case MJ_E_EXISTS: return "refused-exists";
    case MJ_E_NOTFOUND: return "not-found";
    case MJ_E_CORRUPT: return "corrupt";
    case MJ_E_FULL: return "storage-ceiling";
    case MJ_E_LIMIT: return "limit";
    case MJ_E_TIMEOUT: return "time-ceiling";
    case MJ_E_GATE: return "gate-failed";
    case MJ_E_FORBIDDEN: return "forbidden-by-gate";
    case MJ_E_CHAIN: return "chain-broken";
    case MJ_E_BUSY: return "busy-another-writer";
    }
    return "unknown";
}

void mj_budget_init(mj_budget *b, size_t cap) {
    b->cap = cap;
    b->used = 0;
    b->peak = 0;
}

void *mj_balloc(mj_budget *b, size_t n) {
    void *p;
    if (b == NULL || n == 0) return NULL;
    if (n > b->cap || b->used > b->cap - n) return NULL;
    p = calloc(1, n);
    if (p == NULL) return NULL;
    b->used += n;
    if (b->used > b->peak) b->peak = b->used;
    return p;
}

void mj_bfree(mj_budget *b, void *p, size_t n) {
    if (p == NULL) return;
    free(p);
    if (b != NULL) b->used = (n <= b->used) ? b->used - n : 0;
}

static int id_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

int mj_id_valid(const char *s) {
    size_t i;
    if (s == NULL || s[0] == '\0' || s[0] == '.' || s[0] == '-') return 0;
    for (i = 0; s[i] != '\0'; ++i) {
        if (i >= MJ_ID_MAX || !id_char(s[i])) return 0;
    }
    return 1;
}

int mj_text_valid(const char *s) {
    size_t i;
    if (s == NULL) return 0;
    for (i = 0; s[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (i >= MJ_TEXT_MAX || c < 0x20 || c > 0x7E || c == '|') return 0;
    }
    return 1;
}

void mj_hex_encode(const uint8_t *in, size_t n, char *out) {
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; ++i) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = '\0';
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1; /* lowercase only: one canonical spelling per hash */
}

int mj_hex_decode(const char *in, size_t hexlen, uint8_t *out) {
    size_t i;
    if (in == NULL || (hexlen & 1u) != 0) return 0;
    for (i = 0; i < hexlen; i += 2) {
        int hi = hexval(in[i]), lo = hexval(in[i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

int mj_parse_u64(const char *s, size_t len, uint64_t *out) {
    uint64_t v = 0;
    size_t i;
    if (s == NULL || len == 0 || len > 20) return 0;
    if (len > 1 && s[0] == '0') return 0; /* canonical: no leading zeros */
    for (i = 0; i < len; ++i) {
        unsigned d;
        if (s[i] < '0' || s[i] > '9') return 0;
        d = (unsigned)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    *out = v;
    return 1;
}
