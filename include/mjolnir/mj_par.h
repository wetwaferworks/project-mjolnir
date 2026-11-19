/* Mjolnir: split a loop over [0, n) across threads and join them before
 * returning. No locks: each thread owns its range, and the join is the only
 * hand-off.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_PAR_H
#define MJ_PAR_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_PAR_THREADS_MAX 8u

typedef void (*mj_par_fn)(void *ctx, uint32_t lo, uint32_t hi);

/* threads == 0 picks the processor count, capped at MJ_PAR_THREADS_MAX.
 * Ranges are contiguous and as even as possible. */
void mj_par_for(uint32_t n, unsigned threads, mj_par_fn fn, void *ctx);
unsigned mj_par_cpus(void);

#ifdef __cplusplus
}
#endif
#endif
