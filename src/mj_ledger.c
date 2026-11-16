/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_fs.h"

#include <stdio.h>
#include <string.h>

#define NFIELDS 18

int mj_outcome_valid(const char *s) {
    static const char *ok[] = {"integrated", "dry-run-pass", "rejected", "aborted", "rolled-back", "grade"};
    size_t i;
    if (s == NULL) return 0;
    for (i = 0; i < sizeof ok / sizeof ok[0]; ++i)
        if (strcmp(s, ok[i]) == 0) return 1;
    return 0;
}

static int id_or_dash(const char *s) { return strcmp(s, "-") == 0 || mj_id_valid(s); }

static int when_valid(const char *s) {
    /* YYYY-MM-DDTHH:MM:SSZ, digits checked, ranges loosely checked */
    static const char pat[] = "dddd-dd-ddTdd:dd:ddZ";
    size_t i;
    if (strlen(s) != sizeof pat - 1) return 0;
    for (i = 0; pat[i]; ++i) {
        if (pat[i] == 'd') { if (s[i] < '0' || s[i] > '9') return 0; }
        else if (s[i] != pat[i]) return 0;
    }
    return 1;
}

static int short_valid(const char *s) {
    size_t i;
    if (s[0] == '\0') return 0;
    for (i = 0; s[i]; ++i) {
        char c = s[i];
        if (i >= MJ_SHORT_MAX || !((c >= 'a' && c <= 'z') || c == '-' || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

static int inputs_valid(const char *s) {
    char id[MJ_ID_MAX + 1];
    size_t i = 0, k = 0, count = 0;
    if (strcmp(s, "-") == 0) return 1;
    if (!mj_text_valid(s) || s[0] == '\0') return 0;
    for (;;) {
        char c = s[i];
        if (c == ',' || c == '\0') {
            id[k] = '\0';
            if (!mj_id_valid(id) || ++count > MJ_INPUTS_MAX) return 0;
            k = 0;
            if (c == '\0') return 1;
        } else {
            if (k >= MJ_ID_MAX) return 0;
            id[k++] = c;
        }
        ++i;
    }
}

static int record_fields_valid(const mj_dream_record *r) {
    return mj_id_valid(r->dream_id) && when_valid(r->when) && short_valid(r->trigger) && mj_id_valid(r->grader) &&
           inputs_valid(r->inputs) && id_or_dash(r->weights_before) && id_or_dash(r->weights_candidate) &&
           mj_text_valid(r->edit) && r->score_before_bp <= 10000u && r->score_after_bp <= 10000u &&
           r->shift_bp <= 10000u && mj_text_valid(r->gates) && mj_outcome_valid(r->outcome) && mj_text_valid(r->notes);
}

void mj_ledger_record_init(mj_dream_record *r) {
    memset(r, 0, sizeof *r);
    memcpy(r->when, "1970-01-01T00:00:00Z", 21);
    memcpy(r->trigger, "test", 5);
    memcpy(r->inputs, "-", 2);
    memcpy(r->weights_before, "-", 2);
    memcpy(r->weights_candidate, "-", 2);
    memcpy(r->outcome, "aborted", 8);
}

static size_t format_body(const mj_dream_record *r, char *out, size_t cap) {
    char prev[MJ_SHA256_HEX];
    int n;
    if (!record_fields_valid(r)) return 0;
    mj_hex_encode(r->prev, MJ_SHA256_LEN, prev);
    n = snprintf(out, cap, "MJL1|%llu|%s|%s|%s|%s|%s|%s|%s|%s|%s|%llu|%llu|%llu|%s|%s|%s",
                 (unsigned long long)r->seq, prev, r->dream_id, r->when, r->trigger, r->grader, r->inputs,
                 r->weights_before, r->weights_candidate, r->edit, (unsigned long long)r->score_before_bp,
                 (unsigned long long)r->score_after_bp, (unsigned long long)r->shift_bp, r->gates, r->outcome,
                 r->notes);
    if (n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t mj_ledger_format_line(const mj_dream_record *r, char *out, size_t cap) {
    char hex[MJ_SHA256_HEX];
    size_t n = format_body(r, out, cap);
    if (n == 0 || n + 1 + 64 + 1 + 1 > cap || n + 66 > MJ_LEDGER_RECORD_MAX) return 0;
    mj_hex_encode(r->hash, MJ_SHA256_LEN, hex);
    out[n] = '|';
    memcpy(out + n + 1, hex, 64);
    out[n + 65] = '\n';
    out[n + 66] = '\0';
    return n + 66;
}

static int field_copy(char *dst, size_t cap, const char *s, size_t n) {
    if (n >= cap) return 0;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return 1;
}

mj_status mj_ledger_parse_line(const char *line, size_t n, mj_dream_record *out) {
    const char *f[NFIELDS];
    size_t fl[NFIELDS], i, k = 0, start = 0;
    mj_dream_record r;
    uint8_t h[MJ_SHA256_LEN];
    if (line == NULL || out == NULL || n == 0 || n > MJ_LEDGER_RECORD_MAX) return MJ_E_INVAL;
    for (i = 0; i <= n; ++i) {
        if (i == n || line[i] == '|') {
            if (k >= NFIELDS) return MJ_E_INVAL;
            f[k] = line + start;
            fl[k] = i - start;
            ++k;
            start = i + 1;
        } else {
            unsigned char c = (unsigned char)line[i];
            if (c < 0x20 || c > 0x7E) return MJ_E_INVAL;
        }
    }
    if (k != NFIELDS || fl[0] != 4 || memcmp(f[0], "MJL1", 4) != 0) return MJ_E_INVAL;
    memset(&r, 0, sizeof r);
    if (!mj_parse_u64(f[1], fl[1], &r.seq) || r.seq == 0) return MJ_E_INVAL;
    if (fl[2] != 64 || !mj_hex_decode(f[2], 64, r.prev)) return MJ_E_INVAL;
    if (!field_copy(r.dream_id, sizeof r.dream_id, f[3], fl[3]) || !field_copy(r.when, sizeof r.when, f[4], fl[4]) ||
        !field_copy(r.trigger, sizeof r.trigger, f[5], fl[5]) || !field_copy(r.grader, sizeof r.grader, f[6], fl[6]) ||
        !field_copy(r.inputs, sizeof r.inputs, f[7], fl[7]) ||
        !field_copy(r.weights_before, sizeof r.weights_before, f[8], fl[8]) ||
        !field_copy(r.weights_candidate, sizeof r.weights_candidate, f[9], fl[9]) ||
        !field_copy(r.edit, sizeof r.edit, f[10], fl[10]))
        return MJ_E_INVAL;
    if (!mj_parse_u64(f[11], fl[11], &r.score_before_bp) || !mj_parse_u64(f[12], fl[12], &r.score_after_bp) ||
        !mj_parse_u64(f[13], fl[13], &r.shift_bp))
        return MJ_E_INVAL;
    if (!field_copy(r.gates, sizeof r.gates, f[14], fl[14]) || !field_copy(r.outcome, sizeof r.outcome, f[15], fl[15]) ||
        !field_copy(r.notes, sizeof r.notes, f[16], fl[16]))
        return MJ_E_INVAL;
    if (fl[17] != 64 || !mj_hex_decode(f[17], 64, r.hash)) return MJ_E_INVAL;
    if (!record_fields_valid(&r)) return MJ_E_INVAL;
    /* the hash covers the exact bytes before "|hash" */
    mj_sha256(line, (size_t)(f[17] - line) - 1u, h);
    if (memcmp(h, r.hash, MJ_SHA256_LEN) != 0) return MJ_E_CHAIN;
    {
        /* canonical form only: re-formatting must give the same bytes */
        char again[MJ_LEDGER_RECORD_MAX + 2];
        size_t m = mj_ledger_format_line(&r, again, sizeof again);
        if (m != n + 1 || memcmp(again, line, n) != 0) return MJ_E_INVAL;
    }
    *out = r;
    return MJ_OK;
}

mj_status mj_ledger_verify_buf(const char *buf, size_t n, mj_ledger_state *st, uint64_t *bad_line) {
    size_t pos = 0;
    mj_ledger_state s;
    memset(&s, 0, sizeof s);
    if (bad_line) *bad_line = 0;
    if (buf == NULL && n != 0) return MJ_E_INVAL;
    if (n > MJ_LEDGER_FILE_MAX) return MJ_E_LIMIT;
    while (pos < n) {
        size_t e = pos;
        mj_dream_record r;
        mj_status rs;
        while (e < n && buf[e] != '\n') {
            if (e - pos > MJ_LEDGER_RECORD_MAX) break;
            ++e;
        }
        if (bad_line) *bad_line = s.count + 1;
        if (e >= n || buf[e] != '\n') return MJ_E_CHAIN; /* torn or oversized last line */
        rs = mj_ledger_parse_line(buf + pos, e - pos, &r);
        if (rs != MJ_OK) return MJ_E_CHAIN;
        if (r.seq != s.count + 1 || memcmp(r.prev, s.last_hash, MJ_SHA256_LEN) != 0) return MJ_E_CHAIN;
        s.count = r.seq;
        memcpy(s.last_hash, r.hash, MJ_SHA256_LEN);
        s.last = r;
        pos = e + 1;
    }
    if (bad_line) *bad_line = 0;
    if (st) *st = s;
    return MJ_OK;
}

mj_status mj_ledger_verify_file(const char *path, mj_budget *b, mj_ledger_state *st, uint64_t *bad_line) {
    uint8_t *buf;
    size_t n;
    mj_status s = mj_fs_read_all(path, MJ_LEDGER_FILE_MAX, b, &buf, &n);
    if (s == MJ_E_NOTFOUND) { /* an absent ledger is an empty chain */
        if (st) memset(st, 0, sizeof *st);
        if (bad_line) *bad_line = 0;
        return MJ_OK;
    }
    if (s != MJ_OK) return s;
    s = mj_ledger_verify_buf((const char *)buf, n, st, bad_line);
    mj_bfree(b, buf, n ? n : 1);
    return s;
}

mj_status mj_ledger_append(const char *path, mj_budget *b, mj_dream_record *r) {
    mj_ledger_state st, after;
    char line[MJ_LEDGER_RECORD_MAX + 2], body[MJ_LEDGER_RECORD_MAX + 2];
    size_t bl, ll;
    uint64_t sz = 0, bad;
    mj_dream_record chk;
    mj_status s;
    if (path == NULL || r == NULL) return MJ_E_INVAL;
    s = mj_ledger_verify_file(path, b, &st, &bad);
    if (s != MJ_OK) return s;
    r->seq = st.count + 1;
    memcpy(r->prev, st.last_hash, MJ_SHA256_LEN);
    bl = format_body(r, body, sizeof body);
    if (bl == 0) return MJ_E_INVAL;
    mj_sha256(body, bl, r->hash);
    ll = mj_ledger_format_line(r, line, sizeof line);
    if (ll == 0) return MJ_E_LIMIT;
    if (mj_ledger_parse_line(line, ll - 1, &chk) != MJ_OK) return MJ_E_INVAL;
    if (mj_fs_size(path, &sz) == MJ_OK && sz + ll > MJ_LEDGER_FILE_MAX) return MJ_E_FULL;
    s = mj_fs_append_durable(path, line, ll);
    if (s != MJ_OK) return s;
    s = mj_ledger_verify_file(path, b, &after, &bad);
    if (s != MJ_OK) return s;
    return (after.count == r->seq && memcmp(after.last_hash, r->hash, MJ_SHA256_LEN) == 0) ? MJ_OK : MJ_E_CHAIN;
}
