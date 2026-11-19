/* Built when CUDA is off: the GPU products report that they are not there.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_lowrank.h"

int mj_lr_cuda_available(void) { return 0; }

mj_status mj_lr_gemm_nt_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d) {
    (void)A; (void)B; (void)C; (void)p; (void)q; (void)d;
    return MJ_E_NOTFOUND;
}

mj_status mj_lr_gemm_nn_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d) {
    (void)A; (void)B; (void)C; (void)p; (void)q; (void)d;
    return MJ_E_NOTFOUND;
}
