/* The hemisphere batch scorer without CUDA: the CPU path is the only one.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_hemi.h"

#include <stdio.h>

int mj_hemi_gpu_built(void) { return 0; }

mj_status mj_hemi_toy_act_batch_gpu(const mj_toy_model *models, uint32_t k, const int8_t *states, uint32_t n,
                                    uint8_t *actions, char err[128]) {
    (void)models; (void)k; (void)states; (void)n; (void)actions;
    if (err) snprintf(err, 128, "built without CUDA");
    return MJ_E_INVAL;
}
