/* Recompute a dream ledger's hash chain from the first line to the last, and
 * look after its writer token and a torn last line.
 *
 *   mj_ledger_verify LEDGER                      check the whole chain
 *   mj_ledger_verify --expect SEQ:HEX LEDGER     also check record SEQ has hash HEX
 *                                                (a head written down earlier)
 *   mj_ledger_verify --who LEDGER                show who holds the writer token
 *   mj_ledger_verify --release-stale LEDGER      clear a token left by a run that is gone
 *   mj_ledger_verify --repair-tail LEDGER        cut a torn last line (backup first)
 *
 * Exit 0 when intact (or the action succeeded), 1 when broken or refused (the
 * reason is printed), 2 on usage or read errors.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"

#include <stdio.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr,
            "usage: mj_ledger_verify [--expect SEQ:HEX] LEDGER\n"
            "       mj_ledger_verify --who LEDGER\n"
            "       mj_ledger_verify --release-stale LEDGER\n"
            "       mj_ledger_verify --repair-tail LEDGER\n");
    return 2;
}

/* Hash of record seq in buf (already verified). */
static int hash_at(const char *buf, size_t n, uint64_t seq, uint8_t out[MJ_SHA256_LEN]) {
    size_t pos = 0;
    while (pos < n) {
        size_t e = pos;
        mj_dream_record r;
        while (e < n && buf[e] != '\n') ++e;
        if (e >= n || mj_ledger_parse_line(buf + pos, e - pos, &r) != MJ_OK) return 0;
        if (r.seq == seq) { memcpy(out, r.hash, MJ_SHA256_LEN); return 1; }
        pos = e + 1;
    }
    return 0;
}

static int cmd_verify(const char *path, const char *expect) {
    mj_budget b;
    mj_ledger_state st;
    uint64_t bad = 0;
    char hex[MJ_SHA256_HEX];
    uint8_t *buf = NULL;
    size_t n = 0, good = 0;
    int torn = 0;
    mj_status s;
    mj_budget_init(&b, 2u * MJ_LEDGER_FILE_MAX + 1024u);
    s = mj_fs_read_all(path, MJ_LEDGER_FILE_MAX, &b, &buf, &n);
    if (s == MJ_E_NOTFOUND) {
        if (expect) { printf("BROKEN: the ledger is missing but a head was expected\n"); return 1; }
        printf("OK 0 records (no ledger yet)\n");
        return 0;
    }
    if (s != MJ_OK) {
        fprintf(stderr, "cannot verify: %s\n", mj_status_name(s));
        return 2;
    }
    s = mj_ledger_verify_buf((const char *)buf, n, &st, &bad);
    if (s == MJ_E_CHAIN) {
        mj_ledger_state ps;
        uint64_t pbad = 0;
        printf("BROKEN at line %llu\n", (unsigned long long)bad);
        if (mj_ledger_check_tail((const char *)buf, n, &ps, &pbad, &good, &torn) == MJ_OK && torn)
            printf("every complete record verifies; only the last line is cut off (%llu bytes). "
                   "mj_ledger_verify --repair-tail can remove it after making a backup.\n",
                   (unsigned long long)(n - good));
        mj_bfree(&b, buf, n ? n : 1);
        return 1;
    }
    if (s != MJ_OK) {
        mj_bfree(&b, buf, n ? n : 1);
        fprintf(stderr, "cannot verify: %s\n", mj_status_name(s));
        return 2;
    }
    if (expect) {
        uint64_t seq = 0;
        uint8_t want[MJ_SHA256_LEN], got[MJ_SHA256_LEN];
        const char *colon = strchr(expect, ':');
        if (colon == NULL || !mj_parse_u64(expect, (size_t)(colon - expect), &seq) || strlen(colon + 1) != 64 ||
            !mj_hex_decode(colon + 1, 64, want)) {
            mj_bfree(&b, buf, n ? n : 1);
            return usage();
        }
        if (seq == 0 || seq > st.count || !hash_at((const char *)buf, n, seq, got)) {
            printf("BROKEN: record %llu is not in the ledger (it has %llu); records were removed\n",
                   (unsigned long long)seq, (unsigned long long)st.count);
            mj_bfree(&b, buf, n ? n : 1);
            return 1;
        }
        if (memcmp(got, want, MJ_SHA256_LEN) != 0) {
            printf("BROKEN: record %llu does not have the expected hash; the history was rewritten\n",
                   (unsigned long long)seq);
            mj_bfree(&b, buf, n ? n : 1);
            return 1;
        }
    }
    mj_bfree(&b, buf, n ? n : 1);
    mj_hex_encode(st.last_hash, MJ_SHA256_LEN, hex);
    printf("OK %llu records, head %s\n", (unsigned long long)st.count, hex);
    if (st.count > 0) printf("last: %s %s %s\n", st.last.dream_id, st.last.when, st.last.outcome);
    if (expect) printf("expected head matches\n");
    return 0;
}

