/* Built when CUDA is off: the GPU path reports that it is not there.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_apply.h"

int mj_apply_cuda_available(void) { return 0; }

mj_status mj_apply_cuda(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols, const mj_apply_term *t,
                        size_t nt, uint64_t *device_peak) {
    (void)orig; (void)out; (void)rows; (void)cols; (void)t; (void)nt;
    if (device_peak) *device_peak = 0;
    return MJ_E_NOTFOUND;
}
