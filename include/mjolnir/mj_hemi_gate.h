/* mj_hemi_gate: the keep-or-roll-back decision for a round played outside
 * Mjolnir (PCHIE's pchie_hemispheres on a real model).
 *
 * Mjolnir cannot run a large model itself, so it does not take a score from
 * anyone. The world sends sealed sample sets instead, and Mjolnir works the
 * scores out from them:
 *
 *   - lesson sets (purpose MJ_HEMI_LESSON) are what the round learned from;
 *   - holdout sets (purpose MJ_HEMI_HOLDOUT) are the same held-out states
 *     played by each model the decision needs, every move graded by the
 *     answer key.
 *
 * Every set names the full SHA-256 of the model that acted and the model that
 * watched. The decision is only made when the sets are bound to the models
 * named as parent, candidate and other, when every holdout set covers the very
 * same states, when each recorded move is the one its recorded distribution
 * picks, and when the answer key gave the same grade to the same move on the
 * same state in every set. Anything else is refused, with the reason.
 *
 * Rules (the same as the toy stages):
 *   actor:   the candidate plays the holdout states better than the parent;
 *   watcher: the candidate's judgments of the lesson's actor are confirmed by
 *            the answer key more often than the parent's, and its own play
 *            is no worse;
 *   swap:    the child plays better than both parents.
 *
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJOLNIR_MJ_HEMI_GATE_H
#define MJOLNIR_MJ_HEMI_GATE_H

#include "mjolnir/mj_hemi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_HEMI_GATE_MAX_SETS 16u

typedef enum mj_hemi_gate_kind { MJ_HEMI_GATE_ACTOR = 1, MJ_HEMI_GATE_WATCHER = 2, MJ_HEMI_GATE_SWAP = 3 } mj_hemi_gate_kind;

typedef struct mj_hemi_gate_in {
    uint32_t kind;                                   /* mj_hemi_gate_kind */
    const mj_hemi_set *lessons[MJ_HEMI_GATE_MAX_SETS];
    uint32_t n_lessons;
    const mj_hemi_set *holdout[MJ_HEMI_GATE_MAX_SETS];
    uint32_t n_holdout;
    uint8_t parent[MJ_HEMI_FINGERPRINT];             /* the model the candidate would replace */
    uint8_t candidate[MJ_HEMI_FINGERPRINT];
    uint8_t other[MJ_HEMI_FINGERPRINT];              /* swap only: the second parent */
    mj_budget *budget;                               /* scratch for the answer-key check */
} mj_hemi_gate_in;

typedef struct mj_hemi_gate_out {
    int keep;
    uint32_t states;                 /* held-out states every model was scored on */
    uint32_t before_bp, after_bp;    /* parent and candidate, own play on the holdout states */
    uint32_t other_bp;               /* swap: the second parent */
    uint32_t judge_before_bp, judge_after_bp; /* watcher: judgments confirmed by the answer key */
    char why[200];                   /* the reason when the evidence is refused */
} mj_hemi_gate_out;

/* MJ_OK with out->keep set, or MJ_E_INVAL with out->why when the evidence does
 * not hold together (nothing is decided then), or MJ_E_LIMIT. */
mj_status mj_hemi_gate_decide(const mj_hemi_gate_in *in, mj_hemi_gate_out *out);

#ifdef __cplusplus
}
#endif
#endif
