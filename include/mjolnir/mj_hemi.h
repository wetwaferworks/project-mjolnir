/* Mjolnir: the left and right hemispheres.
 *
 * Two models take turns. In pass 1 the left hemisphere plays a world while the
 * right one watches every step and gives a verdict on the move; in pass 2 they
 * swap. Each pass writes a sample set: the state, the move, the watcher's
 * verdict and the answer key's grade. Mjolnir then combines both sets, weighted
 * by the answer key, into one new model, made on a working copy only. The new
 * model is kept only if it beats both parents on games neither has seen; then
 * it takes a side and the next round runs with the sides swapped. Otherwise the
 * parents are restored from their checksums and the journal says why.
 *
 * The answer key is always the final judge. A watcher's opinion only counts
 * where the answer key agrees with it, so two models cannot talk each other
 * into the same mistake.
 *
 * This header holds the parts that do not depend on any one model:
 *   - the hemisphere interface (a table of two functions, so any model that
 *     can give a distribution over the legal moves of a state can take part:
 *     the toy policy here, a transformer, a mixture of experts or a state-space
 *     model behind PCHIE's runtime);
 *   - the verdict rule;
 *   - the sample-set file format MJHEMI1 (see docs/FORMATS.md);
 *   - the combiner (targets from both sets, weighted by the answer key);
 *   - the round driver for the toy policy, with its keep-or-roll-back gate
 *     and the journal entry.
 *
 * No locks: one round runs on one thread; the only hand-offs are phase
 * boundaries (pass 1, pass 2, combine, evaluate, journal).
 * The idea is Jaydee Wetwork's. Mutual learning and distillation between two
 * models is published background; this is a clean-room build from the papers.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_HEMI_H
#define MJ_HEMI_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"
#include "mjolnir/mj_score.h"
#include "mjolnir/mj_toy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_HEMI_MAX_ACTIONS 16u
#define MJ_HEMI_MAX_FEATURES 64u
#define MJ_HEMI_MAX_RECORDS (1u << 20)
#define MJ_HEMI_HEADER_BYTES 96u
#define MJ_HEMI_TRAILER_BYTES 32u /* SHA-256 of everything before it */
#define MJ_HEMI_RECORD_FIXED 14u
#define MJ_HEMI_PROB_ONE 65535u   /* probabilities are stored as p * 65535, rounded */
#define MJ_HEMI_FINGERPRINT 16u   /* first 16 bytes of the model's SHA-256 */

typedef enum mj_hemi_side { MJ_HEMI_LEFT = 0, MJ_HEMI_RIGHT = 1 } mj_hemi_side;

typedef enum mj_hemi_world {
    MJ_HEMI_WORLD_TOY_MAZE = 1, /* Mjolnir's toy corridors */
    MJ_HEMI_WORLD_GRID_MAZE = 2,/* PCHIE's grid mazes (MazeGrid, BFS answer key) */
    MJ_HEMI_WORLD_CIRCUIT = 3
} mj_hemi_world;

typedef enum mj_hemi_reason {
    MJ_HEMI_AGREE = 1,        /* the watcher would have made the same move */
    MJ_HEMI_PREFER_OTHER = 2, /* the watcher is sure another move is better */
    MJ_HEMI_UNSURE = 3,       /* the watcher prefers another move, but only just */
    MJ_HEMI_ILLEGAL = 4       /* the move is not legal in this state */
} mj_hemi_reason;

/* Below this margin (basis points of probability between the watcher's top
 * two moves) a disagreement is "unsure" rather than "prefer other". */
#define MJ_HEMI_UNSURE_BP 1000u

/* ---- the hemisphere interface ---------------------------------------------
 * A state is a small feature vector (what the toy sees) plus an optional
 * opaque pointer for models that read something richer (text, frames). The
 * model fills probs[0..n_actions) with p * 65535 for the legal moves (bit a of
 * legal) and 0 for the rest; the sum is within n_actions of 65535. */
typedef struct mj_hemi_state {
    const int8_t *features;
    uint32_t n_features;
    uint32_t n_actions;
    uint32_t legal;      /* bit mask of legal moves */
    const void *rich;    /* model-specific view of the same state, may be NULL */
} mj_hemi_state;

