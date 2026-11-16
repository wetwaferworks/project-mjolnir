/* Mjolnir M0: shared status codes, limits and small helpers.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_COMMON_H
#define MJ_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every fallible call returns one of these. Anything other than MJ_OK leaves
 * the caller's state unchanged (fail into the safe state). */
typedef enum mj_status {
    MJ_OK = 0,
    MJ_E_INVAL = 1,     /* bad argument or malformed input */
    MJ_E_IO = 2,        /* operating system I/O failure */
    MJ_E_EXISTS = 3,    /* refused: target already exists (no overwrite) */
    MJ_E_NOTFOUND = 4,
    MJ_E_CORRUPT = 5,   /* checksum or hash mismatch */
    MJ_E_FULL = 6,      /* a disk or storage ceiling would be exceeded */
    MJ_E_LIMIT = 7,     /* a size, count or memory ceiling would be exceeded */
    MJ_E_TIMEOUT = 8,   /* a time or step ceiling was reached */
    MJ_E_GATE = 9,      /* a verification gate failed */
    MJ_E_FORBIDDEN = 10,/* refused by a standing gate (M0: live edits) */
    MJ_E_CHAIN = 11     /* hash chain broken */
} mj_status;

const char *mj_status_name(mj_status s);

/* Hard ceilings for stage M0. Inputs above these are rejected, not truncated. */
#define MJ_ID_MAX 64u                 /* version / dream id length */
#define MJ_TEXT_MAX 256u              /* free-text ledger field length */
#define MJ_PATH_MAX 1024u
#define MJ_BLOB_MAX_DEFAULT (64u * 1024u * 1024u) /* toy weights only in M0 */
#define MJ_LEDGER_RECORD_MAX 4096u
#define MJ_LEDGER_FILE_MAX (64u * 1024u * 1024u)

/* Memory budget: every module that allocates charges a budget owned by its
 * context. Single-threaded by design (phase boundaries, no locks). */
typedef struct mj_budget {
    size_t cap;
    size_t used;
    size_t peak;
} mj_budget;

void mj_budget_init(mj_budget *b, size_t cap);
void *mj_balloc(mj_budget *b, size_t n);           /* zeroed; NULL on ceiling */
void mj_bfree(mj_budget *b, void *p, size_t n);

/* Identifier rule shared by versions, dreams and graders:
 * 1..MJ_ID_MAX chars of [A-Za-z0-9._-], not starting with '.' or '-'. */
int mj_id_valid(const char *s);
/* Free text rule: 0..MJ_TEXT_MAX printable ASCII (0x20..0x7E) except '|'. */
int mj_text_valid(const char *s);

/* Lowercase hex helpers. */
void mj_hex_encode(const uint8_t *in, size_t n, char *out /* 2n+1 */);
int mj_hex_decode(const char *in, size_t hexlen, uint8_t *out);

/* Checked unsigned parse of a decimal string of exactly len chars. */
int mj_parse_u64(const char *s, size_t len, uint64_t *out);

#ifdef __cplusplus
}
#endif
#endif
