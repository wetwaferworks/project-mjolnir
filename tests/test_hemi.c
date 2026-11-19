/* The left and right hemispheres: verdicts, the MJHEMI2 format, the
 * combiner, the merge, training, whole toy rounds (kept, rolled back,
 * repeatable), the classroom (watcher lessons, per-side gates, the pipeline),
 * the gate for real-model rounds and the CPU/GPU batch scorer.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"

#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_classroom.h"
#include "mjolnir/mj_hemi.h"
#include "mjolnir/mj_hemi_gate.h"
#include "mjolnir/mj_ledger.h"

static void probs4(uint16_t p[MJ_HEMI_MAX_ACTIONS], uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
    memset(p, 0, MJ_HEMI_MAX_ACTIONS * sizeof p[0]);
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
}

static void test_judge(void) {
    uint16_t p[MJ_HEMI_MAX_ACTIONS];
    mj_hemi_verdict v;
    probs4(p, 40000, 20000, 5535, 0);
    CHECK_EQ(mj_hemi_pick(p, 4, 0xF), 0u);
    CHECK_EQ(mj_hemi_pick(p, 4, 0xE), 1u);         /* move 0 not legal */
    CHECK_EQ(mj_hemi_pick(p, 4, 0x0), MJ_HEMI_MAX_ACTIONS);
    mj_hemi_judge(p, 4, 0xF, 0, &v);
    CHECK(v.reason == MJ_HEMI_AGREE && v.choice == 0);
    CHECK_EQ(v.score_bp, (int16_t)((20000 * 10000) / 65535));
    CHECK_EQ(v.confidence_bp, (uint16_t)((20000 * 10000) / 65535));
    mj_hemi_judge(p, 4, 0xF, 2, &v);
    CHECK(v.reason == MJ_HEMI_PREFER_OTHER && v.choice == 0 && v.score_bp < 0);
    mj_hemi_judge(p, 4, 0xB, 2, &v);                /* move 2 not legal */
    CHECK(v.reason == MJ_HEMI_ILLEGAL && v.score_bp == -10000);
    probs4(p, 33000, 32535, 0, 0);
    mj_hemi_judge(p, 4, 0xF, 1, &v);                /* a 465 / 65535 margin: unsure */
    CHECK(v.reason == MJ_HEMI_UNSURE && v.choice == 0);
    probs4(p, 30000, 30000, 5535, 0);               /* ties go to the lower move */
    CHECK_EQ(mj_hemi_pick(p, 4, 0xF), 0u);
}

static void sample_set(mj_hemi_set *s, mj_budget *b, uint32_t n) {
    mj_hemi_header h;
    uint32_t i;
    memset(&h, 0, sizeof h);
    h.round = 7; h.pass = 2; h.purpose = MJ_HEMI_HOLDOUT; h.actor_side = MJ_HEMI_RIGHT; h.world = MJ_HEMI_WORLD_TOY_MAZE;
    h.n_features = 8; h.n_actions = 4; h.seed = 0x0123456789ABCDEFull;
    memset(h.actor, 0xA1, sizeof h.actor);
    memset(h.observer, 0xB2, sizeof h.observer);
    mj_hemi_set_init(s, b, &h);
    for (i = 0; i < n; ++i) {
        mj_hemi_record r;
        uint32_t f;
        memset(&r, 0, sizeof r);
        r.episode = i / 3; r.step = (uint16_t)(i % 3);
        for (f = 0; f < 8; ++f) r.features[f] = (int8_t)(i * 7 + f * 13 - 100);
        probs4(r.actor_probs, (uint16_t)(30000 + i), 20000, (uint16_t)(15535 - i), 0);
        probs4(r.watcher_probs, 10000, 50000, 5535, 0);
        r.actor_move = 0;
        mj_hemi_judge(r.watcher_probs, 4, 0xF, 0, &r.verdict);
        r.grade = (uint8_t)(i % 3 == 2 ? MJ_CREDIT_NOT_YET : MJ_CREDIT_SUCCESS);
        CHECK_ST(mj_hemi_set_add(s, &r), MJ_OK);
    }
}

