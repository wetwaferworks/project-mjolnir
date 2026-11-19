/* Mjolnir: the classroom, stage 1 of the hemispheres.
 *
 * The left hemisphere acts in a world while the right one watches, grades and
 * flags mistakes; every verdict carries a score, a confidence and a reason.
 * Mjolnir records the state, the move, the verdict and the answer key's grade,
 * then finds the two kinds of disagreement that matter:
 *   - the actor erred (the key failed the move): the actor learns from it,
 *     with the watcher's own move as a hint where the watcher has earned
 *     trust;
 *   - the watcher was wrong (it objected to a move the key cleared, or agreed
 *     with a move the key failed): the watcher is sharpened on exactly those
 *     misses, with its confirmed judgments mixed in so it does not forget.
 * Each update is made on a copy and kept only when it is better on worlds
 * neither side has seen: the actor by its score, the watcher by how often the
 * answer key confirms its judgments (and not at the price of its own play).
 * The answer key is always the final judge.
 *
 * The pipeline runs the classroom first; its improved left and right become
 * the parents of stage 2, the hemisphere swap (mj_hemi.h). The idea is Jaydee
 * Wetwork's; teacher-student and actor-critic learning are published
 * background, and this is a clean-room build from the papers.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_CLASSROOM_H
#define MJ_CLASSROOM_H

#include "mjolnir/mj_hemi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mj_classroom_cfg {
    uint32_t round;
    uint64_t world_seed, holdout_seed;
    uint32_t mazes, holdout_mazes, segments;
    mj_hemi_combine_cfg actor_lessons; /* targets for the actor, as in stage 2 */
    float watcher_miss_weight;         /* a watcher miss: 1.0 */
    float watcher_keep_weight;         /* a confirmed judgment, so nothing is forgotten: 0.25 */
    mj_hemi_train_cfg train;
    int use_gpu;
} mj_classroom_cfg;
void mj_classroom_default(mj_classroom_cfg *c);

typedef struct mj_classroom_report {
    uint32_t round;
    mj_hemi_tally tally;                 /* the lesson: left acts, right watches */
    uint32_t actor_targets, watcher_targets;
    uint32_t actor_before_bp, actor_after_bp;          /* holdout score of the left */
    uint32_t watcher_judge_before_bp, watcher_judge_after_bp; /* judgments the key confirms, holdout */
    uint32_t watcher_play_before_bp, watcher_play_after_bp;   /* the right's own holdout score */
    int actor_kept, watcher_kept;
    uint8_t lesson_sha[MJ_SHA256_LEN];
    char why[MJ_TEXT_MAX + 1];
} mj_classroom_report;

/* Targets for the watcher from one lesson: where it objected to a move the
 * key cleared, the move becomes more likely for it; where it agreed with a
 * move the key failed, less likely; its confirmed judgments are kept with a
 * small weight. Free with mj_bfree(b, *out, n_records * sizeof **out). */
mj_status mj_classroom_watcher_targets(const mj_hemi_set *lesson, float miss_weight, float keep_weight, mj_budget *b,
                                       mj_hemi_target **out, uint32_t *n_out);

/* How often the key confirms a watcher's judgments in a set, basis points
 * (agreed with a cleared move, or objected to a failed one). */
uint32_t mj_classroom_judge_bp(const mj_hemi_tally *t);

/* One classroom round on toy policies, in dir/classroom-NNN/. *left and
 * *right change only when their update is kept; otherwise they are restored
 * from their checksummed snapshots. A journal line goes into ledger_path. */
mj_status mj_classroom_toy(const char *dir, const char *ledger_path, const mj_toy_model *teacher, mj_toy_model *left,
                           mj_toy_model *right, const mj_classroom_cfg *cfg, mj_budget *b, mj_classroom_report *rep);

#ifdef __cplusplus
}
#endif
#endif
