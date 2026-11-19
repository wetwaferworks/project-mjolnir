/* The classroom, stage 1 of the hemispheres. See mj_classroom.h.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_classroom.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mj_hemi_internal.h"

#include <stdio.h>
#include <string.h>

void mj_classroom_default(mj_classroom_cfg *c) {
    memset(c, 0, sizeof *c);
    c->mazes = 512;
    c->holdout_mazes = 2048;
    c->segments = 16;
    mj_hemi_combine_default(&c->actor_lessons);
    c->watcher_miss_weight = 1.0f;
    c->watcher_keep_weight = 0.25f;
    mj_hemi_train_default(&c->train);
}

uint32_t mj_classroom_judge_bp(const mj_hemi_tally *t) {
    uint64_t judged = (uint64_t)t->agree_right + t->agree_wrong + t->object_right + t->object_wrong;
    uint64_t right = (uint64_t)t->agree_right + t->object_right;
    return judged ? (uint32_t)(right * 10000u / judged) : 0u;
}

mj_status mj_classroom_watcher_targets(const mj_hemi_set *lesson, float miss_weight, float keep_weight, mj_budget *b,
                                       mj_hemi_target **out, uint32_t *n_out) {
    uint32_t i, k = 0;
    mj_hemi_target *t;
    if (lesson == NULL || out == NULL || n_out == NULL || mj_hemi_header_check(&lesson->h) != MJ_OK) return MJ_E_INVAL;
    *out = NULL;
    *n_out = 0;
    if (lesson->n == 0) return MJ_OK;
    t = (mj_hemi_target *)mj_balloc(b, (size_t)lesson->n * sizeof *t);
    if (t == NULL) return MJ_E_LIMIT;
    for (i = 0; i < lesson->n; ++i) {
        const mj_hemi_record *r = &lesson->r[i];
        const int cleared = r->grade == MJ_CREDIT_SUCCESS, failed = r->grade == MJ_CREDIT_NOT_YET;
        const int agreed = r->verdict.reason == MJ_HEMI_AGREE;
        /* counted the way mj_hemi_tally_set counts them; an unsure verdict
         * judged nothing and teaches nothing */
        const int objected = r->verdict.reason == MJ_HEMI_PREFER_OTHER || r->verdict.reason == MJ_HEMI_ILLEGAL;
        float w = 0.0f;
        if (objected && cleared) w = miss_weight;        /* it doubted a good move */
        else if (agreed && failed) w = -miss_weight;     /* it backed a bad one */
        else if (agreed && cleared) w = keep_weight;     /* confirmed: keep it */
        else if (objected && failed) w = -keep_weight;   /* confirmed: keep doubting it */
        if (w == 0.0f) continue;
        memcpy(t[k].features, r->features, MJ_HEMI_MAX_FEATURES);
        t[k].move = r->actor_move;
        t[k].from_watcher = 1;
        t[k].weight = w;
        ++k;
    }
    *out = t;
    *n_out = k;
    return MJ_OK;
}

/* the share of a watcher's judgments the key confirms, on a world, with a
 * fixed actor */
static mj_status judge_score(const mj_hemi_toy_world *w, const mj_toy_model *actor, const mj_toy_model *watcher,
                             uint32_t round, mj_budget *b, uint32_t *out) {
    mj_hemi_toy at, wt;
    mj_hemi_model am, wm;
    mj_hemi_header h;
    mj_hemi_set s;
    mj_hemi_tally t;
    mj_status st;
    at.m = *actor;
    wt.m = *watcher;
    if ((st = mj_hemi_toy_bind(&at, "actor", &am)) != MJ_OK || (st = mj_hemi_toy_bind(&wt, "watcher", &wm)) != MJ_OK)
        return st;
    memset(&h, 0, sizeof h);
    h.round = round; h.pass = 1; h.purpose = MJ_HEMI_LESSON; h.actor_side = MJ_HEMI_LEFT; h.world = MJ_HEMI_WORLD_TOY_MAZE;
    h.n_features = MJ_TOY_F; h.n_actions = MJ_TOY_A;
    mj_hemi_set_init(&s, b, &h);
    st = mj_hemi_toy_pass(w, &am, &wm, &s);
    if (st == MJ_OK) {
        mj_hemi_tally_set(&s, &t);
        *out = mj_classroom_judge_bp(&t);
    }
    mj_hemi_set_free(&s);
    return st;
}