static void test_format(void) {
    mj_budget b;
    mj_hemi_set s, t, u;
    uint8_t *buf, *buf2;
    size_t n, n2, i;
    uint32_t bad = 0;
    mj_hemi_record r;
    mj_budget_init(&b, 64u << 20);
    sample_set(&s, &b, 30);
    CHECK_ST(mj_hemi_encode(&s, &b, &buf, &n), MJ_OK);
    CHECK_EQ(n, (size_t)(MJ_HEMI_HEADER_BYTES + 30u * (14u + 8u + 16u) + MJ_HEMI_TRAILER_BYTES));
    CHECK_ST(mj_hemi_parse(buf, n, &b, &t), MJ_OK);
    CHECK(t.n == 30 && t.h.round == 7 && t.h.pass == 2 && t.h.purpose == MJ_HEMI_HOLDOUT && t.h.seed == 0x0123456789ABCDEFull);
    CHECK(memcmp(t.r, s.r, 30 * sizeof *s.r) == 0);
    CHECK_ST(mj_hemi_encode(&t, &b, &buf2, &n2), MJ_OK); /* the same bytes back */
    CHECK(n2 == n && memcmp(buf, buf2, n) == 0);
    mj_bfree(&b, buf2, n2);
    /* every flipped bit and every truncation is refused */
    for (i = 0; i < n; ++i) {
        buf[i] ^= 0x10;
        if (mj_hemi_parse(buf, n, &b, &u) == MJ_OK) { ++bad; mj_hemi_set_free(&u); }
        buf[i] ^= 0x10;
    }
    CHECK_EQ(bad, 0u);
    for (i = 0; i < n; i += 7)
        if (mj_hemi_parse(buf, i, &b, &u) == MJ_OK) { ++bad; mj_hemi_set_free(&u); }
    CHECK_EQ(bad, 0u);
    /* fields that pass the checksum but break the rules: re-seal and refuse */
    {
        static const size_t at[] = {8, 10, 16, 17, 18, 19, 20, 21, 21, 22, 100, 110, 128 + 6, 128 + 7, 128 + 8, 128 + 9, 128 + 13};
        static const uint8_t val[] = {3, 127, 3, 2, 9, 65, 17, 0, 3, 1, 99, 1, 4, 0, 4, 5, 0x40};
        size_t k;
        for (k = 0; k < sizeof at / sizeof at[0]; ++k) {
            uint8_t keep = buf[at[k]];
            buf[at[k]] = val[k];
            mj_sha256(buf, n - 32, buf + n - 32);
            if (mj_hemi_parse(buf, n, &b, &u) == MJ_OK) { ++bad; mj_hemi_set_free(&u); fprintf(stderr, "accepted byte %u\n", (unsigned)at[k]); }
            buf[at[k]] = keep;
        }
        mj_sha256(buf, n - 32, buf + n - 32);
        CHECK_EQ(bad, 0u);
        CHECK_ST(mj_hemi_parse(buf, n, &b, &u), MJ_OK);
        mj_hemi_set_free(&u);
    }
    /* records out of order are refused */
    r = s.r[3];
    CHECK_ST(mj_hemi_set_add(&s, &r), MJ_E_INVAL);
    /* probabilities that do not add up are refused */
    r = s.r[29];
    r.step = 99;
    r.actor_probs[0] = 1;
    CHECK_ST(mj_hemi_set_add(&s, &r), MJ_E_INVAL);
    r.actor_probs[0] = s.r[29].actor_probs[0];
    r.features[9] = 1; /* past n_features */
    CHECK_ST(mj_hemi_set_add(&s, &r), MJ_E_INVAL);
    mj_bfree(&b, buf, n);
    mj_hemi_set_free(&s);
    mj_hemi_set_free(&t);
    CHECK_EQ(b.used, (size_t)0);
}

static void test_combine(void) {
    mj_budget b;
    mj_hemi_set s;
    const mj_hemi_set *sp[1];
    mj_hemi_target *t;
    uint32_t n, i, from_watcher = 0, neg = 0, pos = 0;
    mj_hemi_combine_cfg c;
    mj_budget_init(&b, 64u << 20);
    sample_set(&s, &b, 30); /* every third step failed; the watcher always prefers move 1 */
    sp[0] = &s;
    mj_hemi_combine_default(&c);
    CHECK_ST(mj_hemi_combine(sp, 1, &c, &b, &t, &n), MJ_OK);
    for (i = 0; i < n; ++i) {
        if (t[i].from_watcher) { ++from_watcher; CHECK(t[i].move == 1 && t[i].weight >= 0.0f); }
        else if (t[i].weight < 0) ++neg;
        else ++pos;
    }
    CHECK_EQ(pos, 20u);
    CHECK_EQ(neg, 10u);
    /* the watcher objected 30 times and was right 10 times: trust 2*10/30-1 < 0, nothing from it */
    CHECK_EQ(from_watcher, 0u);
    mj_bfree(&b, t, (size_t)60 * sizeof *t);
    /* make the watcher earn trust: it objects only where the key fails */
    for (i = 0; i < s.n; ++i)
        if (s.r[i].grade == MJ_CREDIT_SUCCESS) {
            probs4(s.r[i].watcher_probs, 50000, 10000, 5535, 0);
            mj_hemi_judge(s.r[i].watcher_probs, 4, 0xF, 0, &s.r[i].verdict);
        }
    CHECK_ST(mj_hemi_combine(sp, 1, &c, &b, &t, &n), MJ_OK);
    for (i = 0, from_watcher = 0; i < n; ++i) from_watcher += t[i].from_watcher;
    CHECK_EQ(from_watcher, 10u);
    mj_bfree(&b, t, (size_t)60 * sizeof *t);
    c.watcher_weight = 0.0f;
    CHECK_ST(mj_hemi_combine(sp, 1, &c, &b, &t, &n), MJ_OK);
    CHECK_EQ(n, 30u);
    mj_bfree(&b, t, (size_t)60 * sizeof *t);
    mj_hemi_set_free(&s);
    CHECK_EQ(b.used, (size_t)0);
}