typedef struct mj_hemi_model {
    void *self;
    mj_status (*distribution)(void *self, const mj_hemi_state *s, uint16_t probs[MJ_HEMI_MAX_ACTIONS]);
    uint8_t fingerprint[MJ_HEMI_FINGERPRINT];
    char name[MJ_ID_MAX + 1];
} mj_hemi_model;

/* The move a hemisphere makes: the most likely legal move, ties to the lower
 * index. Returns MJ_HEMI_MAX_ACTIONS when nothing is legal. */
uint32_t mj_hemi_pick(const uint16_t probs[MJ_HEMI_MAX_ACTIONS], uint32_t n_actions, uint32_t legal);

typedef struct mj_hemi_verdict {
    uint8_t choice;        /* the watcher's own move */
    uint8_t reason;        /* mj_hemi_reason */
    int16_t score_bp;      /* p(actor's move) - p(best other move), -10000..10000 */
    uint16_t confidence_bp;/* p(top) - p(second), 0..10000 */
} mj_hemi_verdict;

/* The verdict rule, integer only, so every machine gives the same verdict for
 * the same distribution. */
void mj_hemi_judge(const uint16_t watcher_probs[MJ_HEMI_MAX_ACTIONS], uint32_t n_actions, uint32_t legal,
                   uint32_t actor_move, mj_hemi_verdict *out);

/* ---- sample sets (MJHEMI1) ------------------------------------------------- */
typedef struct mj_hemi_header {
    uint32_t round;
    uint8_t pass;          /* 1 or 2 */
    uint8_t actor_side;    /* mj_hemi_side */
    uint8_t world;         /* mj_hemi_world */
    uint8_t n_features;    /* 1..64 */
    uint8_t n_actions;     /* 2..16 */
    uint64_t seed;         /* the worlds' seed */
    uint8_t actor[MJ_HEMI_FINGERPRINT];
    uint8_t observer[MJ_HEMI_FINGERPRINT];
} mj_hemi_header;

typedef struct mj_hemi_record {
    uint32_t episode;
    uint16_t step;
    uint8_t actor_move;
    uint8_t grade;         /* mj_credit: 1 cleared, 2 not yet, 3 never reached */
    mj_hemi_verdict verdict;
    int8_t features[MJ_HEMI_MAX_FEATURES];
    uint16_t actor_probs[MJ_HEMI_MAX_ACTIONS];
    uint16_t watcher_probs[MJ_HEMI_MAX_ACTIONS];
} mj_hemi_record;

typedef struct mj_hemi_set {
    mj_hemi_header h;
    mj_hemi_record *r;
    uint32_t n, cap;
    mj_budget *budget;
} mj_hemi_set;

void mj_hemi_set_init(mj_hemi_set *s, mj_budget *b, const mj_hemi_header *h);
void mj_hemi_set_free(mj_hemi_set *s);
mj_status mj_hemi_set_add(mj_hemi_set *s, const mj_hemi_record *r);

/* Bytes one record takes on disk for a header. */
uint32_t mj_hemi_record_bytes(const mj_hemi_header *h);
/* Check a header and a record the way the parser does. */
mj_status mj_hemi_header_check(const mj_hemi_header *h);
mj_status mj_hemi_record_check(const mj_hemi_header *h, const mj_hemi_record *r);

/* Encode into a new buffer from the budget (free with mj_bfree(b, *out, *len)). */
mj_status mj_hemi_encode(const mj_hemi_set *s, mj_budget *b, uint8_t **out, size_t *len);
/* Strict parse of untrusted bytes. On any failure the set is left empty. */
mj_status mj_hemi_parse(const uint8_t *buf, size_t len, mj_budget *b, mj_hemi_set *out);
mj_status mj_hemi_save(const mj_hemi_set *s, const char *path, uint8_t sha[MJ_SHA256_LEN]);
mj_status mj_hemi_load(const char *path, mj_budget *b, mj_hemi_set *out, uint8_t sha[MJ_SHA256_LEN]);

