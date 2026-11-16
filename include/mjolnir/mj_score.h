/* Mjolnir M0: partial-credit scoring (JD's decision 8). Integer math only, so
 * every machine gives the same score for the same run.
 *
 * Mazes: credit per segment. Every pit or obstacle cleared before a failure
 * counts as a success. The segment where the run failed is "not yet": it needs
 * refinement, it is not counted as bad. Segments after the failure were never
 * reached and are not graded.
 *
 * Circuits: credit per element. Each element that works counts as good; each
 * faulty one (a miswired pixel, a broken memory transmission) is "not yet".
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_SCORE_H
#define MJ_SCORE_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_SCORE_MAX_PARTS 4096u
#define MJ_BP_FULL 10000u /* scores are basis points: 10000 = 100% */

typedef enum mj_credit {
    MJ_CREDIT_SUCCESS = 1,
    MJ_CREDIT_NOT_YET = 2,  /* needs refinement */
    MJ_CREDIT_UNGRADED = 3  /* never reached */
} mj_credit;

typedef enum mj_seg_type { MJ_SEG_PIT = 1, MJ_SEG_OBSTACLE = 2, MJ_SEG_GOAL = 3 } mj_seg_type;
typedef enum mj_seg_result { MJ_RUN_CLEARED = 1, MJ_RUN_FAILED = 2, MJ_RUN_NOT_REACHED = 3 } mj_seg_result;

typedef struct mj_maze_segment {
    uint8_t type;   /* mj_seg_type */
    uint8_t result; /* mj_seg_result */
} mj_maze_segment;

typedef struct mj_maze_score {
    uint32_t total, success, not_yet, ungraded;
    uint32_t score_bp; /* success * 10000 / total, rounded down */
    int escaped;       /* every segment cleared and the last one is the goal */
} mj_maze_score;

/* Rejects malformed runs: a cleared segment after a failure or after an
 * unreached one, more than one failure, unknown codes, n == 0 or n too big.
 * labels (optional) receives one credit per segment. */
mj_status mj_score_maze(const mj_maze_segment *seg, uint32_t n, mj_credit *labels, mj_maze_score *out);

typedef enum mj_elem_kind { MJ_ELEM_PIXEL = 1, MJ_ELEM_WIRE = 2, MJ_ELEM_GATE = 3, MJ_ELEM_MEMORY_TX = 4 } mj_elem_kind;

typedef struct mj_circuit_element {
    uint16_t id;  /* unique within one test */
    uint8_t kind; /* mj_elem_kind */
    uint8_t pass; /* 0 or 1, from the simulator's trace */
} mj_circuit_element;

typedef struct mj_circuit_score {
    uint32_t total, good, not_yet;
    uint32_t score_bp;
    int all_pass;
} mj_circuit_score;

mj_status mj_score_circuit(const mj_circuit_element *el, uint32_t n, mj_credit *labels, mj_circuit_score *out);

/* Running total across many runs: score = sum(success) / sum(parts). */
typedef struct mj_score_accum {
    uint64_t parts, success;
} mj_score_accum;

void mj_score_accum_add(mj_score_accum *a, uint32_t parts, uint32_t success);
uint32_t mj_score_accum_bp(const mj_score_accum *a);

#ifdef __cplusplus
}
#endif
#endif