mj_status mj_classroom_toy(const char *dir, const char *ledger_path, const mj_toy_model *teacher, mj_toy_model *left,
                           mj_toy_model *right, const mj_classroom_cfg *cfg, mj_budget *b, mj_classroom_report *rep) {
    char rdir[MJ_PATH_MAX], name[32], path[MJ_PATH_MAX];
    uint8_t left_sha[MJ_SHA256_LEN], right_sha[MJ_SHA256_LEN], new_sha[2][MJ_SHA256_LEN];
    mj_hemi_toy_world world, hold;
    mj_hemi_toy lt, rt;
    mj_hemi_model lm, rm;
    mj_hemi_header h;
    mj_hemi_set lesson;
    const mj_hemi_set *sp[1];
    mj_hemi_target *at = NULL, *wt = NULL;
    uint32_t n_at = 0, n_wt = 0, scores[4];
    mj_toy_model new_left, new_right, four[4];
    float loss;
    mj_status st;
    if (dir == NULL || ledger_path == NULL || teacher == NULL || left == NULL || right == NULL || cfg == NULL ||
        b == NULL || rep == NULL)
        return MJ_E_INVAL;
    memset(rep, 0, sizeof *rep);
    rep->round = cfg->round;
    snprintf(name, sizeof name, "classroom-%03u", (unsigned)cfg->round);
    if ((st = mj_path_join(rdir, sizeof rdir, dir, name)) != MJ_OK) return st;
    if ((st = mj_fs_mkdir(dir)) != MJ_OK || (st = mj_fs_mkdir(rdir)) != MJ_OK) return st;
    if ((st = mj_hemi_save_toy(rdir, "left.mjtoy", left, left_sha)) != MJ_OK) return st;
    if ((st = mj_hemi_save_toy(rdir, "right.mjtoy", right, right_sha)) != MJ_OK) return st;

    st = mj_hemi_toy_world_make(cfg->world_seed, cfg->mazes, cfg->segments, teacher, b, &world);
    if (st != MJ_OK) return st;
    st = mj_hemi_toy_world_make(cfg->holdout_seed, cfg->holdout_mazes, cfg->segments, teacher, b, &hold);
    if (st != MJ_OK) { mj_hemi_toy_world_free(&world); return st; }

    /* the lesson: left acts, right watches, grades and flags */
    lt.m = *left;
    rt.m = *right;
    memset(&h, 0, sizeof h);
    h.round = cfg->round; h.pass = 1; h.purpose = MJ_HEMI_LESSON; h.actor_side = MJ_HEMI_LEFT; h.world = MJ_HEMI_WORLD_TOY_MAZE;
    h.n_features = MJ_TOY_F; h.n_actions = MJ_TOY_A; h.seed = cfg->world_seed;
    mj_hemi_set_init(&lesson, b, &h);
    if ((st = mj_hemi_toy_bind(&lt, "left", &lm)) != MJ_OK || (st = mj_hemi_toy_bind(&rt, "right", &rm)) != MJ_OK)
        goto out;
    if ((st = mj_hemi_toy_pass(&world, &lm, &rm, &lesson)) != MJ_OK) goto out;
    if ((st = mj_path_join(path, sizeof path, rdir, "lesson.mjhemi")) != MJ_OK) goto out;
    (void)mj_fs_remove(path);
    if ((st = mj_hemi_save(&lesson, path, rep->lesson_sha)) != MJ_OK) goto out;
    mj_hemi_tally_set(&lesson, &rep->tally);
    sp[0] = &lesson;
    if ((st = mj_path_join(path, sizeof path, rdir, "disagreements.tsv")) != MJ_OK) goto out;
    if ((st = mj_hemi_write_disagreements(path, sp, 1)) != MJ_OK) goto out;

    /* the actor learns from its errors (the watcher's hints count as far as
     * it has earned trust); the watcher learns from its misses */
    if ((st = mj_hemi_combine(sp, 1, &cfg->actor_lessons, b, &at, &n_at)) != MJ_OK) goto out;
    if ((st = mj_classroom_watcher_targets(&lesson, cfg->watcher_miss_weight, cfg->watcher_keep_weight, b, &wt, &n_wt)) !=
        MJ_OK)
        goto out;
    rep->actor_targets = n_at;
    rep->watcher_targets = n_wt;
    if ((st = mj_hemi_toy_train(left, at, n_at, &cfg->train, &new_left, &loss)) != MJ_OK) goto out;
    if ((st = mj_hemi_toy_train(right, wt, n_wt, &cfg->train, &new_right, &loss)) != MJ_OK) goto out;
    if ((st = mj_hemi_save_toy(rdir, "left-candidate.mjtoy", &new_left, new_sha[0])) != MJ_OK) goto out;
    if ((st = mj_hemi_save_toy(rdir, "right-candidate.mjtoy", &new_right, new_sha[1])) != MJ_OK) goto out;

    /* the gates, on mazes neither side has seen */
    four[0] = *left; four[1] = new_left; four[2] = *right; four[3] = new_right;
    (void)mj_hemi_holdout_scores(&hold, four, 4, cfg->use_gpu, scores, b);
    rep->actor_before_bp = scores[0];
    rep->actor_after_bp = scores[1];
    rep->watcher_play_before_bp = scores[2];
    rep->watcher_play_after_bp = scores[3];
    /* the watcher is judged with the same actor (the old left) before and after */
    if ((st = judge_score(&hold, left, right, cfg->round, b, &rep->watcher_judge_before_bp)) != MJ_OK) goto out;
    if ((st = judge_score(&hold, left, &new_right, cfg->round, b, &rep->watcher_judge_after_bp)) != MJ_OK) goto out;
    rep->actor_kept = rep->actor_after_bp > rep->actor_before_bp;
    rep->watcher_kept = rep->watcher_judge_after_bp > rep->watcher_judge_before_bp &&
                        rep->watcher_play_after_bp >= rep->watcher_play_before_bp;
    snprintf(rep->why, sizeof rep->why,
             "actor %u->%u bp %s; watcher judgments %u->%u bp, own play %u->%u bp %s", (unsigned)scores[0],
             (unsigned)scores[1], rep->actor_kept ? "kept" : "restored", (unsigned)rep->watcher_judge_before_bp,
             (unsigned)rep->watcher_judge_after_bp, (unsigned)scores[2], (unsigned)scores[3],
             rep->watcher_kept ? "kept" : "restored");
    {
        mj_toy_model back;
        if (rep->actor_kept) *left = new_left;
        else if ((st = mj_hemi_restore_toy(rdir, "left.mjtoy", left_sha, b, &back)) != MJ_OK) goto out;
        else *left = back;
        if (rep->watcher_kept) *right = new_right;
        else if ((st = mj_hemi_restore_toy(rdir, "right.mjtoy", right_sha, b, &back)) != MJ_OK) goto out;
        else *right = back;
    }

    /* the journal */
    {
        mj_dream_record rec;
        char a[13], l[13], r[13], c1[13], c2[13], when[21];
        mj_ledger_record_init(&rec);
        snprintf(rec.dream_id, sizeof rec.dream_id, "classroom-%03u", (unsigned)cfg->round);
        if (mj_utc_now(when) == MJ_OK) memcpy(rec.when, when, sizeof when);
        memcpy(rec.trigger, "classroom", 10);
        memcpy(rec.grader, "answer-key", 11);
        mj_hemi_hex12(rep->lesson_sha, a);
        snprintf(rec.inputs, sizeof rec.inputs, "lesson-%s", a);
        mj_hemi_hex12(left_sha, l);
        mj_hemi_hex12(right_sha, r);
        mj_hemi_hex12(new_sha[0], c1);
        mj_hemi_hex12(new_sha[1], c2);
        snprintf(rec.weights_before, sizeof rec.weights_before, "left-%s", l);
        snprintf(rec.weights_candidate, sizeof rec.weights_candidate, "left-%s", c1);
        snprintf(rec.edit, sizeof rec.edit,
                 "actor %u targets; watcher right-%s to right-%s on %u targets (misses %u, shared mistakes %u)",
                 (unsigned)n_at, r, c2, (unsigned)n_wt, (unsigned)rep->tally.object_wrong,
                 (unsigned)rep->tally.agree_wrong);
        rec.score_before_bp = rep->actor_before_bp;
        rec.score_after_bp = rep->actor_after_bp;
        snprintf(rec.gates, sizeof rec.gates, "actor=%s,watcher=%s", rep->actor_kept ? "pass" : "fail",
                 rep->watcher_kept ? "pass" : "fail");
        if (rep->actor_kept || rep->watcher_kept) memcpy(rec.outcome, "dry-run-pass", 13);
        else memcpy(rec.outcome, "rolled-back", 12);
        snprintf(rec.notes, sizeof rec.notes, "%s", rep->why);
        st = mj_ledger_append(ledger_path, b, &rec);
    }

out:
    if (at) mj_bfree(b, at, (size_t)2u * lesson.n * sizeof *at);
    if (wt) mj_bfree(b, wt, (size_t)lesson.n * sizeof *wt);
    mj_hemi_set_free(&lesson);
    mj_hemi_toy_world_free(&world);
    mj_hemi_toy_world_free(&hold);
    return st;
}