static void test_merge_train(void) {
    mj_toy_model a, c, m, x, y;
    mj_hemi_train_cfg tc;
    mj_hemi_target t[3];
    float loss;
    uint32_t i, f;
    mj_hemi_toy_teacher(5, &a);
    mj_hemi_toy_teacher(6, &c);
    mj_hemi_toy_merge(&a, &c, 0, &m);
    CHECK(memcmp(&m, &a, sizeof m) == 0);
    mj_hemi_toy_merge(&a, &c, 65536, &m);
    CHECK(memcmp(&m, &c, sizeof m) == 0);
    mj_hemi_toy_merge(&a, &c, 32768, &m);
    for (i = 0; i < MJ_TOY_A; ++i)
        for (f = 0; f < MJ_TOY_F; ++f) {
            int64_t lo = a.w[i][f] < c.w[i][f] ? a.w[i][f] : c.w[i][f];
            int64_t hi = a.w[i][f] < c.w[i][f] ? c.w[i][f] : a.w[i][f];
            CHECK(m.w[i][f] >= lo && m.w[i][f] <= hi);
        }
    memset(t, 0, sizeof t);
    for (i = 0; i < 3; ++i) {
        t[i].features[i] = 100;
        t[i].features[7] = 64;
        t[i].move = (uint8_t)i;
        t[i].weight = i == 2 ? -0.5f : 1.0f;
    }
    mj_hemi_train_default(&tc);
    tc.step_cap = 0.25f;
    CHECK_ST(mj_hemi_toy_train(&a, t, 3, &tc, &x, &loss), MJ_OK);
    CHECK_ST(mj_hemi_toy_train(&a, t, 3, &tc, &y, &loss), MJ_OK);
    CHECK(memcmp(&x, &y, sizeof x) == 0); /* same seed, same weights */
    for (i = 0; i < MJ_TOY_A; ++i)
        for (f = 0; f < MJ_TOY_F; ++f) CHECK(llabs((long long)x.w[i][f] - a.w[i][f]) <= 16384 + 1);
    tc.step_cap = 4.0f; /* room to learn: the two positive targets win */
    CHECK_ST(mj_hemi_toy_train(&a, t, 3, &tc, &y, &loss), MJ_OK);
    CHECK(mj_toy_act(&y, t[0].features) == 0 && mj_toy_act(&y, t[1].features) == 1);
    CHECK_ST(mj_hemi_toy_train(&a, NULL, 0, &tc, &y, &loss), MJ_OK); /* no targets: unchanged */
    CHECK(memcmp(&a, &y, sizeof a) == 0);
    tc.batch = 0;
    CHECK_ST(mj_hemi_toy_train(&a, t, 3, &tc, &y, &loss), MJ_E_INVAL);
}

static void run_rounds(const char *name, const char *scenario, uint32_t rounds, uint8_t final_sha[2][32],
                       uint32_t *kept, int gpu) {
    char dir[MJ_PATH_MAX], ledger[MJ_PATH_MAX], sub[MJ_PATH_MAX];
    mj_toy_model teacher, left, right;
    mj_budget b;
    uint32_t r;
    uint8_t blob[MJ_TOY_BLOB_LEN];
    mj_ledger_state ls;
    uint64_t bad;
    mj_t_dir(dir, sizeof dir, name);
    for (r = 1; r <= rounds; ++r) { /* clean an earlier run */
        char rn[16];
        snprintf(rn, sizeof rn, "swap-%03u", (unsigned)r);
        if (mj_path_join(sub, sizeof sub, dir, rn) == MJ_OK) mj_t_clean(sub);
    }
    mj_t_clean(dir);
    CHECK_ST(mj_path_join(ledger, sizeof ledger, dir, "hemispheres.mjl"), MJ_OK);
    (void)mj_fs_remove(ledger);
    mj_budget_init(&b, 256u << 20);
    mj_hemi_toy_teacher(1, &teacher);
    CHECK_ST(mj_hemi_toy_parents(scenario, 1, &teacher, &left, &right), MJ_OK);
    *kept = 0;
    for (r = 1; r <= rounds; ++r) {
        mj_hemi_round_cfg c;
        mj_hemi_round_report rep;
        memset(&c, 0, sizeof c);
        c.round = r;
        c.train_seed = 1000003ull + r;
        c.holdout_seed = 1500003ull + r;
        c.train_mazes = 256;
        c.holdout_mazes = 1024;
        c.segments = 16;
        c.use_gpu = gpu;
        mj_hemi_combine_default(&c.combine);
        mj_hemi_train_default(&c.train);
        c.train.seed = c.train_seed;
        CHECK_ST(mj_hemisphere_swap_toy(dir, ledger, &teacher, &left, &right, &c, &b, &rep), MJ_OK);
        if (rep.kept) {
            ++*kept;
            CHECK(rep.child_holdout_bp > rep.left_holdout_bp && rep.child_holdout_bp > rep.right_holdout_bp);
        }
    }
    CHECK_ST(mj_ledger_verify_file(ledger, &b, &ls, &bad), MJ_OK);
    CHECK_EQ(ls.count, (uint64_t)rounds);
    CHECK_ST(mj_toy_save(&left, blob), MJ_OK);
    mj_sha256(blob, sizeof blob, final_sha[0]);
    CHECK_ST(mj_toy_save(&right, blob), MJ_OK);
    mj_sha256(blob, sizeof blob, final_sha[1]);
    CHECK_EQ(b.used, (size_t)0);
}

