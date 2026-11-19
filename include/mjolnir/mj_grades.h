/* Mjolnir: grades, kept in their own hash-chained ledger (MJL1, outcome "grade").
 *
 * The C version of python/mjolnir/grades.py. Each grade is one ledger line:
 * the step it grades (in "dream" and "inputs"), who graded it, and the label
 * in "gates" as label=good|bad|productive. Grades are never edited; a new
 * grade for the same step comes later in the chain and the latest one wins.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_GRADES_H
#define MJ_GRADES_H
#include "mjolnir/mj_common.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MJ_GRADES_MAX 1000000u /* distinct graded steps kept in memory */

/* Score recorded for a label (good and productive 10000, bad 0); -1 if the
 * label is unknown. */
int mj_grade_label_score(const char *label);
/* Training reward for a grader's label and for an automatic label. */
double mj_grade_reward(const char *label);      /* good 1.0, productive 1.5, bad -1.0 */
double mj_grade_auto_reward(const char *autolab); /* good 1.0, productive 1.5, not-yet -1.0 */

/* Append one grade. *seq (optional) gets the ledger sequence number.
 * MJ_E_INVAL for an unknown label or a bad grader name (msg says which). */
mj_status mj_grade_add(const char *ledger_path, mj_budget *b, const char *replay_id, int64_t index, const char *label,
                       const char *grader, const char *autolab, uint64_t *seq, char msg[96]);

typedef struct mj_grade {
    char replay_id[MJ_TEXT_MAX + 1];
    int64_t index;
    char label[12];
    char grader[MJ_ID_MAX + 1];
} mj_grade;

typedef struct mj_grade_map {
    mj_grade *g; /* in order of the first grade of each step */
    uint32_t n, cap;
} mj_grade_map;

/* The newest grade per step. The whole chain is verified first; a broken
 * chain is MJ_E_CHAIN. An absent ledger is an empty map. */
mj_status mj_grades_latest(const char *ledger_path, mj_budget *b, mj_grade_map *out);
const mj_grade *mj_grades_find(const mj_grade_map *m, const char *replay_id, int64_t index);
void mj_grades_free(mj_grade_map *m);

#ifdef __cplusplus
}
#endif
#endif
