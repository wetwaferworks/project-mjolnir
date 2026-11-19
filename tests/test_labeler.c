/* The labeling page without a socket: session order, grade, skip, back, and
 * the HTTP answers (host check, request marker, size limits, refusals).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"
#include "mj_runlog.h"

#include "mjolnir/mj_labeler.h"
#include "mjolnir/mj_ledger.h"

static mj_budget B;

static int ask(mj_lab *s, const char *req, int *code, char *body, size_t cap) {
    mj_jout r;
    const char *b;
    mj_jout_init(&r, 1u << 22);
    mj_lab_http(s, req, strlen(req), &r);
    if (r.failed || r.n < 12) { mj_jout_free(&r); return 0; }
    *code = atoi(r.p + 9);
    b = strstr(r.p, "\r\n\r\n");
    snprintf(body, cap, "%s", b ? b + 4 : "");
    mj_jout_free(&r);
    return 1;
}

static void post(char *out, size_t cap, const char *path, const char *json, const char *extra) {
    snprintf(out, cap, "POST %s HTTP/1.1\r\nHost: 127.0.0.1:8765\r\n%sContent-Length: %u\r\n\r\n%s", path, extra,
             (unsigned)strlen(json), json);
}

int main(void) {
    char d[MJ_PATH_MAX], p[MJ_PATH_MAX], gp[MJ_PATH_MAX], req[8192], json[512], rid[MJ_ID_MAX + 1];
    static char body[1 << 20];
    const char *roots[1];
    mj_replay_set set;
    mj_lab s;
    mj_lab_key k, first;
    mj_ledger_state lst;
    uint64_t bad;
    int code = 0;
    size_t total;

    mj_budget_init(&B, 256u << 20);
    mj_t_dir(d, sizeof d, "labeler");
    (void)mj_fs_mkdir(d);
    mj_path_join(p, sizeof p, d, "r1.jsonl");
    CHECK(mj_t_write_run(p, MJ_T_MOVES, MJ_T_N_MOVES, 0, 0));
    mj_path_join(gp, sizeof gp, d, "grades.mjl");
    (void)mj_fs_remove(gp);
    roots[0] = d;
    CHECK_ST(mj_replay_load_all(roots, 1, &B, NULL, NULL, &set), MJ_OK);
    CHECK_ST(mj_lab_open(&s, &set, gp, "JD", 8765, &B), MJ_OK);
    snprintf(rid, sizeof rid, "%s", set.runs[0].id);

    /* uncertain first: not a plain forward step */
    CHECK(mj_lab_next(&s, &first));
    CHECK(set.runs[0].steps[first.step].autolab != MJ_AUTO_GOOD);
    CHECK_ST(mj_lab_grade(&s, first.run, first.step, "bad", json), MJ_OK);
    CHECK(mj_lab_next(&s, &k) && !(k.step == first.step));
    CHECK(mj_lab_back(&s, &k) && k.step == first.step);
    CHECK_ST(mj_lab_grade(&s, first.run, first.step, "good", json), MJ_OK);
    CHECK_ST(mj_lab_grade(&s, first.run, first.step, "great", json), MJ_E_INVAL);
    CHECK(strcmp(json, "unknown label: 'great'") == 0);
    CHECK_ST(mj_ledger_verify_file(gp, &B, &lst, &bad), MJ_OK);
    CHECK_EQ(lst.count, 2u);
    mj_lab_close(&s);
    CHECK_ST(mj_lab_open(&s, &set, gp, "JD", 8765, &B), MJ_OK);
    CHECK_EQ(s.done.n, 1u);

    /* the page and the API */
    CHECK(ask(&s, "GET / HTTP/1.1\r\nHost: 127.0.0.1:8765\r\n\r\n", &code, body, sizeof body));
    CHECK(code == 200 && strstr(body, "Productive") != NULL && strstr(body, "</html>") != NULL);
    CHECK(ask(&s, "GET /?x=1 HTTP/1.0\r\nhost: localhost:8765\r\n\r\n", &code, body, sizeof body) && code == 200);
    CHECK(ask(&s, "GET / HTTP/1.1\r\nHost: evil.example:8765\r\n\r\n", &code, body, sizeof body) && code == 403);
    CHECK(ask(&s, "GET / HTTP/1.1\r\nHost: 127.0.0.1:8766\r\n\r\n", &code, body, sizeof body) && code == 403);
    CHECK(ask(&s, "GET / HTTP/1.1\r\n\r\n", &code, body, sizeof body) && code == 403);
    CHECK(ask(&s, "GET /api/next HTTP/1.1\r\nHost: 127.0.0.1:8765\r\n\r\n", &code, body, sizeof body) && code == 403);
    CHECK(ask(&s, "GET /api/next HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nX-Mjolnir: 1\r\n\r\n", &code, body, sizeof body));
    CHECK(code == 200 && strstr(body, "\"finished\":false") && strstr(body, "\"map\":\"") && strstr(body, "@"));
    CHECK(ask(&s, "GET /nope HTTP/1.1\r\n\r\n", &code, body, sizeof body) && code == 404);
    CHECK(ask(&s, "DELETE / HTTP/1.1\r\nHost: 127.0.0.1:8765\r\n\r\n", &code, body, sizeof body) && code == 501);
    CHECK(ask(&s, "GET /\r\n\r\n", &code, body, sizeof body) && code == 400);
    CHECK(mj_lab_next(&s, &k));
    snprintf(json, sizeof json, "{\"replay\":\"%s\",\"index\":%d,\"label\":\"productive\"}", rid, (int)k.step);
    post(req, sizeof req, "/api/grade", json, "X-Mjolnir: 1\r\nContent-Type: application/json\r\n");
    CHECK(mj_lab_http_complete(req, strlen(req), &total) == 1 && total == strlen(req));
    CHECK(mj_lab_http_complete(req, strlen(req) - 1, &total) == 0);
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 200);
    CHECK(mj_grades_find(&s.done, rid, k.step) != NULL);
    post(req, sizeof req, "/api/grade", json, "Content-Type: application/json\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 403); /* no marker */
    post(req, sizeof req, "/api/grade", "{\"replay\":\"nope\",\"index\":0,\"label\":\"good\"}", "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400 && strstr(body, "unknown step"));
    snprintf(json, sizeof json, "{\"replay\":\"%s\",\"index\":99,\"label\":\"good\"}", rid);
    post(req, sizeof req, "/api/grade", json, "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400);
    snprintf(json, sizeof json, "{\"replay\":\"%s\",\"index\":\"1\",\"label\":\"great\"}", rid);
    post(req, sizeof req, "/api/grade", json, "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400 && strstr(body, "unknown label"));
    post(req, sizeof req, "/api/grade", "{not json", "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400);
    snprintf(json, sizeof json, "{\"replay\":\"%s\",\"index\":0}", rid);
    post(req, sizeof req, "/api/skip", json, "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 200 && strstr(body, "\"skipped\":1"));
    post(req, sizeof req, "/api/back", "{}", "X-Mjolnir: 1\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 200 && strstr(body, "\"index\":0,") && strstr(body, "\"skipped\":0"));
    snprintf(req, sizeof req, "POST /api/back HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nX-Mjolnir: 1\r\nContent-Length: 5000\r\n\r\n");
    CHECK(mj_lab_http_complete(req, strlen(req), &total) == -1);
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400 && strstr(body, "too large"));
    snprintf(req, sizeof req, "POST /api/back HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nX-Mjolnir: 1\r\nContent-Length: -1\r\n\r\n");
    CHECK(ask(&s, req, &code, body, sizeof body) && code == 400);
    CHECK_ST(mj_ledger_verify_file(gp, &B, &lst, &bad), MJ_OK);
    CHECK_EQ(lst.count, 3u); /* only the one good request wrote */

    mj_lab_close(&s);
    mj_replay_set_free(&set);
    return mj_t_done("test_labeler");
}