static int cmd_who(const char *path) {
    mj_ledger_holder h;
    char who[256];
    mj_status s = mj_ledger_writer_holder(path, &h);
    if (s == MJ_E_NOTFOUND) { printf("free: no writer holds the ledger\n"); return 0; }
    mj_ledger_holder_describe(&h, who, sizeof who);
    printf("held: %s\n", who);
    return s == MJ_OK ? 0 : 1;
}

static int cmd_release_stale(const char *path) {
    mj_ledger_holder h;
    char who[256], moved[MJ_PATH_MAX];
    mj_status s = mj_ledger_writer_release_stale(path, &h, moved, sizeof moved);
    mj_ledger_holder_describe(&h, who, sizeof who);
    switch (s) {
    case MJ_OK: printf("cleared a token left by %s; kept as %s\n", who, moved); return 0;
    case MJ_E_NOTFOUND: printf("free: no writer token to clear\n"); return 0;
    case MJ_E_BUSY: printf("refused: the writer may still be running (%s); nothing changed\n", who); return 1;
    case MJ_E_FORBIDDEN:
        printf("refused: the token was written on another host or cannot be read (%s). Check that no run is "
               "writing, then look at the .writer file by hand; nothing changed\n", who);
        return 1;
    default: fprintf(stderr, "cannot clear the token: %s\n", mj_status_name(s)); return 2;
    }
}

static int cmd_repair(const char *path) {
    mj_budget b;
    mj_ledger_writer w;
    mj_ledger_holder h;
    char who[256], bak[MJ_PATH_MAX];
    size_t removed = 0;
    mj_status s, rs;
    mj_budget_init(&b, 2u * MJ_LEDGER_FILE_MAX + 1024u);
    s = mj_ledger_writer_claim(path, &w, &h);
    if (s == MJ_E_BUSY) {
        mj_ledger_holder_describe(&h, who, sizeof who);
        printf("refused: another writer holds the ledger (%s); nothing changed\n", who);
        return 1;
    }
    if (s != MJ_OK) { fprintf(stderr, "cannot take the writer token: %s\n", mj_status_name(s)); return 2; }
    s = mj_ledger_repair_tail(&w, &b, &removed, bak, sizeof bak);
    rs = mj_ledger_writer_release(&w);
    switch (s) {
    case MJ_OK:
        if (removed == 0) printf("nothing to repair: the last line is complete\n");
        else printf("cut %llu bytes of a torn last line; the whole file before the cut is in %s\n",
                    (unsigned long long)removed, bak);
        break;
    case MJ_E_CHAIN:
        printf("refused: a complete record does not verify, so this is not a torn write. The history is left "
               "exactly as it is; run mj_ledger_verify to see the first bad line\n");
        return 1;
    case MJ_E_CORRUPT:
        printf("refused: the bytes after the last complete record are not the start of a record; nothing changed\n");
        return 1;
    default: fprintf(stderr, "cannot repair: %s\n", mj_status_name(s)); return 2;
    }
    if (rs != MJ_OK) fprintf(stderr, "warning: could not remove the writer token: %s\n", mj_status_name(rs));
    return cmd_verify(path, NULL);
}

int main(int argc, char **argv) {
    if (argc == 2 && argv[1][0] != '-') return cmd_verify(argv[1], NULL);
    if (argc == 4 && strcmp(argv[1], "--expect") == 0) return cmd_verify(argv[3], argv[2]);
    if (argc == 3 && strcmp(argv[1], "--who") == 0) return cmd_who(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--release-stale") == 0) return cmd_release_stale(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--repair-tail") == 0) return cmd_repair(argv[2]);
    return usage();
}