static void test_rounds(void) {
    uint8_t a[2][32], c[2][32];
    uint32_t ka, kc;
    run_rounds("hemi_a", "complement", 3, a, &ka, 0);
    run_rounds("hemi_b", "complement", 3, c, &kc, 0);
    CHECK(memcmp(a, c, sizeof a) == 0); /* repeatable, bit for bit */
    CHECK(ka == kc && ka >= 1);
    printf("complement: %u of 3 rounds kept\n", (unsigned)ka);
    run_rounds("hemi_t", "ternary", 3, a, &ka, 0);
    printf("ternary: %u of 3 rounds kept\n", (unsigned)ka);
    if (mj_hemi_gpu_built()) {
        run_rounds("hemi_g", "ternary", 3, c, &kc, 1);
        CHECK(memcmp(a, c, sizeof a) == 0 && ka == kc); /* the GPU scorer changes nothing */
    }
}

static void test_watcher_targets(void) {
    /* one record for each of the four judgments the key can confirm or
     * refute, an illegal move the watcher rightly flagged, and an unsure
     * verdict that teaches nothing */
    mj_budget b;
    mj_hemi_set s;
    mj_hemi_header h;
    mj_hemi_target *t = NULL;
    mj_hemi_tally ty;
    uint32_t n = 0, i;
    static const struct { uint16_t move; uint8_t grade; } cases[6] = {
        {0, MJ_CREDIT_SUCCESS},  /* agreed, cleared: keep +0.25 */
        {0, MJ_CREDIT_NOT_YET},  /* agreed, failed: a miss, -1 */
        {2, MJ_CREDIT_SUCCESS},  /* objected, cleared: a miss, +1 */
        {2, MJ_CREDIT_NOT_YET},  /* objected, failed: keep -0.25 */
        {3, MJ_CREDIT_NOT_YET},  /* move 3 is illegal here, rightly flagged: -0.25 */
        {1, MJ_CREDIT_SUCCESS},  /* unsure (built below): nothing */
    };
    static const float want[5] = {0.25f, -1.0f, 1.0f, -0.25f, -0.25f};
    mj_budget_init(&b, 1u << 20);
    memset(&h, 0, sizeof h);
    h.round = 1; h.pass = 1; h.purpose = MJ_HEMI_LESSON; h.actor_side = MJ_HEMI_LEFT; h.world = MJ_HEMI_WORLD_TOY_MAZE;
    h.n_features = 8; h.n_actions = 4;
    mj_hemi_set_init(&s, &b, &h);
    for (i = 0; i < 6; ++i) {
        mj_hemi_record r;
        memset(&r, 0, sizeof r);
        r.step = (uint16_t)i;
        r.features[0] = (int8_t)i;
        probs4(r.actor_probs, 20000, 20000, 25535, 0);
        if (i == 5) probs4(r.watcher_probs, 33000, 32535, 0, 0);
        else probs4(r.watcher_probs, 50000, 10000, 5535, 0);
        r.actor_move = (uint8_t)cases[i].move;
        mj_hemi_judge(r.watcher_probs, 4, 0x7, r.actor_move, &r.verdict);
        r.grade = cases[i].grade;
        CHECK_ST(mj_hemi_set_add(&s, &r), MJ_OK);
    }
    mj_hemi_tally_set(&s, &ty);
    CHECK(ty.agree_right == 1 && ty.agree_wrong == 1 && ty.object_wrong == 1 && ty.object_right == 2 && ty.unsure == 1);
    CHECK_EQ(mj_classroom_judge_bp(&ty), 6000u);
    CHECK_ST(mj_classroom_watcher_targets(&s, 1.0f, 0.25f, &b, &t, &n), MJ_OK);
    CHECK_EQ(n, 5u);
    for (i = 0; i < n && i < 5; ++i) {
        CHECK(t[i].features[0] == (int8_t)i && t[i].move == cases[i].move && t[i].from_watcher == 1);
        CHECK(t[i].weight == want[i]);
    }
    mj_bfree(&b, t, (size_t)s.n * sizeof *t);
    CHECK_ST(mj_classroom_watcher_targets(NULL, 1.0f, 0.25f, &b, &t, &n), MJ_E_INVAL);
    mj_hemi_set_free(&s);
    CHECK_EQ(b.used, (size_t)0);
}

