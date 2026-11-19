// The low-rank edit's three big products on the GPU, through cuBLAS, in
// double. Row-major inputs are read as their column-major transposes.
// MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
#include "mjolnir/mj_lowrank.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>

namespace {

struct dev {
    double *p = nullptr;
    ~dev() {
        if (p) cudaFree(p);
    }
    bool alloc(size_t n) { return cudaMalloc(&p, n * sizeof(double)) == cudaSuccess; }
};

struct handle {
    cublasHandle_t h = nullptr;
    ~handle() {
        if (h) cublasDestroy(h);
    }
};

mj_status run(bool nt, const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d) {
    if (A == nullptr || B == nullptr || C == nullptr || p == 0 || q == 0 || d == 0) return MJ_E_INVAL;
    if (!mj_lr_cuda_available()) return MJ_E_NOTFOUND;
    const size_t na = nt ? (size_t)p * d : (size_t)p * q, nb = nt ? (size_t)q * d : (size_t)q * d,
                 nc = nt ? (size_t)p * q : (size_t)p * d;
    dev a, b, c;
    handle h;
    if (!a.alloc(na) || !b.alloc(nb) || !c.alloc(nc)) {
        (void)cudaGetLastError();
        return MJ_E_LIMIT;
    }
    if (cublasCreate(&h.h) != CUBLAS_STATUS_SUCCESS) return MJ_E_NOTFOUND;
    // no tensor-core shortcuts: plain double precision
    (void)cublasSetMathMode(h.h, CUBLAS_PEDANTIC_MATH);
    if (cudaMemcpy(a.p, A, na * sizeof(double), cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(b.p, B, nb * sizeof(double), cudaMemcpyHostToDevice) != cudaSuccess)
        return MJ_E_IO;
    const double one = 1.0, zero = 0.0;
    cublasStatus_t s;
    if (nt) // row-major C (p x q) = A B^T  <=>  column-major C^T (q x p) = B A^T
        s = cublasDgemm(h.h, CUBLAS_OP_T, CUBLAS_OP_N, (int)q, (int)p, (int)d, &one, b.p, (int)d, a.p, (int)d, &zero,
                        c.p, (int)q);
    else // row-major C (p x d) = A B  <=>  column-major C^T (d x p) = B^T A^T
        s = cublasDgemm(h.h, CUBLAS_OP_N, CUBLAS_OP_N, (int)d, (int)p, (int)q, &one, b.p, (int)d, a.p, (int)q, &zero,
                        c.p, (int)d);
    if (s != CUBLAS_STATUS_SUCCESS || cudaDeviceSynchronize() != cudaSuccess) return MJ_E_IO;
    if (cudaMemcpy(C, c.p, nc * sizeof(double), cudaMemcpyDeviceToHost) != cudaSuccess) return MJ_E_IO;
    return MJ_OK;
}

} // namespace

extern "C" int mj_lr_cuda_available(void) {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess) {
        (void)cudaGetLastError();
        return 0;
    }
    return n > 0;
}

extern "C" mj_status mj_lr_gemm_nt_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q,
                                        uint32_t d) {
    return run(true, A, B, C, p, q, d);
}

extern "C" mj_status mj_lr_gemm_nn_cuda(const double *A, const double *B, double *C, uint32_t p, uint32_t q,
                                        uint32_t d) {
    return run(false, A, B, C, p, q, d);
}
