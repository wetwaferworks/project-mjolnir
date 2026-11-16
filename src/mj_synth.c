/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_synth.h"

#include <string.h>

#define ONE 65536

static void state(int8_t x[MJ_TOY_F], uint32_t hot, int8_t mag) {
    memset(x, 0, MJ_TOY_F);
    x[hot] = mag;
    x[7] = 100;
}

void mj_synth_genesis(mj_toy_model *m) {
    uint32_t a;
    memset(m, 0, sizeof *m);
    for (a = 0; a < MJ_TOY_A; ++a) m->w[a][a] = 2 * ONE;
}

void mj_synth_golden(mj_toy_golden g[MJ_SYNTH_GOLDEN]) {
    static const int8_t mags[4] = {60, 80, 100, 120};
    uint32_t k, j;
    for (k = 0; k < 4; ++k)
        for (j = 0; j < 4; ++j) {
            state(g[k * 4 + j].x, k, mags[j]);
            g[k * 4 + j].expected = (uint8_t)k;
        }
}

static const uint8_t B_ACTION[3] = {2, 3, 1};

static void corridor(mj_toy_maze *mz, const uint32_t *hot, uint32_t n) {
    uint32_t i;
    memset(mz, 0, sizeof *mz);
    mz->n = n;
    for (i = 0; i < n; ++i) {
        state(mz->x[i], hot[i], 100);
        if (hot[i] >= 4) mz->x[i][hot[i] - 4] = 60; /* task B shares a feature with task A */
        mz->correct[i] = (uint8_t)(hot[i] < 4 ? hot[i] : B_ACTION[hot[i] - 4]);
        mz->type[i] = (uint8_t)(i + 1 == n ? MJ_SEG_GOAL : (i % 2 ? MJ_SEG_OBSTACLE : MJ_SEG_PIT));
    }
}

void mj_synth_mazes_a(mj_toy_maze mz[MJ_SYNTH_MAZES]) {
    static const uint32_t c[MJ_SYNTH_MAZES][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {1, 3, 0, 2}, {2, 0, 3, 1}};
    uint32_t i;
    for (i = 0; i < MJ_SYNTH_MAZES; ++i) corridor(&mz[i], c[i], 4);
}

void mj_synth_mazes_b(mj_toy_maze mz[MJ_SYNTH_MAZES]) {
    static const uint32_t c[MJ_SYNTH_MAZES][6] = {
        {0, 4, 2, 5, 6, 3}, {1, 2, 5, 4, 0, 6}, {3, 6, 1, 4, 5, 2}, {2, 0, 1, 3, 6, 5}};
    uint32_t i;
    for (i = 0; i < MJ_SYNTH_MAZES; ++i) corridor(&mz[i], c[i], 6);
}

void mj_synth_drills_b(mj_toy_maze mz[MJ_SYNTH_MAZES]) {
    static const uint32_t c[MJ_SYNTH_MAZES][3] = {{4, 5, 6}, {6, 4, 5}, {5, 6, 4}, {4, 6, 5}};
    uint32_t i;
    for (i = 0; i < MJ_SYNTH_MAZES; ++i) corridor(&mz[i], c[i], 3);
}

void mj_synth_demo_run(const mj_toy_maze *mz, const char *id, mj_toy_run *run) {
    uint32_t i;
    size_t l = strlen(id);
    memset(run, 0, sizeof *run);
    if (l > MJ_ID_MAX) l = MJ_ID_MAX;
    memcpy(run->id, id, l);
    run->n = mz->n;
    for (i = 0; i < mz->n; ++i) {
        memcpy(run->x[i], mz->x[i], MJ_TOY_F);
        run->taken[i] = mz->correct[i];
        run->seg[i].type = mz->type[i];
        run->seg[i].result = MJ_RUN_CLEARED;
    }
}

mj_status mj_synth_build(const mj_toy_model *live, mj_synth_material *m) {
    mj_toy_maze a[MJ_SYNTH_MAZES], d[MJ_SYNTH_MAZES];
    uint32_t i;
    static const char *fail_ids[MJ_SYNTH_MAZES] = {"replay-b-run-1", "replay-b-run-2", "replay-b-run-3", "replay-b-run-4"};
    static const char *demo_ids[MJ_SYNTH_MAZES] = {"replay-b-demo-1", "replay-b-demo-2", "replay-b-demo-3", "replay-b-demo-4"};
    static const char *old_ids[MJ_SYNTH_MAZES] = {"memory-a-1", "memory-a-2", "memory-a-3", "memory-a-4"};
    if (live == NULL || m == NULL) return MJ_E_INVAL;
    memset(m, 0, sizeof *m);
    mj_synth_mazes_a(a);
    mj_synth_drills_b(d);
    for (i = 0; i < MJ_SYNTH_MAZES; ++i) {
        mj_status st = mj_toy_play(live, &d[i], fail_ids[i], &m->new_runs[i], NULL);
        if (st == MJ_OK) st = mj_toy_play(live, &a[i], old_ids[i], &m->old_runs[i], NULL);
        if (st != MJ_OK) return st;
        mj_synth_demo_run(&d[i], demo_ids[i], &m->new_runs[MJ_SYNTH_MAZES + i]);
    }
    mj_synth_golden(m->golden);
    mj_synth_mazes_b(m->eval);
    m->n_new = 2u * MJ_SYNTH_MAZES;
    m->n_old = MJ_SYNTH_MAZES;
    m->n_golden = MJ_SYNTH_GOLDEN;
    m->n_eval = MJ_SYNTH_MAZES;
    return MJ_OK;
}
