/* Mjolnir: a nap for the toy policy, trained from real replays.
 *
 * The C version of python/mjolnir/toytrain.py. The dream follows the M0
 * cycle on the toy model:
 *   1. load and check the live toy weights (HEAD of the toy store)
 *   2. snapshot them, so the dream can always be rolled back
 *   3. build the dream batch: replay steps with the grader's label where one
 *      was given, otherwise the automatic partial-credit label, with older
 *      memories (the golden set) mixed in after every new sample (design
 *      principle 5)
 *   4. train a float copy: raise the likelihood of what was productive and
 *      lower the likelihood of what was not (design principle 3), clamping
 *      every weight to the step cap after every step (design principle 4)
 *   5. round to Q16 and run the four M0 gates: step cap, behavior shift on
 *      the golden set, golden score not lower, task score higher
 *   6. save the candidate and write the ledger line; HEAD only moves when the
 *      caller asks to integrate and every gate passed
 *
 * The training runs on the processor: the model has 32 weights, so a GPU
 * would only add transfer time. It is the same loss, SGD with momentum and
 * clamping as the Python version, in float32, but the minibatches come from a
 * small documented generator (splitmix64) instead of PyTorch's, so the two
 * produce different (equally valid) candidates. Feed both the same minibatch
 * indices and they agree to within float rounding; the parity test does that.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_TOYTRAIN_H
#define MJ_TOYTRAIN_H
#include "mjolnir/mj_common.h"
#include "mjolnir/mj_grades.h"
#include "mjolnir/mj_replay.h"
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_toy.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MJ_TT_MAX_SAMPLES (1u << 22)

typedef struct mj_tt_sample {
    int8_t x[MJ_TOY_F];
    uint8_t action;
    float reward;
    uint8_t by_grader; /* 1 when the reward came from a grader's label */
    uint32_t step;     /* the step's index in its run */
} mj_tt_sample;

typedef struct mj_tt_set {
    mj_tt_sample *s;
    uint32_t n, cap;
} mj_tt_set;
void mj_tt_set_free(mj_tt_set *s);

/* toy.features: one feature vector per step that took one of the four moves,
 * appended to out with reward 0. Where Python would stop with an uncaught
 * error (an "open" that is a non-empty list, or text that is not a number),
 * this refuses and msg says why. */
mj_status mj_tt_features(const mj_replay_run *run, mj_tt_set *out, char msg[96]);

/* build_batch and golden_from. *sources gets the sorted ids of the runs that
 * gave samples (pointers into the runs; free the array with free()). */
mj_status mj_tt_build_batch(const mj_replay_run *const *runs, uint32_t n_runs, const mj_grade_map *grades,
                            mj_tt_set *samples, char ***sources, uint32_t *n_sources, char msg[96]);
mj_status mj_tt_golden(const mj_replay_run *const *runs, uint32_t n_runs, mj_tt_set *gold, char msg[96]);

uint32_t mj_tt_task_bp(const mj_toy_model *w, const mj_tt_set *samples);
uint32_t mj_tt_golden_bp(const mj_toy_model *w, const mj_tt_set *gold);
uint32_t mj_tt_shift_bp(const mj_toy_model *w0, const mj_toy_model *w1, const mj_tt_set *gold);

typedef struct mj_tt_cfg {
    uint32_t steps;            /* 300 */
    float lr;                  /* 0.5 */
    float step_cap;            /* 0.5 */
    uint32_t replay_old_per_new; /* 1 */
    uint64_t seed;             /* 1 */
    uint32_t max_shift_bp;     /* 2000 */
} mj_tt_cfg;
void mj_tt_default_cfg(mj_tt_cfg *c);

/* The minibatch generator: splitmix64 from seed, index = value mod n. */
uint64_t mj_tt_next(uint64_t *state);

/* Train a copy of w0. When idx is non-NULL it gives the minibatch indices
 * (steps x min(256, n) of them) instead of the generator. */
mj_status mj_tt_train(const mj_toy_model *w0, const mj_tt_set *samples, const mj_tt_set *gold, const mj_tt_cfg *cfg,
                      const uint32_t *idx, mj_toy_model *w1, float *loss);

typedef struct mj_tt_result {
    char dream[MJ_ID_MAX + 1];
    char outcome[24];
    int gate_step, gate_shift, gate_golden, gate_task;
    uint32_t task_before, task_after, golden_before, golden_after, shift_bp;
    int64_t max_move_q16;
    uint32_t samples, graded, gold;
    uint64_t ledger_seq;
    double seconds;
    char candidate[MJ_ID_MAX + 1];
    char live[MJ_ID_MAX + 1];
} mj_tt_result;

/* One nap. The store must have been opened with the budget b. learn are the
 * runs to dream on, hold the golden runs. dream_id may
 * be NULL ("toy-nap-<UTC stamp>"). msg explains a refusal in Python's words. */
mj_status mj_tt_nap(mj_store *store, const char *ledger_path, mj_budget *b, const mj_replay_run *const *learn,
                    uint32_t n_learn, const mj_replay_run *const *hold, uint32_t n_hold, const mj_grade_map *grades,
                    const char *dream_id, int integrate, const mj_tt_cfg *cfg, mj_tt_result *res, char msg[160]);

/* split_runs: sorted by id (stable), every holdout_every-th from the first
 * goes to hold. The arrays point into runs. */
mj_status mj_tt_split(const mj_replay_run *runs, uint32_t n, uint32_t holdout_every, const mj_replay_run ***learn,
                      uint32_t *n_learn, const mj_replay_run ***hold, uint32_t *n_hold);

#ifdef __cplusplus
}
#endif
#endif
