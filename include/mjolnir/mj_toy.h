/* Mjolnir M0: the toy model and the update-rule core.
 *
 * Stage M0 never touches real model weights. Everything here runs on a tiny
 * synthetic policy: 8 input features, 4 actions, fixed-point integer weights
 * (Q16: 65536 = 1.0). It exists to prove the dreaming rules before any real
 * weights are allowed near them:
 *   - reward-weighted small nudges (decisions 3 and 4)
 *   - a hard cap on how far any weight may move in one dream
 *   - old memories mixed in with new ones (decision 5)
 *   - a maximum behavior shift on a golden set, and a no-forgetting floor
 * All math is integer and the sample order is fixed, so a dream gives the same
 * candidate bit for bit on every machine.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_TOY_H
#define MJ_TOY_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_score.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_TOY_F 8u
#define MJ_TOY_A 4u
#define MJ_TOY_BLOB_LEN (8u + 4u + MJ_TOY_A * MJ_TOY_F * 4u + 4u)
#define MJ_TOY_MAX_SEGMENTS 64u
#define MJ_TOY_MAX_SAMPLES 65536u
#define MJ_TOY_WEIGHT_LIMIT (64 * 65536) /* |w| <= 64.0, keeps logits inside int64 easily */

typedef struct mj_toy_model {
    int32_t w[MJ_TOY_A][MJ_TOY_F];
} mj_toy_model;

uint8_t mj_toy_act(const mj_toy_model *m, const int8_t x[MJ_TOY_F]); /* argmax, ties to the lower action */

/* Serialized form: "MJTOY1\0\0", F and A as bytes, two zero bytes, the weights
 * little-endian row by row, then a CRC-32 of everything before it. */
mj_status mj_toy_save(const mj_toy_model *m, uint8_t out[MJ_TOY_BLOB_LEN]);
mj_status mj_toy_load(const uint8_t *buf, size_t n, mj_toy_model *m);

/* A toy maze: a corridor of segments. Each has a state the model sees and the
 * one action that clears it. */
typedef struct mj_toy_maze {
    uint32_t n;
    int8_t x[MJ_TOY_MAX_SEGMENTS][MJ_TOY_F];
    uint8_t correct[MJ_TOY_MAX_SEGMENTS];
    uint8_t type[MJ_TOY_MAX_SEGMENTS]; /* mj_seg_type */
} mj_toy_maze;

/* A recorded run through a maze: the action taken at each reached segment and
 * the result. This is the dream material: a replay, not a re-run. */
typedef struct mj_toy_run {
    char id[MJ_ID_MAX + 1];
    uint32_t n;
    int8_t x[MJ_TOY_MAX_SEGMENTS][MJ_TOY_F];
    uint8_t taken[MJ_TOY_MAX_SEGMENTS];
    mj_maze_segment seg[MJ_TOY_MAX_SEGMENTS];
} mj_toy_run;

/* Play a maze with a model in the toy simulator, stopping at the first wrong
 * action. Produces a recorded run and its partial-credit score. */
mj_status mj_toy_play(const mj_toy_model *m, const mj_toy_maze *mz, const char *run_id, mj_toy_run *run,
                      mj_maze_score *score);

/* Partial-credit score of a model over a set of mazes, in basis points. */
mj_status mj_toy_eval(const mj_toy_model *m, const mj_toy_maze *mz, uint32_t n, uint32_t *score_bp);

typedef struct mj_toy_golden {
    int8_t x[MJ_TOY_F];
    uint8_t expected;
} mj_toy_golden;

typedef struct mj_update_cfg {
    int32_t lr;               /* Q16 weight units per unit of reward x feature/127 */
    int32_t step_cap;         /* max |candidate - live| per weight, Q16 */
    uint32_t max_shift_bp;    /* max golden actions allowed to change, basis points */
    uint32_t replay_old_per_new; /* old memories mixed in after each new sample */
    uint32_t epochs;          /* passes over the mixed batch */
    uint32_t max_steps;       /* step ceiling */
    uint64_t max_ms;          /* wall-clock ceiling, 0 = none */
    int32_t reward_success;   /* reward for a cleared segment (bp, > 0) */
    int32_t reward_not_yet;   /* reward for the failed segment (bp, < 0) */
} mj_update_cfg;

void mj_update_default_cfg(mj_update_cfg *c);

typedef struct mj_update_report {
    uint32_t samples_new, samples_old, steps;
    int32_t max_abs_delta;
    uint32_t shift_bp;
    uint32_t golden_before_bp, golden_after_bp;
    uint32_t task_before_bp, task_after_bp;
    int gate_step, gate_shift, gate_golden, gate_improve; /* 1 = pass */
    char gates[MJ_TEXT_MAX + 1];
    char why[MJ_TEXT_MAX + 1]; /* failure note, empty on pass */
} mj_update_report;

/* One dream on toy weights. Builds reward-weighted samples from the new runs
 * (cleared segments strengthen the action taken, the "not yet" segment weakens
 * it, unreached segments are skipped), mixes in the old runs, nudges a copy of
 * the live weights, then checks the gates:
 *   step:    no weight moved more than step_cap
 *   shift:   golden actions changed by at most max_shift_bp
 *   golden:  golden accuracy did not drop (no forgetting)
 *   improve: the task score on eval mazes went up
 * Returns MJ_OK with *candidate set when every gate passes, MJ_E_GATE when one
 * fails (the candidate is still written for the record, never applied), or
 * another error, in which case the candidate is a copy of live. */
mj_status mj_update_dream(const mj_toy_model *live, const mj_toy_run *new_runs, uint32_t n_new,
                          const mj_toy_run *old_runs, uint32_t n_old, const mj_toy_golden *golden, uint32_t n_golden,
                          const mj_toy_maze *eval, uint32_t n_eval, const mj_update_cfg *cfg,
                          mj_toy_model *candidate, mj_update_report *rep);

/* Check a recorded run's shape (used for every replay before a dream). */
mj_status mj_toy_run_validate(const mj_toy_run *r);

#ifdef __cplusplus
}
#endif
#endif
