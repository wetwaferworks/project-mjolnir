/* Mjolnir M0: the weight version store.
 *
 * Content-addressed: every payload is kept once, as objects/<sha256>.blob.
 * Each version is an immutable manifest, versions/<id>.ver, naming its blob,
 * size, SHA-256 and CRC-32. A version id can never be written twice, not even
 * after retention retires it (the manifest moves to retired/ and stays as the
 * record). HEAD names the live version; rollback points HEAD at an older one.
 *
 * Every write is atomic (temp file, flush, rename) and read back and checked
 * before the call returns. A store ceiling and a free-disk floor are enforced
 * before anything is written.
 *
 * Stage M0 holds toy weights only: payloads that look like real model files
 * (GGUF, safetensors, zip/PyTorch) are refused.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_STORE_H
#define MJ_STORE_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mj_kind {
    MJ_KIND_SNAPSHOT = 1,  /* taken before every dream: the rollback point */
    MJ_KIND_NIGHTLY = 2,   /* a version produced by a nightly dream */
    MJ_KIND_MILESTONE = 3, /* graded known-good version, pinned for good */
    MJ_KIND_CANDIDATE = 4  /* a dream's proposal, not yet integrated */
} mj_kind;

const char *mj_kind_name(mj_kind k);

typedef struct mj_store_limits {
    uint64_t max_store_bytes; /* ceiling on all live blobs together */
    uint64_t max_blob_bytes;  /* ceiling on one payload */
    uint64_t min_free_bytes;  /* refuse a write that would leave less free disk */
    uint32_t keep_nightly;    /* retention: newest N nightly versions (about 14) */
    uint32_t keep_snapshots;  /* retention: newest N pre-dream snapshots */
    uint32_t keep_candidates; /* retention: newest N candidates */
    uint32_t max_versions;    /* ceiling on manifests scanned */
} mj_store_limits;

void mj_store_default_limits(mj_store_limits *l);

typedef struct mj_version_info {
    char id[MJ_ID_MAX + 1];
    uint64_t seq;
    mj_kind kind;
    uint8_t sha256[MJ_SHA256_LEN];
    uint64_t size;
    uint32_t crc32;
    char parent[MJ_ID_MAX + 1]; /* "-" for none */
    char label[MJ_TEXT_MAX + 1];
} mj_version_info;

typedef struct mj_store {
    char root[MJ_PATH_MAX];
    mj_store_limits lim;
    mj_budget *budget;
    uint64_t next_seq;
    uint64_t used_bytes;
} mj_store;

mj_status mj_store_open(mj_store *s, const char *root, const mj_store_limits *lim, mj_budget *b);

/* Save a new version. Refuses an id that exists or ever existed (MJ_E_EXISTS). */
mj_status mj_store_put(mj_store *s, const char *id, mj_kind kind, const char *parent, const char *label,
                       const void *data, size_t n, mj_version_info *out);

mj_status mj_store_info(mj_store *s, const char *id, mj_version_info *out);

/* 1 if the id names a live or retired version (ids are never reused). */
int mj_store_ever_used(mj_store *s, const char *id);

/* Load a version's payload, checking size, CRC-32 and SHA-256. Free with
 * mj_bfree(s->budget, *data, *n ? *n : 1). */
mj_status mj_store_get(mj_store *s, const char *id, uint8_t **data, size_t *n);

mj_status mj_store_head(mj_store *s, char id_out[MJ_ID_MAX + 1]); /* MJ_E_NOTFOUND if unset */
mj_status mj_store_set_head(mj_store *s, const char *id);         /* verifies the version first */
/* Rollback: verify the target fully, then point HEAD at it. */
mj_status mj_store_rollback(mj_store *s, const char *id);
mj_status mj_store_pin(mj_store *s, const char *id);
int mj_store_is_pinned(mj_store *s, const char *id);

typedef struct mj_retain_report {
    uint32_t scanned, kept, retired, blobs_removed;
    uint64_t bytes_freed;
} mj_retain_report;

/* Retire versions past the retention policy. Pinned versions, milestones and
 * HEAD are always kept. Retired manifests move to retired/; unreferenced
 * blobs are removed. */
mj_status mj_store_retain(mj_store *s, mj_retain_report *r);

/* Re-check every live version's blob. Returns MJ_E_CORRUPT on the first bad one. */
mj_status mj_store_verify_all(mj_store *s, uint32_t *checked);

/* Exposed for tests and fuzzing: strict manifest parser. */
mj_status mj_manifest_parse(const char *text, size_t n, mj_version_info *out);
size_t mj_manifest_format(const mj_version_info *v, char *out, size_t cap);
/* 1 if the payload looks like a real model file (refused in M0). */
int mj_payload_is_model_file(const void *data, size_t n);

#ifdef __cplusplus
}
#endif
#endif