static void clean_stage_dirs(const char *dir, uint32_t rounds) {
    char sub[MJ_PATH_MAX], rn[24];
    uint32_t r;
    for (r = 1; r <= rounds; ++r) {
        snprintf(rn, sizeof rn, "classroom-%03u", (unsigned)r);
        if (mj_path_join(sub, sizeof sub, dir, rn) == MJ_OK) mj_t_clean(sub);
        snprintf(rn, sizeof rn, "swap-%03u", (unsigned)r);
        if (mj_path_join(sub, sizeof sub, dir, rn) == MJ_OK) mj_t_clean(sub);
    }
    mj_t_clean(dir);
}

/* classroom rounds, then swap rounds, as the pipeline runs them */
static void run_pipeline(const char *name, uint32_t classroom_rounds, uint32_t swap_rounds, int train_steps,
                         uint8_t final_sha[2][32], uint32_t *improved, uint32_t *swaps_kept) {
    char dir[MJ_PATH_MAX], ledger[MJ_PATH_MAX];
    mj_toy_model teacher, left, right;
    mj_budget b;
    uint32_t r;
    uint8_t blob[MJ_TOY_BLOB_LEN];
    mj_ledger_state ls;
    uint64_t bad;
    mj_t_dir(dir, sizeof dir, name);
    clean_stage_dirs(dir, classroom_rounds > swap_rounds ? classroom_rounds : swap_rounds);
    CHECK_ST(mj_path_join(ledger, sizeof ledger, dir, "hemispheres.mjl"), MJ_OK);
    (void)mj_fs_remove(ledger);
    mj_budget_init(&b, 256u << 20);
    mj_hemi_toy_teacher(1, &teacher);
    CHECK_ST(mj_hemi_toy_parents("complement", 1, &teacher, &left, &right), MJ_OK);
    *improved = 0;
    *swaps_kept = 0;
    for (r = 1; r <= classroom_rounds; ++r) {
        mj_classroom_cfg c;
        mj_classroom_report rep;
        mj_toy_model l0 = left, r0 = right;
        mj_classroom_default(&c);
        c.round = r;
        c.world_seed = 900000ull + r;
        c.holdout_seed = 950000ull + r;
        c.mazes = 256;
        c.holdout_mazes = 1024;
        c.train.seed = c.world_seed;
        if (train_steps >= 0) c.train.steps = (uint32_t)train_steps;
        CHECK_ST(mj_classroom_toy(dir, ledger, &teacher, &left, &right, &c, &b, &rep), MJ_OK);
        /* each side changes only through its own gate */
        if (rep.actor_kept) { ++*improved; CHECK(rep.actor_after_bp > rep.actor_before_bp); }
        else CHECK(memcmp(&left, &l0, sizeof l0) == 0);
        if (rep.watcher_kept) {
            ++*improved;
            CHECK(rep.watcher_judge_after_bp > rep.watcher_judge_before_bp);
            CHECK(rep.watcher_play_after_bp >= rep.watcher_play_before_bp);
        } else CHECK(memcmp(&right, &r0, sizeof r0) == 0);
        CHECK(rep.tally.steps > 0);
    }
    for (r = 1; r <= swap_rounds; ++r) {
        mj_hemi_round_cfg c;
        mj_hemi_round_report rep;
        memset(&c, 0, sizeof c);
        c.round = r;
        c.train_seed = 1000003ull + r;
        c.holdout_seed = 1500003ull + r;
        c.train_mazes = 256;
        c.holdout_mazes = 1024;
        c.segments = 16;
        mj_hemi_combine_default(&c.combine);
        mj_hemi_train_default(&c.train);
        c.train.seed = c.train_seed;
        CHECK_ST(mj_hemisphere_swap_toy(dir, ledger, &teacher, &left, &right, &c, &b, &rep), MJ_OK);
        if (rep.kept) ++*swaps_kept;
    }
    CHECK_ST(mj_ledger_verify_file(ledger, &b, &ls, &bad), MJ_OK);
    CHECK_EQ(ls.count, (uint64_t)(classroom_rounds + swap_rounds));
    CHECK_ST(mj_toy_save(&left, blob), MJ_OK);
    mj_sha256(blob, sizeof blob, final_sha[0]);
    CHECK_ST(mj_toy_save(&right, blob), MJ_OK);
    mj_sha256(blob, sizeof blob, final_sha[1]);
    CHECK_EQ(b.used, (size_t)0);
}

