/* The JSON reader, replays from PCHIE run logs, and the grades ledger.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"
#include "mj_runlog.h"

#include "mjolnir/mj_grades.h"
#include "mjolnir/mj_json.h"
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_replay.h"

static mj_budget B;

static int dumps_as(const char *in, const char *want) {
    mj_json d;
    mj_jout o;
    uint32_t r;
    int ok;
    mj_json_init(&d, &B, 1u << 16, 1u << 16);
    if (mj_json_parse(&d, in, strlen(in), &r) != MJ_OK) { mj_json_free(&d); return 0; }
    mj_jout_init(&o, 1u << 20);
    mj_jout_value(&o, &d, r);
    ok = !o.failed && strcmp(o.p, want) == 0;
    if (!ok) fprintf(stderr, "  dump of %s gave %s, wanted %s\n", in, o.p ? o.p : "(none)", want);
    mj_jout_free(&o);
    mj_json_free(&d);
    return ok;
}

static int refuses(const char *in, size_t n) {
    mj_json d;
    uint32_t r;
    mj_status st;
    mj_json_init(&d, &B, 1u << 16, 1u << 16);
    st = mj_json_parse(&d, in, n, &r);
    mj_json_free(&d);
    return st != MJ_OK;
}

static int py_eq(const char *a, const char *b) {
    mj_json d;
    uint32_t x, y;
    int e;
    mj_json_init(&d, &B, 1u << 16, 1u << 16);
    if (mj_json_parse(&d, a, strlen(a), &x) != MJ_OK || mj_json_parse(&d, b, strlen(b), &y) != MJ_OK) { mj_json_free(&d); return -1; }
    e = mj_json_eq(&d, x, &d, y);
    if (e) CHECK(mj_json_hash(&d, x) == mj_json_hash(&d, y) || !mj_json_hashable(&d, x));
    mj_json_free(&d);
    return e;
}

static void test_json(void) {
    char deep[1200];
    int i;
    /* json.dumps(json.loads(x), sort_keys=True, separators=(",", ":")) */
    CHECK(dumps_as("{\"b\":1,\"a\":[true,false,null],\"a\":2}", "{\"a\":2,\"b\":1}"));
    CHECK(dumps_as("\"caf\xc3\xa9 \xf0\x9f\x98\x80 \x7f\"", "\"caf\\u00e9 \\ud83d\\ude00 \\u007f\""));
    CHECK(dumps_as("\"\\ud800x\\udc00\"", "\"\\ud800x\\udc00\""));
    CHECK(dumps_as("\"\\ud83d\\ude00\\/\\b\\u0001\"", "\"\\ud83d\\ude00/\\b\\u0001\""));
    CHECK(dumps_as("[1.0,1e16,-0,-0.0,1E-5,2.5e-308,12345678901234567890,1e400]",
                   "[1.0,1e+16,0,-0.0,1e-05,2.5e-308,12345678901234567890,Infinity]"));
    CHECK(dumps_as(" \t\r\n{ \"k\" : [ ] , \"j\" : { } }\r\n", "{\"j\":{},\"k\":[]}"));
    /* refusals, as json.loads, plus the documented stricter cases */
    CHECK(refuses("", 0));
    CHECK(refuses("{", 1));
    CHECK(refuses("[1,]", 4));
    CHECK(refuses("{\"a\":1,}", 8));
    CHECK(refuses("01", 2));
    CHECK(refuses("1.", 2));
    CHECK(refuses("1e", 2));
    CHECK(refuses("-", 1));
    CHECK(refuses("\"a\x01\"", 4));
    CHECK(refuses("\"\\x\"", 4));
    CHECK(refuses("\"\\u12\"", 6));
    CHECK(refuses("\"\xc3\"", 3));
    CHECK(refuses("\"\xc0\x80\"", 4));
    CHECK(refuses("\"\xf4\x90\x80\x80\"", 6));
    CHECK(refuses("NaN", 3));
    CHECK(refuses("Infinity", 8));
    CHECK(refuses("[1] x", 5));
    CHECK(refuses("\xef\xbb\xbf{}", 5));
    CHECK(refuses("{\"a\"\x0b:1}", 7));
    CHECK(refuses("[1]\0", 4));
    for (i = 0; i < 600; ++i) deep[i] = '[';
    for (i = 0; i < 600; ++i) deep[600 + i] = ']';
    CHECK(refuses(deep, 1200));
    CHECK(!refuses(deep + 100, 1000)); /* 500 deep is fine */
    /* Python's == */
    CHECK_EQ(py_eq("1", "1.0"), 1);
    CHECK_EQ(py_eq("1", "true"), 1);
    CHECK_EQ(py_eq("0", "false"), 1);
    CHECK_EQ(py_eq("0.0", "-0.0"), 1);
    CHECK_EQ(py_eq("null", "null"), 1);
    CHECK_EQ(py_eq("9007199254740993", "9007199254740992.0"), 0);
    CHECK_EQ(py_eq("9007199254740992", "9007199254740992.0"), 1);
    CHECK_EQ(py_eq("100000000000000000000", "1e20"), 1);
    CHECK_EQ(py_eq("100000000000000000001", "1e20"), 0);
    CHECK_EQ(py_eq("\"1\"", "1"), 0);
    CHECK_EQ(py_eq("[1,2]", "[1.0,2]"), 1);
    CHECK_EQ(py_eq("[1,2]", "[2,1]"), 0);
    CHECK_EQ(py_eq("{\"a\":1,\"b\":2}", "{\"b\":2,\"a\":1}"), 1);
    CHECK_EQ(py_eq("{\"a\":1,\"a\":2}", "{\"a\":2}"), 1);
    CHECK_EQ(py_eq("{\"a\":1}", "{\"a\":1,\"b\":1}"), 0);
    CHECK_EQ(py_eq("null", "false"), 0);
}

