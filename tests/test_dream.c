/* The nightly driver and the nap entry point, end to end, dry run only.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_dream.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_synth.h"
#include "mj_test.h"

static mj_synth_material M;

static void material(mj_dream_material *d) {
    d->new_runs = M.new_runs; d->n_new = M.n_new;
    d->old_runs = M.old_runs; d->n_old = M.n_old;
    d->golden = M.golden; d->n_golden = M.n_golden;
    d->eval = M.eval; d->n_eval = M.n_eval;
}

static uint64_t ledger_count(const char *path) {
    mj_budget b;
    mj_ledger_state st;
    uint64_t bad;
    mj_budget_init(&b, 64u << 20);
    if (mj_ledger_verify_file(path, &b, &st, &bad) != MJ_OK) return UINT64_MAX;
    return st.count;
}

int main(void) {
    char root[MJ_PATH_MAX], ledger[MJ_PATH_MAX], head[MJ_ID_MAX + 1];
    mj_dream_cfg cfg;
    mj_dream_material dm;
    mj_dream_result r;
    mj_retain_report rr;
    mj_toy_model g;
    mj_budget b;
    mj_store s;
    mj_version_info vi, gi;
    uint32_t i;

    mj_t_dir(root, sizeof root, "dream_store");
    mj_t_dir(ledger, sizeof ledger, "dream_ledger.mjl");
    mj_t_clean(root);
    (void)mj_fs_remove(ledger);
    mj_synth_genesis(&g);
    CHECK_ST(mj_synth_build(&g, &M), MJ_OK);
    material(&dm);

    mj_dream_default_cfg(&cfg);
    cfg.store_root = root;
    cfg.ledger_path = ledger;
    cfg.when = "2026-11-16T17:42:00Z";
    cfg.limits.min_free_bytes = 0;

    /* no live weights yet: the dream refuses */
    cfg.dream_id = "nap-0";
    CHECK_ST(mj_take_a_nap(&cfg, &dm, &r), MJ_E_NOTFOUND);
    CHECK_ST(mj_dream_seed_toy(root, &cfg.limits, head), MJ_OK);
    CHECK_ST(mj_dream_seed_toy(root, &cfg.limits, head), MJ_E_EXISTS);

    /* a live run is refused outright, and nothing is written */
    cfg.dry_run = 0;
    cfg.dream_id = "live-1";
    CHECK_ST(mj_take_a_nap(&cfg, &dm, &r), MJ_E_FORBIDDEN);
    CHECK_EQ(ledger_count(ledger), 0u);
    cfg.dry_run = 1;

    /* take a nap: new verified experience, dry run */
    cfg.dream_id = "nap-1";
    CHECK_ST(mj_take_a_nap(&cfg, &dm, &r), MJ_OK);
    CHECK_ST(r.verdict, MJ_OK);
    CHECK(strcmp(r.outcome, "dry-run-pass") == 0);
    CHECK(strcmp(r.head, "toy-genesis") == 0);
    CHECK(strcmp(r.snapshot, "nap-1-pre") == 0);
    CHECK(strcmp(r.candidate, "nap-1-cand") == 0);
    CHECK_EQ(r.ledger_seq, 1u);
    CHECK(!r.adapted);

    mj_budget_init(&b, 64u << 20);
    CHECK_ST(mj_store_open(&s, root, &cfg.limits, &b), MJ_OK);
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "toy-genesis") == 0); /* the dry run left HEAD alone */
    CHECK_ST(mj_store_info(&s, "nap-1-pre", &vi), MJ_OK);
    CHECK_ST(mj_store_info(&s, "toy-genesis", &gi), MJ_OK);
    CHECK(memcmp(vi.sha256, gi.sha256, 32) == 0); /* snapshot is the live weights */
    CHECK(vi.kind == MJ_KIND_SNAPSHOT && strcmp(vi.parent, "toy-genesis") == 0);
    CHECK_ST(mj_store_info(&s, "nap-1-cand", &vi), MJ_OK);
    CHECK(vi.kind == MJ_KIND_CANDIDATE && strcmp(vi.parent, "nap-1-pre") == 0);

    /* a nap needs new experience; a dream id is used once */
    {
        mj_dream_material empty = dm;
        empty.n_new = 0;
        cfg.dream_id = "nap-2";
        CHECK_ST(mj_take_a_nap(&cfg, &empty, &r), MJ_E_INVAL);
    }
    cfg.dream_id = "nap-1";
    CHECK_ST(mj_take_a_nap(&cfg, &dm, &r), MJ_E_EXISTS);
    CHECK_EQ(ledger_count(ledger), 1u);

    /* a careless dream (no old replay) is rejected, with a note */
    cfg.dream_id = "nap-careless";
    cfg.update.replay_old_per_new = 0;
    {
        mj_dream_material no_old = dm;
        no_old.n_old = 0;
        CHECK_ST(mj_take_a_nap(&cfg, &no_old, &r), MJ_OK);
        CHECK_ST(r.verdict, MJ_E_GATE);
        CHECK(strcmp(r.outcome, "rejected") == 0);
        CHECK(strstr(r.rep.why, "shift gate") != NULL);
        /* decision 10: the next dream reads the note and adapts */
        cfg.dream_id = "nap-after";
        CHECK_ST(mj_take_a_nap(&cfg, &no_old, &r), MJ_OK);
        CHECK(r.adapted);
    }
    cfg.update.replay_old_per_new = 1;
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "toy-genesis") == 0);

    /* the time ceiling aborts a dream cleanly and records it */
    cfg.dream_id = "nap-slow";
    cfg.update.max_steps = 5;
    CHECK_ST(mj_take_a_nap(&cfg, &dm, &r), MJ_OK);
    CHECK_ST(r.verdict, MJ_E_TIMEOUT);
    CHECK(strcmp(r.outcome, "aborted") == 0);
    CHECK(strcmp(r.candidate, "-") == 0);
    cfg.update.max_steps = 1u << 20;

    /* twenty nights: retention keeps 14 of each kind, the milestone and HEAD */
    for (i = 0; i < 20; ++i) {
        char id[32];
        snprintf(id, sizeof id, "night-%02u", i);
        cfg.dream_id = id;
        CHECK_ST(mj_nightly(&cfg, &dm, &r, &rr), MJ_OK);
        CHECK(strcmp(r.outcome, "dry-run-pass") == 0);
    }
    CHECK_ST(mj_store_open(&s, root, &cfg.limits, &b), MJ_OK);
    CHECK_ST(mj_store_info(&s, "toy-genesis", &vi), MJ_OK);
    CHECK_ST(mj_store_info(&s, "night-19-cand", &vi), MJ_OK);
    CHECK_ST(mj_store_info(&s, "nap-1-pre", &vi), MJ_E_NOTFOUND); /* retired */
    {
        uint32_t checked = 0;
        CHECK_ST(mj_store_verify_all(&s, &checked), MJ_OK);
        CHECK_EQ(checked, 1u + 14u + 14u); /* milestone + 14 snapshots + 14 candidates */
    }
    CHECK_EQ(ledger_count(ledger), 24u); /* 4 naps + 20 nights */

    /* a broken ledger stops all dreaming */
    {
        uint8_t *buf;
        size_t n;
        CHECK_ST(mj_fs_read_all(ledger, 1u << 20, &b, &buf, &n), MJ_OK);
        buf[200] ^= 1;
        (void)mj_fs_remove(ledger);
        CHECK_ST(mj_fs_write_atomic(ledger, buf, n, 0), MJ_OK);
        cfg.dream_id = "after-tamper";
        CHECK_ST(mj_nightly(&cfg, &dm, &r, &rr), MJ_E_CHAIN);
        buf[200] ^= 1;
        (void)mj_fs_remove(ledger);
        CHECK_ST(mj_fs_write_atomic(ledger, buf, n, 0), MJ_OK);
        mj_bfree(&b, buf, n);
        CHECK_ST(mj_nightly(&cfg, &dm, &r, &rr), MJ_OK);
    }

    /* corrupted live weights stop the dream before anything is dreamed */
    {
        char p[MJ_PATH_MAX], hex[MJ_SHA256_HEX];
        uint8_t *buf;
        size_t n;
        CHECK_ST(mj_store_info(&s, "toy-genesis", &gi), MJ_OK);
        mj_hex_encode(gi.sha256, 32, hex);
        snprintf(p, sizeof p, "%s/objects/%s.blob", root, hex);
        CHECK_ST(mj_fs_read_all(p, 1u << 20, &b, &buf, &n), MJ_OK);
        buf[30] ^= 1;
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, buf, n, 0), MJ_OK);
        cfg.dream_id = "bad-weights";
        CHECK_ST(mj_nightly(&cfg, &dm, &r, &rr), MJ_E_CORRUPT);
        buf[30] ^= 1;
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, buf, n, 0), MJ_OK);
        mj_bfree(&b, buf, n);
    }
    CHECK_EQ(ledger_count(ledger), 25u);
    CHECK_EQ(b.used, 0u);
    return mj_t_done("test_dream");
}
