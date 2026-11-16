/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mj_test.h"

static mj_budget B;

static void rec(mj_dream_record *r, const char *id, const char *outcome, const char *notes) {
    mj_ledger_record_init(r);
    snprintf(r->dream_id, sizeof r->dream_id, "%s", id);
    snprintf(r->when, sizeof r->when, "2026-11-16T17:42:00Z");
    snprintf(r->trigger, sizeof r->trigger, "test");
    snprintf(r->grader, sizeof r->grader, "auto-maze");
    snprintf(r->inputs, sizeof r->inputs, "replay-1,replay-2");
    snprintf(r->weights_before, sizeof r->weights_before, "toy-genesis");
    snprintf(r->edit, sizeof r->edit, "toy nudge");
    r->score_before_bp = 3333;
    r->score_after_bp = 10000;
    snprintf(r->gates, sizeof r->gates, "step=pass,shift=pass");
    snprintf(r->outcome, sizeof r->outcome, "%s", outcome);
    snprintf(r->notes, sizeof r->notes, "%s", notes);
}

static size_t load(const char *path, char *buf, size_t cap) {
    uint8_t *b;
    size_t n;
    if (mj_fs_read_all(path, cap, &B, &b, &n) != MJ_OK) return 0;
    memcpy(buf, b, n);
    mj_bfree(&B, b, n ? n : 1);
    return n;
}

static void save(const char *path, const char *buf, size_t n) {
    (void)mj_fs_remove(path);
    (void)mj_fs_write_atomic(path, buf, n, 0);
}

int main(void) {
    char path[MJ_PATH_MAX], orig[16384], work[16384];
    size_t n, i, nl[8], k = 0;
    mj_dream_record r;
    mj_ledger_state st;
    uint64_t bad;

    mj_budget_init(&B, 128u << 20);
    mj_t_dir(path, sizeof path, "ledger_test.mjl");
    (void)mj_fs_remove(path);

    CHECK_ST(mj_ledger_verify_file(path, &B, &st, &bad), MJ_OK); /* absent = empty chain */
    CHECK_EQ(st.count, 0u);
    for (i = 0; i < 5; ++i) {
        char id[16];
        snprintf(id, sizeof id, "dream-%u", (unsigned)i);
        rec(&r, id, i == 2 ? "rejected" : "dry-run-pass", i == 2 ? "shift gate: too much drift" : "");
        CHECK_ST(mj_ledger_append(path, &B, &r), MJ_OK);
        CHECK_EQ(r.seq, (uint64_t)i + 1);
    }
    CHECK_ST(mj_ledger_verify_file(path, &B, &st, &bad), MJ_OK);
    CHECK_EQ(st.count, 5u);
    CHECK(strcmp(st.last.dream_id, "dream-4") == 0);

    /* grades go through the same chain */
    rec(&r, "grade-1", "grade", "JD: not productive, wrong turn at pit 2");
    snprintf(r.grader, sizeof r.grader, "JD");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_OK);

    /* invalid fields are refused before anything is written */
    rec(&r, "bad", "dry-run-pass", "pipe | inside");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_INVAL);
    rec(&r, "bad", "made-up-outcome", "");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_INVAL);
    rec(&r, "bad", "dry-run-pass", "");
    r.score_after_bp = 10001;
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_INVAL);
    rec(&r, "bad", "dry-run-pass", "");
    snprintf(r.when, sizeof r.when, "yesterday");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_INVAL);
    rec(&r, "bad", "dry-run-pass", "");
    snprintf(r.inputs, sizeof r.inputs, "ok,../evil");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_INVAL);
    CHECK_ST(mj_ledger_verify_file(path, &B, &st, &bad), MJ_OK);
    CHECK_EQ(st.count, 6u);

    n = load(path, orig, sizeof orig);
    CHECK(n > 0);
    for (i = 0; i < n && k < 8; ++i)
        if (orig[i] == '\n') nl[k++] = i;
    CHECK_EQ(k, 6u);

    /* one changed byte in line 3 breaks the chain at line 3 */
    memcpy(work, orig, n);
    work[nl[1] + 40] ^= 1;
    CHECK_ST(mj_ledger_verify_buf(work, n, &st, &bad), MJ_E_CHAIN);
    CHECK_EQ(bad, 3u);
    /* a changed score (still well-formed) is caught by the line hash */
    memcpy(work, orig, n);
    {
        char *p = strstr(work + nl[0] + 1, "|3333|");
        CHECK(p != NULL);
        if (p) p[1] = '4';
    }
    CHECK_ST(mj_ledger_verify_buf(work, n, &st, &bad), MJ_E_CHAIN);
    CHECK_EQ(bad, 2u);
    /* deleting line 2 breaks the link at the new line 2 */
    memcpy(work, orig, nl[0] + 1);
    memcpy(work + nl[0] + 1, orig + nl[1] + 1, n - nl[1] - 1);
    CHECK_ST(mj_ledger_verify_buf(work, n - (nl[1] - nl[0]), &st, &bad), MJ_E_CHAIN);
    CHECK_EQ(bad, 2u);
    /* swapping lines 1 and 2 */
    {
        size_t l1 = nl[0] + 1, l2 = nl[1] - nl[0];
        memcpy(work, orig + l1, l2);
        memcpy(work + l2, orig, l1);
        memcpy(work + l1 + l2, orig + l1 + l2, n - l1 - l2);
        CHECK_ST(mj_ledger_verify_buf(work, n, &st, &bad), MJ_E_CHAIN);
        CHECK_EQ(bad, 1u);
    }
    /* a torn last line (crash mid-append) */
    CHECK_ST(mj_ledger_verify_buf(orig, n - 10, &st, &bad), MJ_E_CHAIN);
    CHECK_EQ(bad, 6u);
    /* truncating whole lines from the end is detectable only against a
       remembered head, so the verifier reports the head for witnesses */
    CHECK_ST(mj_ledger_verify_buf(orig, nl[3] + 1, &st, &bad), MJ_OK);
    CHECK_EQ(st.count, 4u);

    /* appends are refused while the chain is broken */
    memcpy(work, orig, n);
    work[nl[2] + 20] ^= 1;
    save(path, work, n);
    rec(&r, "after-break", "dry-run-pass", "");
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_E_CHAIN);
    CHECK_EQ(load(path, work, sizeof work), n); /* nothing was appended */
    save(path, orig, n);
    CHECK_ST(mj_ledger_append(path, &B, &r), MJ_OK);
    CHECK_EQ(r.seq, 7u);

    /* parser strictness: non-canonical numbers and stray bytes */
    {
        char line[MJ_LEDGER_RECORD_MAX + 2];
        mj_dream_record x;
        size_t ll = mj_ledger_format_line(&r, line, sizeof line);
        CHECK(ll > 0);
        CHECK_ST(mj_ledger_parse_line(line, ll - 1, &x), MJ_OK);
        line[5] = '0'; /* seq "07" style */
        CHECK(mj_ledger_parse_line(line, ll - 1, &x) != MJ_OK);
        CHECK_ST(mj_ledger_parse_line("", 0, &x), MJ_E_INVAL);
        CHECK_ST(mj_ledger_parse_line("MJL1|1", 6, &x), MJ_E_INVAL);
    }
    CHECK_EQ(B.used, 0u);
    return mj_t_done("test_ledger");
}
