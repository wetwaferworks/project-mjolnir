/* Mjolnir M0: the nightly-dream driver and the "take a nap" entry point.
 *
 * One dream, in order:
 *   1. refuse unless dry-run (stage M0 never changes live weights)
 *   2. check the ledger chain; refuse to dream on a broken chain
 *   3. load and verify the live weights named by HEAD
 *   4. read the last dream's failure note and adapt (decision 10)
 *   5. snapshot the live weights into the store: the rollback point
 *   6. dream: reward-weighted nudges on a copy, old memories mixed in
 *   7. watch and rest: partial-credit scoring of the candidate
 *   8. verify: step, shift, golden and improvement gates
 *   9. record the candidate in the store (never as HEAD in M0)
 *  10. check HEAD is unchanged, then append the dream to the ledger
 * Toy weights only. Integration (moving HEAD) is M1 work behind JD's go.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_DREAM_H
#define MJ_DREAM_H

#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_toy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mj_dream_material {
    const mj_toy_run *new_runs;
    uint32_t n_new;
    const mj_toy_run *old_runs;
    uint32_t n_old;
    const mj_toy_golden *golden;
    uint32_t n_golden;
    const mj_toy_maze *eval;
    uint32_t n_eval;
} mj_dream_material;

typedef struct mj_dream_cfg {
    const char *store_root;
    const char *ledger_path;
    const char *dream_id; /* at most 58 chars: "-pre" and "-cand" are appended */
    const char *when;     /* UTC timestamp; NULL = now */
    const char *trigger;  /* nightly | nap | test */
    const char *grader;   /* e.g. auto-maze */
    int dry_run;          /* must be 1 in M0 */
    mj_store_limits limits;
    mj_update_cfg update;
    size_t mem_cap;       /* memory ceiling for the whole dream */
} mj_dream_cfg;

void mj_dream_default_cfg(mj_dream_cfg *c);

typedef struct mj_dream_result {
    char head[MJ_ID_MAX + 1];
    char snapshot[MJ_ID_MAX + 1];
    char candidate[MJ_ID_MAX + 1]; /* "-" if none was kept */
    char outcome[MJ_SHORT_MAX + 1];
    int adapted;                   /* 1 if a prior failure note changed this dream's steps */
    mj_status verdict;             /* the update gates: MJ_OK, MJ_E_GATE, MJ_E_TIMEOUT ... */
    mj_update_report rep;
    uint64_t ledger_seq;
} mj_dream_result;

/* Returns MJ_OK when the dream ran and was recorded, whatever the gates said
 * (see result->verdict). Any other return means nothing was recorded. */
mj_status mj_dream_run(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r);

/* "Take a nap": a dream started by new verified experience instead of the
 * clock. Needs at least one new run. Always dry-run in M0. */
mj_status mj_take_a_nap(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r);

/* The nightly driver: one dream, then the retention policy, then a full
 * store check. Always dry-run in M0. */
mj_status mj_nightly(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r,
                     mj_retain_report *retain);

/* First-time setup: save the synthetic genesis toy weights as a pinned
 * milestone and point HEAD at it. MJ_E_EXISTS if HEAD is already set. */
mj_status mj_dream_seed_toy(const char *store_root, const mj_store_limits *lim, char id_out[MJ_ID_MAX + 1]);

#ifdef __cplusplus
}
#endif
#endif