static void test_classroom(void) {
    uint8_t a[2][32], c[2][32];
    uint32_t ia, sa, ic, sc;
    run_pipeline("hemi_class_a", 2, 2, -1, a, &ia, &sa);
    run_pipeline("hemi_class_b", 2, 2, -1, c, &ic, &sc);
    CHECK(memcmp(a, c, sizeof a) == 0 && ia == ic && sa == sc); /* repeatable, bit for bit */
    CHECK(ia >= 1);                                             /* the classroom taught something */
    /* no learning at all: both sides are restored every time */
    run_pipeline("hemi_class_rb", 1, 0, 0, a, &ia, &sa);
    CHECK_EQ(ia, 0u);
    {
        char dir[MJ_PATH_MAX], ledger[MJ_PATH_MAX];
        mj_budget b;
        mj_ledger_state ls;
        uint64_t bad;
        mj_t_dir(dir, sizeof dir, "hemi_class_rb");
        CHECK_ST(mj_path_join(ledger, sizeof ledger, dir, "hemispheres.mjl"), MJ_OK);
        mj_budget_init(&b, 16u << 20);
        CHECK_ST(mj_ledger_verify_file(ledger, &b, &ls, &bad), MJ_OK);
        CHECK(ls.count == 1 && strcmp(ls.last.outcome, "rolled-back") == 0 && strcmp(ls.last.trigger, "classroom") == 0);
    }
}

static void test_rollback(void) {
    /* identical parents and no learning: the child equals them and cannot
     * beat them, so the round rolls back and the parents come back intact */
    char dir[MJ_PATH_MAX], ledger[MJ_PATH_MAX];
    mj_toy_model teacher, left, right, l0;
    mj_budget b;
    mj_hemi_round_cfg c;
    mj_hemi_round_report rep;
    mj_ledger_state ls;
    uint64_t bad;
    mj_t_dir(dir, sizeof dir, "hemi_rb");
    mj_t_clean(dir);
    CHECK_ST(mj_path_join(ledger, sizeof ledger, dir, "hemispheres.mjl"), MJ_OK);
    (void)mj_fs_remove(ledger);
    mj_budget_init(&b, 256u << 20);
    mj_hemi_toy_teacher(9, &teacher);
    CHECK_ST(mj_hemi_toy_parents("ternary", 9, &teacher, &left, &right), MJ_OK);
    right = left;
    l0 = left;
    memset(&c, 0, sizeof c);
    c.round = 1; c.train_seed = 11; c.holdout_seed = 12; c.train_mazes = 64; c.holdout_mazes = 256; c.segments = 8;
    mj_hemi_combine_default(&c.combine);
    mj_hemi_train_default(&c.train);
    c.train.steps = 0;
    CHECK_ST(mj_hemisphere_swap_toy(dir, ledger, &teacher, &left, &right, &c, &b, &rep), MJ_OK);
    CHECK(!rep.kept);
    CHECK(memcmp(&left, &l0, sizeof l0) == 0 && memcmp(&right, &l0, sizeof l0) == 0);
    CHECK_ST(mj_ledger_verify_file(ledger, &b, &ls, &bad), MJ_OK);
    CHECK(ls.count == 1 && strcmp(ls.last.outcome, "rolled-back") == 0);
    CHECK_EQ(b.used, (size_t)0);
}

static void test_gpu_parity(void) {
    enum { N = 100003 };
    static int8_t states[(size_t)N * MJ_TOY_F];
    static uint8_t cpu[(size_t)N * 3], gpu[(size_t)N * 3];
    mj_toy_model m[3];
    uint64_t s = 3;
    uint32_t i, diff = 0;
    char err[128];
    for (i = 0; i < (uint32_t)N * MJ_TOY_F; ++i) states[i] = (int8_t)(mj_hemi_next(&s) & 0xFF);
    for (i = 0; i < 3; ++i) mj_hemi_toy_teacher(40 + i, &m[i]);
    m[2] = m[1];
    m[2].w[3][0] = m[2].w[2][0]; /* make ties happen */
    CHECK_ST(mj_hemi_toy_act_batch_cpu(m, 3, states, N, cpu), MJ_OK);
    if (!mj_hemi_gpu_built()) {
        CHECK_ST(mj_hemi_toy_act_batch_gpu(m, 3, states, N, gpu, err), MJ_E_INVAL);
        printf("gpu parity: built without CUDA, skipped\n");
        return;
    }
    if (mj_hemi_toy_act_batch_gpu(m, 3, states, N, gpu, err) != MJ_OK) {
        printf("gpu parity: %s, skipped\n", err);
        return;
    }
    for (i = 0; i < (uint32_t)N * 3; ++i) diff += cpu[i] != gpu[i];
    CHECK_EQ(diff, 0u);
    printf("gpu parity: %u moves, %u differ\n", (unsigned)(N * 3), (unsigned)diff);
}

