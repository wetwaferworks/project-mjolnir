/* Mjolnir: the steps of a real-weight session that need no model code,
 * each one recorded in the ledger exactly as the Python side records it.
 *
 *   register   copy the original download to a working folder, check the
 *              copy, mark the original read-only, and store a base record
 *              (MJBASE1: file names, sizes, hashes) as a pinned milestone.
 *   apply      make the working copy hold any stored version, including the
 *              untouched base (that is the rollback).
 *
 * Under --data: real-store/ (the version store), real-dreams.mjl (the
 * ledger), reports/.
 *
 * The ledger append keeps the existing behaviour: verify, append, verify
 * again. Two writers appending to one ledger at the same moment are not
 * kept apart here; run one session per data folder.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_SESSION_H
#define MJ_SESSION_H

#include "mjolnir/mj_store.h"
#include "mjolnir/mj_workcopy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_REAL_BASE_ID "base"

typedef struct mj_real_paths {
    char store[MJ_PATH_MAX];
    char ledger[MJ_PATH_MAX];
    char reports[MJ_PATH_MAX];
} mj_real_paths;

mj_status mj_real_paths_for(const char *data, mj_real_paths *p);
/* The real store's limits: 8 GiB in all, 1 GiB per payload, 4 GiB disk kept free. */
void mj_real_limits(mj_store_limits *l);

/* msg (may be NULL) gets one line about what happened or why it was refused. */
mj_status mj_real_register(const char *original, const char *working, const char *data, mj_budget *b, char *msg,
                           size_t cap);
/* The SHA-256 (hex) of the model file the base record names. */
mj_status mj_real_base_sha(const char *data, mj_budget *b, char sha[MJ_SHA256_HEX]);
mj_status mj_real_apply(const char *original, const char *working, const char *data, const char *version_id,
                        const mj_wc_options *opt, mj_budget *b, mj_wc_result *res);

/* UTC time as 20261118T190000Z. */
mj_status mj_utc_stamp(char out[17]);

#ifdef __cplusplus
}
#endif
#endif
