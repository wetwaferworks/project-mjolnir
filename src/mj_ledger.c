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

/* ---- torn tails ---- */

mj_status mj_ledger_check_tail(const char *buf, size_t n, mj_ledger_state *st, uint64_t *bad_line, size_t *good_len,
                               int *torn) {
    size_t g = n;
    mj_status s;
    if (buf == NULL && n != 0) return MJ_E_INVAL;
    while (g > 0 && buf[g - 1] != '\n') --g;
    if (good_len) *good_len = g;
    if (torn) *torn = g < n;
    s = mj_ledger_verify_buf(buf, g, st, bad_line);
    return s;
}

/* ---- the writer token ---- */

static mj_status token_path_for(const char *ledger, char *out, size_t cap) {
    int w;
    if (ledger == NULL || ledger[0] == '\0') return MJ_E_INVAL;
    w = snprintf(out, cap, "%s.writer", ledger);
    return (w > 0 && (size_t)w < cap && (size_t)w < MJ_PATH_MAX) ? MJ_OK : MJ_E_LIMIT;
}

/* MJW1|pid|start|host|since|nonce */
static int parse_token(const char *t, size_t n, mj_ledger_holder *h) {
    const char *f[6];
    size_t fl[6], i, k = 0, st = 0;
    memset(h, 0, sizeof *h);
    if (n == 0 || t[n - 1] != '\n') return 0;
    --n;
    for (i = 0; i <= n; ++i) {
        if (i == n || t[i] == '|') {
            if (k >= 6) return 0;
            f[k] = t + st;
            fl[k] = i - st;
            ++k;
            st = i + 1;
        } else if ((unsigned char)t[i] < 0x21 || (unsigned char)t[i] > 0x7E) {
            return 0;
        }
    }
    if (k != 6 || fl[0] != 4 || memcmp(f[0], "MJW1", 4) != 0) return 0;
    if (!mj_parse_u64(f[1], fl[1], &h->pid) || !mj_parse_u64(f[2], fl[2], &h->start)) return 0;
    if (fl[3] == 0 || fl[3] >= sizeof h->host || fl[4] >= sizeof h->since || fl[5] != 32) return 0;
    memcpy(h->host, f[3], fl[3]);
    memcpy(h->since, f[4], fl[4]);
    memcpy(h->nonce, f[5], 32);
    if (!when_valid(h->since)) return 0;
    for (i = 0; i < 32; ++i) {
        char c = h->nonce[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static void holder_liveness(mj_ledger_holder *h) {
    char me[64];
    mj_host_name(me, sizeof me);
    h->same_host = strcmp(me, h->host) == 0;
    h->running = h->same_host ? mj_proc_running(h->pid, h->start) : -1;
}

static mj_status read_token(const char *tp, char *buf, size_t cap, size_t *len) {
    mj_budget tb;
    uint8_t *d;
    size_t n;
    mj_status s;
    mj_budget_init(&tb, MJ_LEDGER_TOKEN_MAX + 64u);
    s = mj_fs_read_all(tp, MJ_LEDGER_TOKEN_MAX, &tb, &d, &n);
    if (s == MJ_E_LIMIT) return MJ_E_CORRUPT;
    if (s != MJ_OK) return s;
    if (n >= cap) { mj_bfree(&tb, d, n ? n : 1); return MJ_E_CORRUPT; }
    memcpy(buf, d, n);
    buf[n] = '\0';
    *len = n;
    mj_bfree(&tb, d, n ? n : 1);
    return MJ_OK;
}

mj_status mj_ledger_writer_holder(const char *ledger, mj_ledger_holder *h) {
    char tp[MJ_PATH_MAX], t[MJ_LEDGER_TOKEN_MAX + 1];
    size_t n = 0;
    mj_status s;
    mj_ledger_holder tmp;
    if (h == NULL) h = &tmp;
    memset(h, 0, sizeof *h);
    if ((s = token_path_for(ledger, tp, sizeof tp)) != MJ_OK) return s;
    s = read_token(tp, t, sizeof t, &n);
    if (s == MJ_E_NOTFOUND) return s;
    if (s != MJ_OK) { h->running = -1; return s == MJ_E_IO ? MJ_E_IO : MJ_E_CORRUPT; }
    if (!parse_token(t, n, h)) { memset(h, 0, sizeof *h); h->running = -1; return MJ_E_CORRUPT; }
    holder_liveness(h);
    return MJ_OK;
}

void mj_ledger_holder_describe(const mj_ledger_holder *h, char *out, size_t cap) {
    const char *state = !h->same_host ? "on another host, cannot check here"
                        : h->running == 1 ? "still running"
                        : h->running == 0 ? "not running any more"
                                          : "may still be running";
    if (h->host[0] == '\0') snprintf(out, cap, "the writer token cannot be read; treating the ledger as held");
    else
        snprintf(out, cap, "process %llu on %s since %s (%s)", (unsigned long long)h->pid, h->host, h->since, state);
}

mj_status mj_ledger_writer_claim(const char *ledger, mj_ledger_writer *w, mj_ledger_holder *holder) {
    uint64_t pid, start, tick;
    uint8_t seed[48], dig[MJ_SHA256_LEN];
    char host[64], when[21], nonce[MJ_SHA256_HEX];
    uint64_t c;
    size_t ll;
    int n;
    mj_status s;
    if (w == NULL || ledger == NULL) return MJ_E_INVAL;
    memset(w, 0, sizeof *w);
    if (holder) memset(holder, 0, sizeof *holder);
    ll = strlen(ledger);
    if (ll == 0 || ll >= sizeof w->ledger) return MJ_E_LIMIT;
    memcpy(w->ledger, ledger, ll + 1);
    if ((s = token_path_for(ledger, w->token_path, sizeof w->token_path)) != MJ_OK) return s;
    mj_proc_self(&pid, &start);
    mj_host_name(host, sizeof host);
    if ((s = mj_utc_now(when)) != MJ_OK) return s;
    /* the nonce only has to tell our token from anyone else's */
    tick = mj_clock_ms();
    { volatile int here = 0; c = (uint64_t)(uintptr_t)&here; (void)here; } /* differs between threads */
    memset(seed, 0, sizeof seed);
    memcpy(seed, &pid, 8);
    memcpy(seed + 8, &start, 8);
    memcpy(seed + 16, &tick, 8);
    memcpy(seed + 24, &c, 8);
    {
        const void *addr = (const void *)w;
        memcpy(seed + 32, &addr, sizeof addr < 16 ? sizeof addr : 16);
    }
    mj_sha256(seed, sizeof seed, dig);
    mj_hex_encode(dig, 16, nonce);
    n = snprintf(w->token, sizeof w->token, "MJW1|%llu|%llu|%s|%s|%s\n", (unsigned long long)pid,
                 (unsigned long long)start, host, when, nonce);
    if (n <= 0 || (size_t)n >= sizeof w->token) return MJ_E_LIMIT;
    w->token_len = (size_t)n;
    s = mj_fs_create_exclusive(w->token_path, w->token, w->token_len);
    if (s == MJ_E_EXISTS) {
        mj_ledger_holder h;
        mj_status hs = mj_ledger_writer_holder(ledger, &h);
        if (hs == MJ_E_NOTFOUND) memset(&h, 0, sizeof h), h.running = -1; /* it went away just now: still refuse, never race */
        if (holder) *holder = h;
        return MJ_E_BUSY;
    }
    if (s != MJ_OK) return s;
    w->held = 1;
    return MJ_OK;
}

mj_status mj_ledger_writer_release(mj_ledger_writer *w) {
    char t[MJ_LEDGER_TOKEN_MAX + 1];
    size_t n = 0;
    mj_status s;
    if (w == NULL || !w->held) return MJ_E_INVAL;
    s = read_token(w->token_path, t, sizeof t, &n);
    if (s != MJ_OK || n != w->token_len || memcmp(t, w->token, n) != 0) {
        w->held = 0;
        return MJ_E_FORBIDDEN; /* not ours any more: leave it alone */
    }
    /* Rename first, then delete: the token's name is free the moment the rename lands, even if a reader still has
     * the file open (on Windows a plain delete would leave the name taken until the last reader closes it). */
    {
        char gone[MJ_PATH_MAX];
        const char *nonce = w->token + w->token_len - 33; /* the 32 hex digits before the newline */
        int k = snprintf(gone, sizeof gone, "%s.released-%.32s", w->token_path, nonce);
        if (k <= 0 || (size_t)k >= sizeof gone || (size_t)k >= MJ_PATH_MAX) return MJ_E_LIMIT;
        s = mj_fs_rename_noreplace(w->token_path, gone);
        if (s != MJ_OK) return s;
        w->held = 0;
        (void)mj_fs_remove(gone); /* best effort; the name that matters is already free */
        return MJ_OK;
    }
}

mj_status mj_ledger_writer_release_stale(const char *ledger, mj_ledger_holder *h, char *moved_to, size_t moved_cap) {
    char tp[MJ_PATH_MAX], t[MJ_LEDGER_TOKEN_MAX + 1], t2[MJ_LEDGER_TOKEN_MAX + 1], dst[MJ_PATH_MAX], when[21], stamp[16];
    size_t n = 0, n2 = 0, i, k = 0;
    mj_ledger_holder tmp;
    mj_status s;
    int w;
    if (h == NULL) h = &tmp;
    memset(h, 0, sizeof *h);
    if (moved_to && moved_cap) moved_to[0] = '\0';
    if ((s = token_path_for(ledger, tp, sizeof tp)) != MJ_OK) return s;
    if ((s = read_token(tp, t, sizeof t, &n)) != MJ_OK) return s == MJ_E_NOTFOUND ? MJ_E_NOTFOUND : MJ_E_FORBIDDEN;
    if (!parse_token(t, n, h)) return MJ_E_FORBIDDEN; /* unreadable: a person has to look at it */
    holder_liveness(h);
    if (!h->same_host) return MJ_E_FORBIDDEN;
    if (h->running != 0) return MJ_E_BUSY; /* running, or cannot tell: never take over */
    if ((s = mj_utc_now(when)) != MJ_OK) return s;
    for (i = 0; when[i] && k + 1 < sizeof stamp; ++i)
        if (when[i] >= '0' && when[i] <= '9') stamp[k++] = when[i];
    stamp[k] = '\0';
    w = snprintf(dst, sizeof dst, "%s.stale-%s-%.8s", tp, stamp, h->nonce);
    if (w <= 0 || (size_t)w >= sizeof dst || (size_t)w >= MJ_PATH_MAX) return MJ_E_LIMIT;
    if ((s = mj_fs_rename_noreplace(tp, dst)) != MJ_OK) return s == MJ_E_NOTFOUND ? MJ_E_BUSY : s;
    /* make sure what we moved is the token we judged; if a new writer slipped in between, put theirs back */
    if (read_token(dst, t2, sizeof t2, &n2) != MJ_OK || n2 != n || memcmp(t, t2, n) != 0) {
        (void)mj_fs_rename_noreplace(dst, tp);
        return MJ_E_BUSY;
    }
    if (moved_to && moved_cap) snprintf(moved_to, moved_cap, "%s", dst);
    return MJ_OK;
}

/* ---- appends ---- */

mj_status mj_ledger_append_held(const mj_ledger_writer *w, mj_budget *b, mj_dream_record *r) {
    mj_ledger_state st, after;
    char line[MJ_LEDGER_RECORD_MAX + 2], body[MJ_LEDGER_RECORD_MAX + 2];
    size_t bl, ll;
    uint64_t sz = 0, bad;
    mj_dream_record chk;
    mj_status s, ws;
    const char *path;
    if (w == NULL || r == NULL || !w->held) return MJ_E_INVAL;
    path = w->ledger;
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
    s = mj_fs_size(path, &sz);
    if (s == MJ_E_NOTFOUND) sz = 0;
    else if (s != MJ_OK) return s;
    if (sz + ll > MJ_LEDGER_FILE_MAX) return MJ_E_FULL;
    ws = mj_fs_append_durable(path, line, ll);
    if (ws == MJ_OK) {
        s = mj_ledger_verify_file(path, b, &after, &bad);
        if (s == MJ_OK && after.count == r->seq && memcmp(after.last_hash, r->hash, MJ_SHA256_LEN) == 0) return MJ_OK;
        if (s == MJ_OK) s = MJ_E_CHAIN;
    } else {
        s = ws;
    }
    /* We hold the token and the chain verified at sz bytes, so anything past sz is what this call wrote. Cut it
     * off again rather than leave a torn line that would block every later append. */
    {
        uint64_t now = 0;
        if (mj_fs_size(path, &now) == MJ_OK && now > sz) (void)mj_fs_truncate(path, sz);
    }
    return s;
}

mj_status mj_ledger_append(const char *path, mj_budget *b, mj_dream_record *r) {
    mj_ledger_writer w;
    mj_ledger_holder h;
    mj_status s, rs;
    if (path == NULL || r == NULL) return MJ_E_INVAL;
    s = mj_ledger_writer_claim(path, &w, &h);
    if (s == MJ_E_BUSY) {
        char who[256];
        mj_ledger_holder_describe(&h, who, sizeof who);
        fprintf(stderr,
                "ledger %s: refused, another writer holds it: %s. If that run is gone, clear its token with "
                "mj_ledger_verify --release-stale.\n",
                path, who);
        return s;
    }
    if (s != MJ_OK) return s;
    s = mj_ledger_append_held(&w, b, r);
    rs = mj_ledger_writer_release(&w);
    return s != MJ_OK ? s : rs;
}

/* ---- repair ---- */

static int looks_like_record_start(const char *t, size_t n) {
    static const char head[] = "MJL1|";
    size_t i;
    if (n == 0 || n > MJ_LEDGER_RECORD_MAX + 1u) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)t[i];
        if (c < 0x20 || c > 0x7E) return 0;
        if (i < sizeof head - 1 && t[i] != head[i]) return 0;
    }
    return 1;
}

mj_status mj_ledger_repair_tail(const mj_ledger_writer *w, mj_budget *b, size_t *removed, char *backup,
                                size_t backup_cap) {
    uint8_t *buf = NULL, *again = NULL;
    size_t n = 0, n2 = 0, good = 0;
    int torn = 0, wn;
    uint64_t bad = 0;
    mj_ledger_state st, st2;
    mj_status s;
    char when[21], stamp[16], bak[MJ_PATH_MAX];
    size_t i, k = 0;
    if (removed) *removed = 0;
    if (backup && backup_cap) backup[0] = '\0';
    if (w == NULL || !w->held) return MJ_E_INVAL;
    s = mj_fs_read_all(w->ledger, MJ_LEDGER_FILE_MAX, b, &buf, &n);
    if (s != MJ_OK) return s;
    s = mj_ledger_check_tail((const char *)buf, n, &st, &bad, &good, &torn);
    if (s != MJ_OK) goto out;                         /* a complete line is wrong: that is not a torn write */
    if (!torn) goto out;                              /* nothing to do */
    if (!looks_like_record_start((const char *)buf + good, n - good)) { s = MJ_E_CORRUPT; goto out; }
    if ((s = mj_utc_now(when)) != MJ_OK) goto out;
    for (i = 0; when[i] && k + 1 < sizeof stamp; ++i)
        if (when[i] >= '0' && when[i] <= '9') stamp[k++] = when[i];
    stamp[k] = '\0';
    for (i = 0; i < 100; ++i) { /* never overwrite an older backup: take the next free name */
        wn = i ? snprintf(bak, sizeof bak, "%s.torn-%s-%u.bak", w->ledger, stamp, (unsigned)i)
               : snprintf(bak, sizeof bak, "%s.torn-%s.bak", w->ledger, stamp);
        if (wn <= 0 || (size_t)wn >= sizeof bak || (size_t)wn >= MJ_PATH_MAX) { s = MJ_E_LIMIT; goto out; }
        s = mj_fs_write_atomic(bak, buf, n, 0);
        if (s != MJ_E_EXISTS) break;
    }
    if (s != MJ_OK) goto out;
    if ((s = mj_fs_truncate(w->ledger, (uint64_t)good)) != MJ_OK) goto out;
    /* the result must be exactly the complete lines we verified */
    s = mj_fs_read_all(w->ledger, MJ_LEDGER_FILE_MAX, b, &again, &n2);
    if (s != MJ_OK) goto out;
    if (n2 != good || memcmp(again, buf, good) != 0) { s = MJ_E_CORRUPT; goto out; }
    s = mj_ledger_verify_buf((const char *)again, n2, &st2, &bad);
    if (s == MJ_OK && (st2.count != st.count || memcmp(st2.last_hash, st.last_hash, MJ_SHA256_LEN) != 0)) s = MJ_E_CHAIN;
    if (s == MJ_OK) {
        if (removed) *removed = n - good;
        if (backup && backup_cap) snprintf(backup, backup_cap, "%s", bak);
    }
out:
    if (again) mj_bfree(b, again, n2 ? n2 : 1);
    if (buf) mj_bfree(b, buf, n ? n : 1);
    return s;
}