/* ---- the gate for rounds played outside Mjolnir (review of 19 November 2026) ----
 * Scores are worked out by Mjolnir from sealed sets bound to each model's
 * SHA-256; nothing is taken from the caller's word. */
#define GATE_N 40u

/* A set over GATE_N states. moves[i] is the actor's move and wmoves[i] the
 * watcher's; the answer key clears moves 0 and 1 and nothing else. */
static void gate_set(mj_hemi_set *s, mj_budget *b, uint8_t purpose, uint64_t seed, uint8_t actor, uint8_t watcher,
                     const uint8_t *moves, const uint8_t *wmoves) {
    mj_hemi_header h;
    uint32_t i;
    memset(&h, 0, sizeof h);
    h.round = 1; h.pass = 1; h.purpose = purpose; h.actor_side = MJ_HEMI_LEFT; h.world = MJ_HEMI_WORLD_GRID_MAZE;
    h.n_features = 2; h.n_actions = 4; h.seed = seed;
    memset(h.actor, actor, MJ_HEMI_FINGERPRINT);
    memset(h.observer, watcher, MJ_HEMI_FINGERPRINT);
    mj_hemi_set_init(s, b, &h);
    for (i = 0; i < GATE_N; ++i) {
        mj_hemi_record r;
        memset(&r, 0, sizeof r);
        r.episode = i / 8;
        r.step = (uint16_t)(i % 8);
        r.features[0] = (int8_t)(i % 7);
        r.features[1] = (int8_t)(i / 7 + (int)seed);
        r.actor_move = moves[i];
        r.grade = moves[i] <= 1 ? MJ_CREDIT_SUCCESS : MJ_CREDIT_NOT_YET;
        r.actor_probs[moves[i]] = MJ_HEMI_PROB_ONE;
        r.watcher_probs[wmoves[i]] = MJ_HEMI_PROB_ONE;
        r.verdict.choice = wmoves[i];
        r.verdict.reason = wmoves[i] == moves[i] ? MJ_HEMI_AGREE : MJ_HEMI_PREFER_OTHER;
        r.verdict.confidence_bp = 10000;
        CHECK_ST(mj_hemi_set_add(s, &r), MJ_OK);
    }
}

/* cleared: how many of the GATE_N moves are good ones */
static void gate_moves(uint8_t *m, uint32_t cleared) {
    uint32_t i;
    for (i = 0; i < GATE_N; ++i) m[i] = (uint8_t)(i < cleared ? (i & 1) : 2 + (i & 1));
}

static void gate_in(mj_hemi_gate_in *in, mj_budget *b, uint32_t kind, uint8_t parent, uint8_t cand, uint8_t other) {
    memset(in, 0, sizeof *in);
    in->kind = kind;
    in->budget = b;
    memset(in->parent, parent, MJ_HEMI_FINGERPRINT);
    memset(in->candidate, cand, MJ_HEMI_FINGERPRINT);
    memset(in->other, other, MJ_HEMI_FINGERPRINT);
}

