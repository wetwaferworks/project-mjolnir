/* Mjolnir: the low-rank edit from captured keys, outputs and gradients, the
 * C side of python/mjolnir/real/edit.py (whiten, with_shared_key, low_rank).
 *
 * For n examples with key k_i (the down projection's input, length in), its
 * output o_i and the gradient g_i at that output (length out):
 *
 *   G   = [ g_i / (|g_i| + 1e-12) * |o_i| ]          out x n, one column each
 *   K   = k_i / (|k_i|^2 + 1e-12)                    without background keys
 *   K   = Kw_i / (Kw_i . k_i), Kw = whiten(K0, B)    with them
 *   dW  = G K, kept to its top `rank` singular directions: dW ~ U V
 *
 * whiten(K, B) maps each row through (lam I + B^T B / m)^-1 by the Woodbury
 * identity, lam = ridge * sum(B^2) / (m d): with S = (B B^T + m lam I)^-1 B K^T,
 * Kw = (K - S^T B) / lam. The background B gets the mean key appended
 * shared_weight times first (with_shared_key in Python).
 *
 * All arithmetic is in double. The three products with the background (the
 * expensive part) run through cuBLAS when built with CUDA and a device is
 * present, else on CPU threads; the rest (Cholesky, Householder QR, Jacobi
 * SVD) is small and runs on the CPU.
 *
 * U and V are not unique (a singular vector's sign can flip), but U V is.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_LOWRANK_H
#define MJ_LOWRANK_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_LR_MAX_EXAMPLES 1024u
#define MJ_LR_MAX_RANK 64u
#define MJ_LR_MAX_DIM (1u << 20)
#define MJ_LR_MAX_BACKGROUND 8192u

typedef struct mj_lr_input {
    uint32_t n, out, in;  /* examples, output length, input length */
    const float *k;       /* n x in */
    const float *o;       /* n x out */
    const float *g;       /* n x out */
    uint32_t m;           /* background rows; 0 for none */
    const float *bg;      /* m x in, or NULL */
    uint32_t shared_weight; /* mean key rows appended to the background (Python: 64) */
    double ridge;         /* Python: 0.1 */
    uint32_t rank;
    int use_cuda;
    unsigned threads;     /* 0: automatic */
} mj_lr_input;

typedef struct mj_lr_result {
    uint32_t r;           /* min(rank, n) */
    float *U;             /* out x r, row-major */
    float *V;             /* r x in, row-major */
    double s[MJ_LR_MAX_EXAMPLES]; /* singular values of dW, largest first (n of them) */
    int used_cuda;
    mj_budget *budget;
    size_t nu, nv;
} mj_lr_result;

mj_status mj_lowrank(const mj_lr_input *in, mj_budget *b, mj_lr_result *res);
void mj_lr_free(mj_lr_result *res);

/* C = A B^T (A: p x d, B: q x d, C: p x q), all row-major doubles. Exposed
 * for the tests and the GPU file. sym != 0 when A == B (C is symmetric). */
mj_status mj_lr_gemm_nt_cpu(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d, int sym,
                            unsigned threads);
/* C = A B (A: p x q, B: q x d, C: p x d). */
mj_status mj_lr_gemm_nn_cpu(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d,
                            unsigned threads);
/* GPU versions; MJ_E_NOTFOUND without CUDA. */
int mj_lr_cuda_available(void);
mj_status mj_lr_gemm_nt_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d);
mj_status mj_lr_gemm_nn_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d);

#ifdef __cplusplus
}
#endif
#endif