/* ---- tallies the journal keeps ------------------------------------------- */
typedef struct mj_hemi_tally {
    uint32_t steps, cleared, not_yet;
    uint32_t agree_right;    /* watcher agreed, the key cleared the move */
    uint32_t agree_wrong;    /* watcher agreed, the key said not yet: a shared mistake */
    uint32_t object_right;   /* watcher objected, the key said not yet */
    uint32_t object_wrong;   /* watcher objected, the key cleared the move */
    uint32_t unsure;
} mj_hemi_tally;
void mj_hemi_tally_set(const mj_hemi_set *s, mj_hemi_tally *t);

/* ---- the combiner ---------------------------------------------------------- */
typedef struct mj_hemi_target {
    int8_t features[MJ_HEMI_MAX_FEATURES];
    uint8_t move;
    uint8_t from_watcher; /* 1 when the target came from a watcher's objection */
    float weight;         /* > 0: make the move more likely; < 0: less likely */
} mj_hemi_target;

typedef struct mj_hemi_combine_cfg {
    float cleared_weight;  /* a move the key cleared: 1.0 */
    float not_yet_weight;  /* a move the key failed: 0.5 (negative target) */
    float watcher_weight;  /* the watcher's own move on a failed step, times its confidence: 0.75 */
} mj_hemi_combine_cfg;
void mj_hemi_combine_default(mj_hemi_combine_cfg *c);

/* Targets from both passes, pass 1 first, records in order. Only the answer
 * key decides the sign; a watcher's move becomes a target only on a step the
 * key failed, only when the watcher objected with confidence, and only as far
 * as that watcher has earned trust in the same pass: trust = 2 * (judgments
 * the key confirmed / all judgments) - 1, floored at 0, so a watcher that is
 * right no more often than wrong adds nothing. */
mj_status mj_hemi_combine(const mj_hemi_set *const *sets, uint32_t n_sets, const mj_hemi_combine_cfg *cfg,
                          mj_budget *b, mj_hemi_target **out, uint32_t *n_out);

/* ---- the toy hemisphere ---------------------------------------------------- */
typedef struct mj_hemi_toy {
    mj_toy_model m;
} mj_hemi_toy;
/* Wrap a toy policy as a hemisphere; the fingerprint is the SHA-256 of its
 * saved blob. */
mj_status mj_hemi_toy_bind(mj_hemi_toy *t, const char *name, mj_hemi_model *out);
/* Softmax of the integer logits (logit / 127 / 65536), rounded to p * 65535,
 * with the rounding remainder on the top move so the sum is exactly 65535. */
void mj_hemi_toy_probs(const mj_toy_model *m, const int8_t x[MJ_TOY_F], uint16_t probs[MJ_HEMI_MAX_ACTIONS]);

/* A world of toy corridors from a seed: a hidden rule (a teacher policy) picks
 * the move that clears each segment. Same seed, same corridors. */
typedef struct mj_hemi_toy_world {
    mj_toy_model teacher;
    mj_toy_maze *mazes;
    uint32_t n;
    mj_budget *budget;
} mj_hemi_toy_world;
mj_status mj_hemi_toy_world_make(uint64_t seed, uint32_t n_mazes, uint32_t segments, const mj_toy_model *teacher,
                                 mj_budget *b, mj_hemi_toy_world *w);
void mj_hemi_toy_world_free(mj_hemi_toy_world *w);
/* A teacher from a seed (the hidden rule of every toy world). */
void mj_hemi_toy_teacher(uint64_t seed, mj_toy_model *t);

/* Two starting parents for a toy run, from the teacher:
 *   "complement": each side knows half of the rule (left features 0-3,
 *                 right features 4-6; the other columns are noise);
 *   "ternary":    right is the teacher with small noise, left is right made
 *                 ternary ({-1, 0, +1} times one scale per move, the absmean
 *                 rule), the toy picture of the ternary and full Nemo pair. */
mj_status mj_hemi_toy_parents(const char *scenario, uint64_t seed, const mj_toy_model *teacher, mj_toy_model *left,
                              mj_toy_model *right);

/* One pass: the actor plays every maze of the world (stopping at the first
 * failed segment, as partial credit does) and the observer judges every move.
 * The set's header must already name the round, pass and sides. */
