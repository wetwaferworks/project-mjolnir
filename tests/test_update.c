/* The update rule on toy weights: improve without forgetting, reject and roll
 * back when the gates fail, stay deterministic, respect the ceilings.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_synth.h"
#include "mj_test.h"

static mj_synth_material M;

int main(void) {
    mj_toy_model g, c1, c2;
    mj_update_cfg cfg;
    mj_update_report rep, rep2;
    uint8_t b1[MJ_TOY_BLOB_LEN], b2[MJ_TOY_BLOB_LEN];
    uint32_t a, i, score;

    mj_synth_genesis(&g);
    CHECK_ST(mj_synth_build(&g, &M), MJ_OK);

    /* the genesis weights know the old skill and not the new one */
    CHECK_ST(mj_toy_eval(&g, M.eval, M.n_eval, &score), MJ_OK);
    CHECK_EQ(score, 3333u); /* partial credit: the old-skill segments before each first failure */

    /* 1. small nudges + old memories mixed in: better, nothing forgotten */
    mj_update_default_cfg(&cfg);
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval,
                             &cfg, &c1, &rep), MJ_OK);
    CHECK(rep.task_after_bp > rep.task_before_bp);
    CHECK_EQ(rep.task_after_bp, 10000u);
    CHECK_EQ(rep.golden_after_bp, 10000u);
    CHECK_EQ(rep.shift_bp, 0u);
    CHECK(rep.max_abs_delta <= cfg.step_cap);
    CHECK(rep.gate_step && rep.gate_shift && rep.gate_golden && rep.gate_improve);
    CHECK_EQ(rep.why[0], '\0');
    CHECK(rep.samples_old > 0);

    /* 2. same material, no old memories: the old skill is damaged and the gates say so */
    cfg.replay_old_per_new = 0;
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, NULL, 0, M.golden, M.n_golden, M.eval, M.n_eval, &cfg, &c2,
                             &rep2), MJ_E_GATE);
    CHECK(!rep2.gate_shift);
    CHECK(!rep2.gate_golden);
    CHECK(rep2.golden_after_bp < 10000u);
    CHECK(strstr(rep2.why, "shift gate") != NULL);

    /* 3. big steps, no replay: catastrophic forgetting, caught */
    cfg.lr = 65536;
    cfg.step_cap = 8 * 65536;
    cfg.epochs = 50;
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, NULL, 0, M.golden, M.n_golden, M.eval, M.n_eval, &cfg, &c2,
                             &rep2), MJ_E_GATE);
    CHECK(rep2.shift_bp >= 5000u);

    /* 4. the step cap holds per weight, whatever the learning rate */
    mj_update_default_cfg(&cfg);
    cfg.lr = 65536;
    cfg.step_cap = 1000;
    cfg.epochs = 100;
    (void)mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval, &cfg,
                          &c2, &rep2);
    for (a = 0; a < MJ_TOY_A; ++a)
        for (i = 0; i < MJ_TOY_F; ++i) {
            int32_t d = c2.w[a][i] - g.w[a][i];
            CHECK(d <= 1000 && d >= -1000);
        }

    /* 5. determinism: the same dream twice gives the same bytes */
    mj_update_default_cfg(&cfg);
    (void)mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval, &cfg,
                          &c2, &rep2);
    CHECK_ST(mj_toy_save(&c1, b1), MJ_OK);
    CHECK_ST(mj_toy_save(&c2, b2), MJ_OK);
    CHECK(memcmp(b1, b2, sizeof b1) == 0);

    /* 6. the step ceiling stops a dream and leaves nothing behind */
    cfg.max_steps = 10;
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval,
                             &cfg, &c2, &rep2), MJ_E_TIMEOUT);
    CHECK(memcmp(&c2, &g, sizeof g) == 0);

    /* 7. bad configuration and bad material are refused */
    mj_update_default_cfg(&cfg);
    cfg.lr = 0;
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval,
                             &cfg, &c2, &rep2), MJ_E_INVAL);
    mj_update_default_cfg(&cfg);
    cfg.reward_not_yet = 5; /* failures must weaken, never strengthen */
    CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, M.old_runs, M.n_old, M.golden, M.n_golden, M.eval, M.n_eval,
                             &cfg, &c2, &rep2), MJ_E_INVAL);
    mj_update_default_cfg(&cfg);
    {
        mj_toy_run bad = M.new_runs[0];
        bad.seg[0].result = MJ_RUN_NOT_REACHED; /* cleared after unreached: malformed */
        bad.seg[1].result = MJ_RUN_CLEARED;
        CHECK_ST(mj_update_dream(&g, &bad, 1, NULL, 0, M.golden, M.n_golden, M.eval, M.n_eval, &cfg, &c2, &rep2),
                 MJ_E_INVAL);
        bad = M.new_runs[4];
        bad.taken[0] = 9;
        CHECK_ST(mj_update_dream(&g, &bad, 1, NULL, 0, M.golden, M.n_golden, M.eval, M.n_eval, &cfg, &c2, &rep2),
                 MJ_E_INVAL);
    }

    /* 8. toy weight files: round trip, corruption, wrong shape */
    CHECK_ST(mj_toy_load(b1, sizeof b1, &c2), MJ_OK);
    CHECK(memcmp(&c1, &c2, sizeof c1) == 0);
    b1[20] ^= 1;
    CHECK_ST(mj_toy_load(b1, sizeof b1, &c2), MJ_E_CORRUPT);
    b1[20] ^= 1;
    b1[8] = 9;
    CHECK_ST(mj_toy_load(b1, sizeof b1, &c2), MJ_E_INVAL);
    CHECK_ST(mj_toy_load(b1, sizeof b1 - 1, &c2), MJ_E_INVAL);

    /* 9. rollback on gate failure, through the version store */
    {
        char root[MJ_PATH_MAX], head[MJ_ID_MAX + 1];
        mj_budget bud;
        mj_store s;
        mj_store_limits lim;
        uint8_t *live;
        size_t n;
        mj_t_dir(root, sizeof root, "update_rollback");
        mj_t_clean(root);
        mj_budget_init(&bud, 16u << 20);
        mj_store_default_limits(&lim);
        lim.min_free_bytes = 0;
        CHECK_ST(mj_store_open(&s, root, &lim, &bud), MJ_OK);
        CHECK_ST(mj_toy_save(&g, b1), MJ_OK);
        CHECK_ST(mj_store_put(&s, "genesis", MJ_KIND_MILESTONE, NULL, "toy", b1, sizeof b1, NULL), MJ_OK);
        CHECK_ST(mj_store_set_head(&s, "genesis"), MJ_OK);
        /* snapshot, then a careless dream that a test integrates anyway, the
           way a faulty integrator would; the out-of-dream check then fails */
        CHECK_ST(mj_store_put(&s, "d1-pre", MJ_KIND_SNAPSHOT, "genesis", "", b1, sizeof b1, NULL), MJ_OK);
        cfg.replay_old_per_new = 0;
        CHECK_ST(mj_update_dream(&g, M.new_runs, M.n_new, NULL, 0, M.golden, M.n_golden, M.eval, M.n_eval, &cfg,
                                 &c2, &rep2), MJ_E_GATE);
        CHECK_ST(mj_toy_save(&c2, b2), MJ_OK);
        CHECK_ST(mj_store_put(&s, "d1-cand", MJ_KIND_CANDIDATE, "d1-pre", "", b2, sizeof b2, NULL), MJ_OK);
        CHECK_ST(mj_store_set_head(&s, "d1-cand"), MJ_OK);
        CHECK_ST(mj_store_get(&s, "d1-cand", &live, &n), MJ_OK);
        CHECK_ST(mj_toy_load(live, n, &c2), MJ_OK);
        mj_bfree(&bud, live, n);
        {
            uint32_t ok = 0, k;
            for (k = 0; k < M.n_golden; ++k) ok += mj_toy_act(&c2, M.golden[k].x) == M.golden[k].expected;
            CHECK(ok < M.n_golden); /* verification outside the dream fails */
        }
        CHECK_ST(mj_store_rollback(&s, "d1-pre"), MJ_OK);
        CHECK_ST(mj_store_head(&s, head), MJ_OK);
        CHECK(strcmp(head, "d1-pre") == 0);
        CHECK_ST(mj_store_get(&s, head, &live, &n), MJ_OK);
        CHECK(n == sizeof b1 && memcmp(live, b1, n) == 0); /* bit-identical to the weights before the dream */
        mj_bfree(&bud, live, n);
        CHECK_EQ(bud.used, 0u);
    }
    return mj_t_done("test_update");
}