static int has_line(const char *path, const char *needle) {
    uint8_t *buf;
    size_t n;
    int found;
    if (mj_fs_read_all(path, 1u << 20, &B, &buf, &n) != MJ_OK) return 0;
    found = n && strstr((const char *)buf, needle) != NULL;
    mj_bfree(&B, buf, n ? n : 1);
    return found;
}

static void corrupt_copy(const char *src, const char *dst, const char *from, const char *to) {
    uint8_t *buf;
    size_t n;
    char *p;
    if (mj_fs_read_all(src, 1u << 20, &B, &buf, &n) != MJ_OK) return;
    p = (char *)malloc(n + 1);
    memcpy(p, buf, n);
    p[n] = '\0';
    {
        char *at = strstr(p, from);
        if (at && strlen(from) == strlen(to)) memcpy(at, to, strlen(to));
    }
    (void)mj_fs_remove(dst);
    (void)mj_fs_write_atomic(dst, p, n, 1);
    free(p);
    mj_bfree(&B, buf, n ? n : 1);
}

typedef struct errs { int n; char last[MJ_REPLAY_ERR_MAX]; } errs;
static void on_err(const char *path, const char *why, void *u) {
    errs *e = (errs *)u;
    (void)path;
    e->n++;
    snprintf(e->last, sizeof e->last, "%s", why);
}

static void test_replay(void) {
    char d[MJ_PATH_MAX], sub[MJ_PATH_MAX], hid[MJ_PATH_MAX], p[MJ_PATH_MAX], q[MJ_PATH_MAX], err[MJ_REPLAY_ERR_MAX];
    mj_replay_run r;
    mj_replay_set set;
    errs e;
    const char *roots[1];
    char *paths;
    uint32_t np;
    static const mj_auto_label want[] = {MJ_AUTO_GOOD, MJ_AUTO_GOOD, MJ_AUTO_NOT_YET, MJ_AUTO_NOT_YET, MJ_AUTO_GOOD,
                                         MJ_AUTO_PRODUCTIVE};
    uint32_t i;

    mj_t_dir(d, sizeof d, "replay_runs");
    (void)mj_fs_mkdir(d);
    mj_path_join(sub, sizeof sub, d, "sub");
    mj_path_join(hid, sizeof hid, d, ".hidden");
    (void)mj_fs_mkdir(sub);
    (void)mj_fs_mkdir(hid);
    mj_path_join(p, sizeof p, d, "b1s1_ge.jsonl");
    CHECK(mj_t_write_run(p, MJ_T_MOVES, MJ_T_N_MOVES, 0, 0));

    CHECK_ST(mj_replay_load(p, &B, &r, err), MJ_OK);
    CHECK_EQ(r.n_steps, 6u);
    CHECK(strncmp(r.id, "b1s1_ge.", 8) == 0 && strlen(r.id) == 16);
    CHECK(strcmp(r.source, "b1s1_ge.jsonl") == 0);
    CHECK(r.ended);
    CHECK(mj_json_str_is(&r.doc, r.flags, "test"));
    for (i = 0; i < r.n_steps && i < 6; ++i) CHECK_EQ(r.steps[i].autolab, want[i]);
    CHECK(r.steps[2].revisit && !r.steps[1].revisit);
    CHECK_EQ(mj_replay_score_bp(&r), 6666u);
    CHECK(r.steps[5].reward == 1.0);
    CHECK_EQ(r.steps[0].prop_n, 2u);
    CHECK(r.props[r.steps[0].prop_first].score == 600000.0 / 1048576.0);
    CHECK(r.steps[0].margin == 200000.0 / 1048576.0);
    mj_replay_free(&r);

    /* every way a log can be broken is refused, with Python's words */
    mj_path_join(q, sizeof q, d, "bad_hash.jsonl");
    corrupt_copy(p, q, "\"task\":5", "\"task\":6");
    CHECK_ST(mj_replay_load(q, &B, &r, err), MJ_E_CHAIN);
    CHECK(strcmp(err, "line 2 hash does not match") == 0);
    mj_path_join(q, sizeof q, d, "bad_start.jsonl");
    corrupt_copy(p, q, "\"start\"", "\"stArt\"");
    CHECK_ST(mj_replay_load(q, &B, &r, err), MJ_E_CHAIN); /* the hash breaks first */
    mj_path_join(q, sizeof q, d, "bad_fmt.jsonl");
    corrupt_copy(p, q, "pchie.research.v1\",\"event\":\"advise", "pchie.research.v2\",\"event\":\"advise");
    CHECK_ST(mj_replay_load(q, &B, &r, err), MJ_E_INVAL);
    CHECK(strcmp(err, "line 2 is not pchie.research.v1") == 0);
    mj_path_join(q, sizeof q, sub, "notes.jsonl");
    (void)mj_fs_remove(q);
    (void)mj_fs_write_atomic(q, "{\"format\":\"something.else\"}\n", 28, 1);
    mj_path_join(q, sizeof q, sub, "copy.jsonl"); /* the same run copied: loaded once */
    corrupt_copy(p, q, "zzz", "zzz");
    mj_path_join(q, sizeof q, hid, "hidden.jsonl"); /* hidden folders are not scanned */
    corrupt_copy(p, q, "zzz", "zzz");
    mj_path_join(q, sizeof q, d, ".dot.jsonl");
    corrupt_copy(p, q, "zzz", "zzz");

    roots[0] = d;
    CHECK_ST(mj_replay_find(roots, 1, &paths, &np), MJ_OK);
    CHECK_EQ(np, 5u); /* b1s1_ge, bad_fmt, bad_hash, bad_start, sub/copy */
    free(paths);
    memset(&e, 0, sizeof e);
    CHECK_ST(mj_replay_load_all(roots, 1, &B, on_err, &e, &set), MJ_OK);
    CHECK_EQ(set.n, 1u);
    CHECK_EQ(e.n, 3);
    mj_replay_set_free(&set);
    CHECK(B.used == 0);
}