mj_status mj_hemi_toy_pass(const mj_hemi_toy_world *w, const mj_hemi_model *actor, const mj_hemi_model *observer,
                           mj_hemi_set *out);

/* Partial-credit score of a toy policy on a world, basis points. CPU. */
uint32_t mj_hemi_toy_score(const mj_toy_model *m, const mj_hemi_toy_world *w);

/* ---- training the combined model ------------------------------------------ */
typedef struct mj_hemi_train_cfg {
    uint32_t steps;     /* 400 */
    uint32_t batch;     /* 64 */
    float lr;           /* 0.5 */
    float step_cap;     /* 4.0: how far any weight may move from the merged start */
    uint64_t seed;      /* minibatch order (splitmix64) */
} mj_hemi_train_cfg;
void mj_hemi_train_default(mj_hemi_train_cfg *c);

/* The merge: a + alpha * (b - a) for every weight in Q16, alpha in Q16
 * (0..65536), rounded half away from zero. Integer only. */
void mj_hemi_toy_merge(const mj_toy_model *a, const mj_toy_model *b, uint32_t alpha_q16, mj_toy_model *out);

/* Train a copy of start on the targets. Deterministic for a seed. */
mj_status mj_hemi_toy_train(const mj_toy_model *start, const mj_hemi_target *t, uint32_t n,
                            const mj_hemi_train_cfg *cfg, mj_toy_model *out, float *final_loss);

/* ---- one round ------------------------------------------------------------- */
typedef struct mj_hemi_round_cfg {
    uint32_t round;
    uint64_t train_seed, holdout_seed;
    uint32_t train_mazes, holdout_mazes, segments;
    mj_hemi_combine_cfg combine;
    mj_hemi_train_cfg train;
    int use_gpu;        /* score the holdout on the GPU when built with CUDA */
    int no_merge;       /* ablation: train from the parent that scored higher in its pass, not the merge */
} mj_hemi_round_cfg;

typedef struct mj_hemi_round_report {
    uint32_t round;
    uint32_t left_holdout_bp, right_holdout_bp, child_holdout_bp;
    uint32_t left_pass_bp, right_pass_bp; /* each side's score while it played */
    uint32_t alpha_q16;                   /* the merge's share of the right parent */
    uint32_t targets, watcher_targets;
    mj_hemi_tally tally[2];
    float loss;
    int kept;                             /* 1: the child beat both parents */
    uint8_t weaker_side;                  /* the side the child replaces when kept */
    uint8_t pass_sha[2][MJ_SHA256_LEN];
    uint8_t child_sha[MJ_SHA256_LEN];
    char why[MJ_TEXT_MAX + 1];
} mj_hemi_round_report;

/* Run one round on toy policies. pass files go to dir/round-NNN/. When the
 * child is kept, *left and *right are updated: the child takes the weaker
 * parent's place and the two swap sides. When it is not, they are restored
 * from their saved blobs (checked against their SHA-256) and left as they were.
 * A journal line goes into ledger_path either way. */
mj_status mj_hemi_toy_round(const char *dir, const char *ledger_path, const mj_toy_model *teacher,
                            mj_toy_model *left, mj_toy_model *right, const mj_hemi_round_cfg *cfg, mj_budget *b,
                            mj_hemi_round_report *rep);

/* Holdout scoring for many policies at once: actions for n states under k
 * toy policies (k * n results). CPU reference; the CUDA build has the same
 * function on the GPU with the same results bit for bit (integer math). */
mj_status mj_hemi_toy_act_batch_cpu(const mj_toy_model *models, uint32_t k, const int8_t *states, uint32_t n,
                                    uint8_t *actions);
/* MJ_E_INVAL when the build has no CUDA or no device is present. */
mj_status mj_hemi_toy_act_batch_gpu(const mj_toy_model *models, uint32_t k, const int8_t *states, uint32_t n,
                                    uint8_t *actions, char err[128]);
int mj_hemi_gpu_built(void);

/* splitmix64, the documented generator every seeded choice here uses. */
uint64_t mj_hemi_next(uint64_t *state);

#ifdef __cplusplus
}
#endif
#endif
