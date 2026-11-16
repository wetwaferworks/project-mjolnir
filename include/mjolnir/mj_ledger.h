/* Mjolnir M0: the dream ledger.
 *
 * An append-only text file, one record per line. Each line carries the hash of
 * the line before it and its own SHA-256, so any edit, deletion, reordering or
 * torn write breaks the chain at a known line. Appends are refused while the
 * chain is broken. Every dream, score, gate result and grade goes in here.
 *
 * Line layout ('|' separated, fixed field order):
 *   MJL1|seq|prev|dream|when|trigger|grader|inputs|before|candidate|edit|
 *   score_before|score_after|shift_bp|gates|outcome|notes|hash
 * hash = SHA-256 of everything before the final "|hash".
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_LEDGER_H
#define MJ_LEDGER_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_WHEN_MAX 32u
#define MJ_SHORT_MAX 24u
#define MJ_INPUTS_MAX 16u

typedef struct mj_dream_record {
    uint64_t seq;                         /* assigned by append, from 1 */
    uint8_t prev[MJ_SHA256_LEN];          /* assigned by append; zeros for the first */
    char dream_id[MJ_ID_MAX + 1];
    char when[MJ_WHEN_MAX + 1];           /* UTC, e.g. 2026-11-16T17:42:00Z */
    char trigger[MJ_SHORT_MAX + 1];       /* nightly | nap | test */
    char grader[MJ_ID_MAX + 1];           /* who graded: auto-maze, auto-circuit, JD, ... */
    char inputs[MJ_TEXT_MAX + 1];         /* replay ids, comma separated, or "-" */
    char weights_before[MJ_ID_MAX + 1];   /* version id or "-" */
    char weights_candidate[MJ_ID_MAX + 1];/* version id or "-" */
    char edit[MJ_TEXT_MAX + 1];           /* edit summary */
    uint64_t score_before_bp;             /* basis points, 0..10000 */
    uint64_t score_after_bp;
    uint64_t shift_bp;                    /* behavior shift on the golden set */
    char gates[MJ_TEXT_MAX + 1];          /* e.g. step=pass,shift=pass,golden=pass */
    char outcome[MJ_SHORT_MAX + 1];       /* see mj_outcome_valid() */
    char notes[MJ_TEXT_MAX + 1];          /* failure notes the next dream reads */
    uint8_t hash[MJ_SHA256_LEN];          /* assigned by append */
} mj_dream_record;

/* integrated | dry-run-pass | rejected | aborted | rolled-back | grade */
int mj_outcome_valid(const char *s);

typedef struct mj_ledger_state {
    uint64_t count;
    uint8_t last_hash[MJ_SHA256_LEN];
    mj_dream_record last; /* valid when count > 0 */
} mj_ledger_state;

/* Parse one line (without its '\n') and check its own hash. Does not check the
 * link to the previous line. */
mj_status mj_ledger_parse_line(const char *line, size_t n, mj_dream_record *out);
/* Format a record whose seq, prev and hash are already set. Returns length
 * including the trailing '\n', or 0 if it does not fit or a field is invalid. */
size_t mj_ledger_format_line(const mj_dream_record *r, char *out, size_t cap);

/* Walk a whole ledger image. On a break, *bad_line is the 1-based line. */
mj_status mj_ledger_verify_buf(const char *buf, size_t n, mj_ledger_state *st, uint64_t *bad_line);
mj_status mj_ledger_verify_file(const char *path, mj_budget *b, mj_ledger_state *st, uint64_t *bad_line);

/* Verify the chain, then append r (seq, prev and hash are filled in) and
 * flush it to the disk. Refuses with MJ_E_CHAIN when the chain is broken. */
mj_status mj_ledger_append(const char *path, mj_budget *b, mj_dream_record *r);

/* Fill a record with "-" / zero defaults. */
void mj_ledger_record_init(mj_dream_record *r);

#ifdef __cplusplus
}
#endif
#endif
