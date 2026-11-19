/* Test helper: write a correctly chained pchie.research.v1 run log, the same
 * shape as python/tests/util.py write_run.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_RUNLOG_H
#define MJ_RUNLOG_H
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_hash.h"

#include <stdio.h>
#include <string.h>

typedef struct mj_t_move { int action, dx, dy, reward_q20; } mj_t_move;

typedef struct mj_t_log {
    char buf[1 << 16];
    size_t n;
    char prev[MJ_SHA256_HEX];
} mj_t_log;

/* body: the record's fields without braces and without prev */
static void mj_t_emit(mj_t_log *l, const char *body) {
    char line[4096];
    char h[MJ_SHA256_HEX];
    int k = snprintf(line, sizeof line, "{%s,\"prev\":\"%s\"", body, l->prev);
    mj_sha256_hex(line, (size_t)k, h);
    k += snprintf(line + k, sizeof line - (size_t)k, ",\"sha256\":\"%s\"}\n", h);
    memcpy(l->buf + l->n, line, (size_t)k);
    l->n += (size_t)k;
    memcpy(l->prev, h, sizeof h);
}

static int mj_t_write_run(const char *path, const mj_t_move *mv, int n, int sx, int sy) {
    static mj_t_log l;
    char body[1024];
    int i, x = sx, y = sy;
    memset(&l, 0, sizeof l);
    memset(l.prev, '0', 64);
    mj_t_emit(&l, "\"format\":\"pchie.research.v1\",\"event\":\"start\",\"flags\":\"test\"");
    for (i = 0; i < n; ++i) {
        int a = mv[i].action, state = (x + 1) * 1000 + y;
        snprintf(body, sizeof body,
                 "\"format\":\"pchie.research.v1\",\"event\":\"advise\",\"decision\":%d,\"task\":5,\"x\":%d,\"y\":%d,"
                 "\"key\":0,\"open\":%d,\"proposals\":[[%d,600000,1],[%d,400000,1]],\"scores\":[]",
                 i + 1, x, y, (1 << a) | (1 << ((a + 2) % 4)), 7 + a, 7 + (a + 1) % 4);
        mj_t_emit(&l, body);
        snprintf(body, sizeof body, "\"format\":\"pchie.research.v1\",\"event\":\"decided\",\"decision\":%d,\"token\":%d",
                 i + 1, 7 + a);
        mj_t_emit(&l, body);
        x += mv[i].dx;
        y += mv[i].dy;
        snprintf(body, sizeof body,
                 "\"format\":\"pchie.research.v1\",\"event\":\"transition\",\"state\":%d,\"action\":%d,\"next\":%d,"
                 "\"reward_q20\":%d,\"ok\":1",
                 state, a, (x + 1) * 1000 + y, mv[i].reward_q20);
        mj_t_emit(&l, body);
    }
    snprintf(body, sizeof body, "\"format\":\"pchie.research.v1\",\"event\":\"end\",\"decisions\":%d", n);
    mj_t_emit(&l, body);
    (void)mj_fs_remove(path);
    return mj_fs_write_atomic(path, l.buf, l.n, 1) == MJ_OK;
}

/* The moves python/tests/test_replay.py uses: forward, a loop back, a reward. */
static const mj_t_move MJ_T_MOVES[] = {{1, 1, 0, 0}, {1, 1, 0, 0}, {3, -1, 0, 0}, {1, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 1, 1048576}};
#define MJ_T_N_MOVES ((int)(sizeof MJ_T_MOVES / sizeof MJ_T_MOVES[0]))
#endif
