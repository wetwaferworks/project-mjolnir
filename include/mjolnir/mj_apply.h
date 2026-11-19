/* Mjolnir: add a chain of low-rank edits to one bf16 tensor.
 *
 *   s   = +0.0f, then for each term in order: s = s + float(U_i . V_j) * scale
 *   out = bf16(float(orig) + s)          (round to nearest, ties to even)
 *
 * U_i . V_j is summed in double, k = 0 .. rank-1 in order, then rounded to
 * float once. The CPU and CUDA paths do exactly the same operations in the
 * same order with no fused multiply-add, so they agree bit for bit. Python
 * (torch float32 matmul) sums in float with its own order, so it can differ
 * from both by one bf16 step where a value sits on a rounding boundary.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_APPLY_H
#define MJ_APPLY_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mj_apply_term {
    const float *U; /* rows x rank */
    const float *V; /* rank x cols */
    uint32_t rank;
    float scale;    /* the delta's scale, rounded to float (as torch does) */
} mj_apply_term;

/* threads: 0 picks the processor count, capped at 8. */
mj_status mj_apply_cpu(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols, const mj_apply_term *t,
                       size_t nt, unsigned threads);

/* 1 if this build has CUDA and a device is present. */
int mj_apply_cuda_available(void);

/* Same result on the GPU. *device_peak (if not NULL) gets the bytes this
 * call allocated on the device. MJ_E_NOTFOUND when there is no CUDA. */
mj_status mj_apply_cuda(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols, const mj_apply_term *t,
                        size_t nt, uint64_t *device_peak);

#ifdef __cplusplus
}
#endif
#endif