static void test_gate(void) {
    enum { P = 1, C = 2, W = 3, A = 4, O = 5 };
    mj_budget b;
    mj_hemi_set lesson, hp, hc, ho, x;
    mj_hemi_gate_in in;
    mj_hemi_gate_out g;
    uint8_t m20[GATE_N], m30[GATE_N], m35[GATE_N], all_good[GATE_N];
    mj_budget_init(&b, 64u << 20);
    gate_moves(m20, 20);
    gate_moves(m30, 30);
    gate_moves(m35, 35);
    gate_moves(all_good, GATE_N);

    /* actor: the candidate plays better on the same held-out states -> keep */
    gate_set(&lesson, &b, MJ_HEMI_LESSON, 1, P, W, m20, m20);
    gate_set(&hp, &b, MJ_HEMI_HOLDOUT, 2, P, P, m20, m20);
    gate_set(&hc, &b, MJ_HEMI_HOLDOUT, 2, C, C, m30, m30);
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &hc;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_OK);
    CHECK(g.keep == 1 && g.states == GATE_N && g.before_bp == 5000 && g.after_bp == 7500);
    /* the same evidence read the other way round -> roll back */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, C, P, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &hc;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);   /* the lesson was not played by C */
    CHECK(strstr(g.why, "not played by the parent") != NULL);
    /* a made-up parent: no set is bound to it */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, 9, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hc;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    /* the parent's own play is missing */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hc;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    CHECK(strstr(g.why, "played by the parent") != NULL);
    /* a lesson passed off as holdout evidence */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &lesson;
    in.holdout[in.n_holdout++] = &hc;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    CHECK(strstr(g.why, "marked as a lesson set") != NULL);
    /* the candidate scored on easier states */
    gate_set(&x, &b, MJ_HEMI_HOLDOUT, 3, C, C, m30, m30);
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &x;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    CHECK(strstr(g.why, "same states") != NULL);
    mj_hemi_set_free(&x);
    /* a grade bumped without changing the move: the key graded the same move two ways */
    gate_set(&x, &b, MJ_HEMI_HOLDOUT, 2, C, C, m30, m30);
    x.r[35].grade = MJ_CREDIT_SUCCESS;                   /* move 3 on state 35: P's set says not yet */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &x;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    CHECK(strstr(g.why, "two ways") != NULL);
    mj_hemi_set_free(&x);
    /* a recorded move that is not the one its distribution picks */
    gate_set(&x, &b, MJ_HEMI_HOLDOUT, 2, C, C, m30, m30);
    x.r[33].actor_move = 0;
    x.r[33].grade = MJ_CREDIT_SUCCESS;
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &x;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    CHECK(strstr(g.why, "distribution") != NULL);
    mj_hemi_set_free(&x);
    /* two sets that claim the same model played differently */
    gate_set(&x, &b, MJ_HEMI_HOLDOUT, 2, C, W, m35, m35);
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, C, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    in.holdout[in.n_holdout++] = &hc;
    in.holdout[in.n_holdout++] = &x;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    mj_hemi_set_free(&x);
    /* the candidate is the parent */
    gate_in(&in, &b, MJ_HEMI_GATE_ACTOR, P, P, 0);
    in.lessons[in.n_lessons++] = &lesson;
    in.holdout[in.n_holdout++] = &hp;
    CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
    mj_hemi_set_free(&lesson);

    /* watcher: parent P and candidate C both watch actor A on the held-out states */
    {
        mj_hemi_set jp, jc, ha;
        gate_set(&lesson, &b, MJ_HEMI_LESSON, 1, A, P, m20, m20);
        gate_set(&ha, &b, MJ_HEMI_HOLDOUT, 2, A, A, m20, m20);
        gate_set(&jp, &b, MJ_HEMI_HOLDOUT, 2, A, P, m20, m20);       /* agrees with everything: 50% right */
        gate_set(&jc, &b, MJ_HEMI_HOLDOUT, 2, A, C, m20, all_good);  /* objects exactly to the bad moves */
        gate_in(&in, &b, MJ_HEMI_GATE_WATCHER, P, C, 0);
        in.lessons[in.n_lessons++] = &lesson;
        in.holdout[in.n_holdout++] = &hp;
        in.holdout[in.n_holdout++] = &hc;
        in.holdout[in.n_holdout++] = &ha;
        in.holdout[in.n_holdout++] = &jp;
        in.holdout[in.n_holdout++] = &jc;
        CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_OK);
        CHECK(g.keep == 1 && g.judge_before_bp == 5000 && g.judge_after_bp == 10000 && g.after_bp >= g.before_bp);
        /* without the candidate's judgments nothing is decided */
        in.n_holdout = 4;
        CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
        mj_hemi_set_free(&jp);
        mj_hemi_set_free(&jc);
        mj_hemi_set_free(&ha);
        mj_hemi_set_free(&lesson);
    }

    /* swap: the child must beat both parents */
    {
        mj_hemi_set l1, l2;
        gate_set(&l1, &b, MJ_HEMI_LESSON, 1, P, O, m20, m20);
        gate_set(&l2, &b, MJ_HEMI_LESSON, 1, O, P, m20, m20);
        gate_set(&ho, &b, MJ_HEMI_HOLDOUT, 2, O, O, m30, m30);
        gate_set(&x, &b, MJ_HEMI_HOLDOUT, 2, C, C, m35, m35);
        gate_in(&in, &b, MJ_HEMI_GATE_SWAP, P, C, O);
        in.lessons[in.n_lessons++] = &l1;
        in.lessons[in.n_lessons++] = &l2;
        in.holdout[in.n_holdout++] = &hp;
        in.holdout[in.n_holdout++] = &ho;
        in.holdout[in.n_holdout++] = &x;
        CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_OK);
        CHECK(g.keep == 1 && g.before_bp == 5000 && g.other_bp == 7500 && g.after_bp == 8750);
        /* a child that only ties the stronger parent is rolled back */
        in.holdout[2] = &hc;
        CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_OK);
        CHECK(g.keep == 0 && g.after_bp == 7500);
        /* one pass alone is not a swap */
        in.n_lessons = 1;
        CHECK_ST(mj_hemi_gate_decide(&in, &g), MJ_E_INVAL);
        mj_hemi_set_free(&l1);
        mj_hemi_set_free(&l2);
        mj_hemi_set_free(&ho);
        mj_hemi_set_free(&x);
    }
    mj_hemi_set_free(&hp);
    mj_hemi_set_free(&hc);
    CHECK_EQ(b.used, (size_t)0);
}

int main(void) {
    test_judge();
    test_format();
    test_combine();
    test_merge_train();
    test_rounds();
    test_rollback();
    test_watcher_targets();
    test_classroom();
    test_gate();
    test_gpu_parity();
    return mj_t_done("test_hemi");
}
