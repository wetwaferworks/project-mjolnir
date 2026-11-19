/* The toy nap from replays: features, gates, store and ledger.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"
#include "mj_runlog.h"

#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_toytrain.h"

static mj_budget B;

int main(void) {
    char d[MJ_PATH_MAX], rd[MJ_PATH_MAX], p[MJ_PATH_MAX], sr[MJ_PATH_MAX], lp[MJ_PATH_MAX], msg[160], name[32];
    const char *roots[1];
    mj_replay_set set;
    const mj_replay_run *learn[3], *hold[1];
    mj_store s;
    mj_store_limits lim;
    mj_toy_model z, w1, w2;
    uint8_t blob[MJ_TOY_BLOB_LEN];
    mj_tt_cfg cfg;
    mj_tt_result r;
    mj_tt_set feat;
    mj_ledger_state lst;
    uint64_t bad;
    float loss1, loss2;
    char head[MJ_ID_MAX + 1];
    int i;

    mj_budget_init(&B, 256u << 20);
    mj_t_dir(d, sizeof d, "toytrain");
    (void)mj_fs_mkdir(d);
    mj_path_join(rd, sizeof rd, d, "runs");
    (void)mj_fs_mkdir(rd);
    for (i = 0; i < 4; ++i) {
        snprintf(name, sizeof name, "r%d.jsonl", i);
        mj_path_join(p, sizeof p, rd, name);
        CHECK(mj_t_write_run(p, MJ_T_MOVES, MJ_T_N_MOVES, i, 0));
    }
    roots[0] = rd;
    CHECK_ST(mj_replay_load_all(roots, 1, &B, NULL, NULL, &set), MJ_OK);
    CHECK_EQ(set.n, 4u);

    /* features: open bits, key, visited, previous move */
    memset(&feat, 0, sizeof feat);
    CHECK_ST(mj_tt_features(&set.runs[0], &feat, NULL), MJ_OK);
    CHECK_EQ(feat.n, 6u);
    CHECK(feat.s[0].x[1] == 127 && feat.s[0].x[3] == 127 && feat.s[0].x[0] == 0 && feat.s[0].x[6] == 0 && feat.s[0].x[7] == 127);
    CHECK(feat.s[1].x[6] == 32 && feat.s[3].x[5] == 127 && feat.s[0].x[5] == 0);
    CHECK_EQ(feat.s[2].action, 3);
    mj_tt_set_free(&feat);

    mj_path_join(sr, sizeof sr, d, "toy");
    mj_path_join(lp, sizeof lp, d, "toy-dreams.mjl");
    {
        static const char *subs[] = {"objects", "versions", "retired", "pins"};
        int k;
        for (k = 0; k < 4; ++k) {
            char sd[MJ_PATH_MAX];
            mj_path_join(sd, sizeof sd, sr, subs[k]);
            mj_t_clean(sr);
            (void)sd;
        }
    }
    (void)mj_fs_remove(lp);
    (void)mj_fs_mkdir(sr);
    mj_store_default_limits(&lim);
    lim.min_free_bytes = 0;
    CHECK_ST(mj_store_open(&s, sr, &lim, &B), MJ_OK);
    for (i = 0; i < 3; ++i) learn[i] = &set.runs[i];
    hold[0] = &set.runs[3];
    mj_tt_default_cfg(&cfg);
    cfg.max_shift_bp = 10000;

    /* no live version yet */
    CHECK_ST(mj_tt_nap(&s, lp, &B, learn, 3, hold, 1, NULL, "t0", 0, &cfg, &r, msg), MJ_E_NOTFOUND);
    CHECK(strcmp(msg, "the toy store has no live version; seed it first (toy-seed)") == 0);
    memset(&z, 0, sizeof z);
    CHECK_ST(mj_toy_save(&z, blob), MJ_OK);
    CHECK_ST(mj_store_put(&s, "toy-seed", MJ_KIND_MILESTONE, "-", "zero", blob, sizeof blob, NULL), MJ_OK);
    CHECK_ST(mj_store_set_head(&s, "toy-seed"), MJ_OK);

    /* training is deterministic, and the generator and explicit indices agree */
    {
        mj_tt_set sm, gd;
        char **src;
        uint32_t ns, k, n, bs;
        uint32_t *idx;
        uint64_t st = cfg.seed;
        memset(&sm, 0, sizeof sm);
        memset(&gd, 0, sizeof gd);
        CHECK_ST(mj_tt_build_batch(learn, 3, NULL, &sm, &src, &ns, NULL), MJ_OK);
        CHECK_ST(mj_tt_golden(hold, 1, &gd, NULL), MJ_OK);
        CHECK_EQ(ns, 3u);
        CHECK_EQ(sm.n, 18u);
        CHECK_EQ(gd.n, 4u);
        CHECK_ST(mj_tt_train(&z, &sm, &gd, &cfg, NULL, &w1, &loss1), MJ_OK);
        n = sm.n + gd.n;
        bs = n < 256 ? n : 256;
        idx = (uint32_t *)malloc((size_t)cfg.steps * bs * sizeof *idx);
        for (k = 0; k < cfg.steps * bs; ++k) idx[k] = (uint32_t)(mj_tt_next(&st) % n);
        CHECK_ST(mj_tt_train(&z, &sm, &gd, &cfg, idx, &w2, &loss2), MJ_OK);
        CHECK(memcmp(&w1, &w2, sizeof w1) == 0 && loss1 == loss2);
        CHECK(mj_tt_task_bp(&w1, &sm) > mj_tt_task_bp(&z, &sm));
        free(idx);
        free(src);
        mj_tt_set_free(&sm);
        mj_tt_set_free(&gd);
    }

    /* a dry run never moves HEAD; integrate does */
    CHECK_ST(mj_tt_nap(&s, lp, &B, learn, 3, hold, 1, NULL, "t1", 0, &cfg, &r, msg), MJ_OK);
    CHECK(strcmp(r.outcome, "dry-run-pass") == 0);
    CHECK(r.task_after > r.task_before);
    CHECK(r.max_move_q16 <= 32769);
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "toy-seed") == 0);
    CHECK_ST(mj_tt_nap(&s, lp, &B, learn, 3, hold, 1, NULL, "t2", 1, &cfg, &r, msg), MJ_OK);
    CHECK(strcmp(r.outcome, "integrated") == 0 && strcmp(r.live, "t2.cand") == 0);
    CHECK_EQ(r.ledger_seq, 2u);
    /* the same id again: the snapshot exists, refused */
    CHECK_ST(mj_tt_nap(&s, lp, &B, learn, 3, hold, 1, NULL, "t2", 0, &cfg, &r, msg), MJ_E_EXISTS);
    /* a shift gate of 0 rejects any change of behavior */
    cfg.max_shift_bp = 0;
    CHECK_ST(mj_store_set_head(&s, "toy-seed"), MJ_OK);
    CHECK_ST(mj_tt_nap(&s, lp, &B, learn, 3, hold, 1, NULL, "t3", 1, &cfg, &r, msg), MJ_OK);
    CHECK(strcmp(r.outcome, "rejected") == 0 || r.shift_bp == 0);
    CHECK_ST(mj_ledger_verify_file(lp, &B, &lst, &bad), MJ_OK);
    CHECK_EQ(lst.count, 3u);
    CHECK(strncmp(lst.last.edit, "gpu=cpu steps=300 lr=0.5 cap=0.5 samples=18 jd=0 gold=4 loss=", 61) == 0);
    mj_replay_set_free(&set);
    return mj_t_done("test_toytrain");
}
