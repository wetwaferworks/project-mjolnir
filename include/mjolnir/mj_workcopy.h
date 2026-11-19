/* Mjolnir: the original download, its working copy, and applying stored
 * edits. The same files and rules as the Python side:
 *
 *   original/   the download. Never opened for writing; marked read-only.
 *   working/    a byte-for-byte copy. Edits are applied here only.
 *
 * Applying a version restores every tensor any earlier edit touched to the
 * original bytes, then adds the version's chain of deltas, oldest first, and
 * writes only those tensors back into the working copy.
 *
 * One addition: before writing into the working model file in place, a
 * marker MJOLNIR-DIRTY.txt is written next to it naming every tensor about to
 * change. It is removed only after the writes are read back and the state
 * file is updated. If a write is cut short the marker stays, the state is
 * reported dirty, and the next checkout restores the named tensors too.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_WORKCOPY_H
#define MJ_WORKCOPY_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"
#include "mjolnir/mj_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_WC_MODEL_FILE "model-00001-of-00001.safetensors"
#define MJ_WC_STATE_FILE "MJOLNIR-STATE.txt"
#define MJ_WC_DIRTY_FILE "MJOLNIR-DIRTY.txt"
#define MJ_WC_FILES_MAX 4096u
#define MJ_WC_NAME_MAX 255u      /* one file name, bytes */
#define MJ_WC_TOUCHED_MAX 65536u /* tensors one state file may list */
#define MJ_WC_TNAME_MAX 200u     /* a tensor name an edit can carry */

/* ---- checksums: file name -> (size, sha256), sorted by name ---- */
typedef struct mj_sum {
    char name[MJ_WC_NAME_MAX + 1];
    uint64_t size;
    char sha[MJ_SHA256_HEX];
} mj_sum;

typedef struct mj_sums {
    size_t n, cap;
    mj_sum *v;
    mj_budget *budget;
} mj_sums;

void mj_sums_free(mj_sums *s);
mj_status mj_wc_checksums(const char *folder, mj_budget *b, mj_sums *out);
/* "<sha>  <size>  <name>\n" per file, sorted by name. The buffer is charged
 * to b and NUL-terminated; free it with mj_bfree(b, *out, *n + 1). */
mj_status mj_wc_format_sums(const mj_sums *s, mj_budget *b, char **out, size_t *n);
mj_status mj_wc_parse_sums(const char *text, size_t n, mj_budget *b, mj_sums *out);
int mj_wc_sums_equal(const mj_sums *a, const mj_sums *b);
const mj_sum *mj_wc_sums_find(const mj_sums *s, const char *name);

/* ---- a sorted set of tensor names ---- */
typedef struct mj_names {
    size_t n, cap;
    char (*v)[MJ_WC_TNAME_MAX + 1];
    mj_budget *budget;
} mj_names;

void mj_names_free(mj_names *s);
mj_status mj_names_add(mj_names *s, const char *name); /* keeps it sorted and unique */
int mj_names_has(const mj_names *s, const char *name);

/* ---- folders ---- */
/* Refuse the same folder, or one inside the other. */
mj_status mj_wc_guard(const char *original, const char *working);
/* Copy every file, verify the copy, mark the original files read-only. */
mj_status mj_wc_make_copy(const char *original, const char *working, mj_budget *b, mj_sums *out);

/* ---- the state file ---- */
typedef struct mj_wc_state {
    char applied[MJ_ID_MAX + 1]; /* "-" when there is no state file */
    mj_names touched;
    int dirty;                   /* a dirty marker is present */
    mj_names dirty_touched;      /* the tensors it names */
} mj_wc_state;

mj_status mj_wc_read_state(const char *working, mj_budget *b, mj_wc_state *st);
void mj_wc_state_free(mj_wc_state *st);
mj_status mj_wc_write_state(const char *working, const char *applied, const mj_names *touched);

/* ---- checkout ---- */
typedef struct mj_wc_options {
    int use_cuda;      /* 1: GPU when present, else the CPU; 0: CPU only */
    unsigned threads;  /* CPU threads, 0 = automatic */
} mj_wc_options;

typedef struct mj_wc_result {
    size_t touched;        /* tensors written (restored or edited) */
    size_t edited;         /* of those, with a delta on top */
    int used_cuda;
    uint64_t device_peak;  /* bytes the GPU path allocated at most */
    char reason[160];      /* why it was refused, when it was */
} mj_wc_result;

/* Make the working copy equal to the original plus the version's chain of
 * deltas. base_sha is the hex SHA-256 the deltas must have been computed on. */
mj_status mj_wc_checkout(mj_store *store, const char *version_id, const char *original, const char *working,
                         const char *base_sha, const mj_wc_options *opt, mj_budget *b, mj_wc_result *res);

#ifdef __cplusplus
}
#endif
#endif
