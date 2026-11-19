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
 *
 * One writer at a time: an append first creates <ledger>.writer exclusively
 * (the writer token) and removes it when done. A second writer finds the
 * token and is refused with MJ_E_BUSY; it never waits, and it never takes the
 * token over. A token left by a run that died is cleared only on request
 * (mj_ledger_writer_release_stale, `mj_ledger_verify --release-stale`), and only
 * when the process named in it is provably not running on this host.
 *
 * A crash in the middle of an append can leave a cut-off last line. Appends
 * refuse until it is repaired with mj_ledger_repair_tail
 * (`mj_ledger_verify --repair-tail`), which keeps a backup copy, checks that every
 * complete line still verifies, and only then cuts the incomplete line. It
 * never edits or drops a complete record.
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
 * flush it to the disk. Takes the writer token for the length of the call:
 * MJ_E_BUSY (with one line on stderr naming the holder) when another writer
 * holds it. Refuses with MJ_E_CHAIN when the chain is broken. If the write
 * itself fails, the bytes this call added are cut off again, so a failed
 * append never leaves a torn line behind. */
mj_status mj_ledger_append(const char *path, mj_budget *b, mj_dream_record *r);

/* ---- the writer token ---- */
#define MJ_LEDGER_TOKEN_MAX 256u

typedef struct mj_ledger_writer {
    char ledger[MJ_PATH_MAX];
    char token_path[MJ_PATH_MAX];
    char token[MJ_LEDGER_TOKEN_MAX]; /* the exact bytes written, to recognise our own token */
    size_t token_len;
    int held;
} mj_ledger_writer;

/* Who holds a token, as written in it. */
typedef struct mj_ledger_holder {
    uint64_t pid;
    uint64_t start;
    char host[64];
    char since[MJ_WHEN_MAX + 1];
    char nonce[33];
    int running;  /* mj_proc_running() for this host, or -1 for another host */
    int same_host;
} mj_ledger_holder;

/* Claim the token for ledger. MJ_E_BUSY when someone holds it (holder filled
 * in when not NULL). Holding it across several appends is fine; use
 * mj_ledger_append_held for those. */
mj_status mj_ledger_writer_claim(const char *ledger, mj_ledger_writer *w, mj_ledger_holder *holder);
/* Remove our own token. Refuses (MJ_E_FORBIDDEN) if the file there is no
 * longer the token we wrote; it is then left alone. */
mj_status mj_ledger_writer_release(mj_ledger_writer *w);
/* Read the current token. MJ_E_NOTFOUND when nobody holds it, MJ_E_CORRUPT
 * when the file is there but unreadable (it is then treated as held). */
mj_status mj_ledger_writer_holder(const char *ledger, mj_ledger_holder *h);
/* Clear a token left by a run that is gone. Refuses with MJ_E_BUSY when the
 * process named in it is (or may be) still running, and with MJ_E_FORBIDDEN
 * when it was written on another host or cannot be read. The token is renamed
 * to <ledger>.writer.stale-<when>-<nonce> for the record, not deleted. */
mj_status mj_ledger_writer_release_stale(const char *ledger, mj_ledger_holder *h, char *moved_to, size_t moved_cap);
/* One line describing h, for refusal messages. */
void mj_ledger_holder_describe(const mj_ledger_holder *h, char *out, size_t cap);

/* Append while holding w (claimed for this ledger). */
mj_status mj_ledger_append_held(const mj_ledger_writer *w, mj_budget *b, mj_dream_record *r);

/* ---- torn tails ---- */
/* Check every complete line of buf. *good_len is the length up to and
 * including the last '\n'; *torn is 1 when bytes follow it. MJ_OK when all
 * complete lines verify (a torn tail alone is not an error here), MJ_E_CHAIN
 * with *bad_line otherwise. */
mj_status mj_ledger_check_tail(const char *buf, size_t n, mj_ledger_state *st, uint64_t *bad_line, size_t *good_len,
                               int *torn);
/* Repair a torn last line while holding w: refuse unless every complete line
 * verifies and the leftover looks like the start of a record (printable, no
 * newline, at most one record long, beginning with a prefix of "MJL1|"); write
 * the whole file to <ledger>.torn-<when>.bak first; then cut the leftover and
 * verify again. *removed is the number of bytes cut (0 when nothing was torn). */
mj_status mj_ledger_repair_tail(const mj_ledger_writer *w, mj_budget *b, size_t *removed, char *backup,
                                size_t backup_cap);

/* Fill a record with "-" / zero defaults. */
void mj_ledger_record_init(mj_dream_record *r);

#ifdef __cplusplus
}
#endif
#endif