static void test_safe_id(void) {
    char out[80];
    mj_safe_id("..-a b\xc3\xa9.c", 64, out, sizeof out);
    CHECK(strcmp(out, "a_b_.c") == 0);
    mj_safe_id("...", 64, out, sizeof out);
    CHECK(strcmp(out, "x") == 0);
    mj_safe_id("abc\xff\xfe", 4, out, sizeof out);
    CHECK(strcmp(out, "abc_") == 0);
}

static void test_grades(void) {
    char d[MJ_PATH_MAX], lp[MJ_PATH_MAX], msg[96];
    mj_grade_map m;
    const mj_grade *g;
    uint64_t seq = 0;
    mj_t_dir(d, sizeof d, "grades");
    (void)mj_fs_mkdir(d);
    mj_path_join(lp, sizeof lp, d, "grades.mjl");
    (void)mj_fs_remove(lp);
    CHECK_ST(mj_grades_latest(lp, &B, &m), MJ_OK); /* absent: empty */
    CHECK_EQ(m.n, 0u);
    CHECK_ST(mj_grade_add(lp, &B, "run-a.0123abcd", 3, "good", "JD", "not-yet", &seq, msg), MJ_OK);
    CHECK_EQ(seq, 1u);
    CHECK_ST(mj_grade_add(lp, &B, "run-a.0123abcd", 3, "bad", "JD", "", &seq, msg), MJ_OK);
    CHECK_ST(mj_grade_add(lp, &B, "run-b.0123abcd", -2, "productive", "helper", NULL, &seq, msg), MJ_OK);
    CHECK_ST(mj_grade_add(lp, &B, "run-b.0123abcd", 1, "great", "JD", NULL, NULL, msg), MJ_E_INVAL);
    CHECK(strcmp(msg, "unknown label: 'great'") == 0);
    CHECK_ST(mj_grade_add(lp, &B, "run-b.0123abcd", 1, "good", "bad name", NULL, NULL, msg), MJ_E_INVAL);
    CHECK(strcmp(msg, "bad grader name") == 0);
    CHECK(has_line(lp, "|g-run-a.0123abcd.s0003|"));
    CHECK(has_line(lp, "|g-run-b.0123abcd.s-002|"));
    CHECK(has_line(lp, "|step=3 auto=not-yet|"));
    CHECK(has_line(lp, "|step=3 auto=-|"));
    CHECK_ST(mj_grades_latest(lp, &B, &m), MJ_OK);
    CHECK_EQ(m.n, 2u);
    g = mj_grades_find(&m, "run-a.0123abcd", 3);
    CHECK(g != NULL && strcmp(g->label, "bad") == 0);
    g = mj_grades_find(&m, "run-b.0123abcd", -2);
    CHECK(g != NULL && strcmp(g->label, "productive") == 0 && strcmp(g->grader, "helper") == 0);
    mj_grades_free(&m);
    CHECK(B.used == 0);
}

int main(void) {
    mj_budget_init(&B, 256u << 20);
    test_json();
    test_safe_id();
    test_replay();
    test_grades();
    return mj_t_done("test_replay");
}
