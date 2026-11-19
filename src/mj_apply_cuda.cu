// The GPU path of mj_apply: one thread per output element, the same
// operations in the same order as the CPU path. The intrinsics below round
// every multiply and add on its own (no fused multiply-add), whatever flags
// the compiler gets.
// MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_bf16.h"

#include <cuda_runtime.h>

#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

struct dterm {
    const float *U;
    const float *V;
    uint32_t rank;
    float scale;
};

__global__ void apply_kernel(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols, const dterm *t,
                             int nt) {
    const uint32_t c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= cols) return;
    for (uint32_t r = blockIdx.y; r < rows; r += gridDim.y) {
        float s = 0.0f;
        for (int i = 0; i < nt; ++i) {
            const float *u = t[i].U + (size_t)r * t[i].rank;
            const float *v = t[i].V + c;
            double acc = 0.0;
            for (uint32_t k = 0; k < t[i].rank; ++k)
                acc = __dadd_rn(acc, __dmul_rn((double)u[k], (double)v[(size_t)k * cols]));
            float d = __double2float_rn(acc);
            d = __fmul_rn(d, t[i].scale);
            s = __fadd_rn(s, d);
        }
        const size_t at = (size_t)r * cols + c;
        out[at] = mj_f32_to_bf16(__fadd_rn(mj_bf16_to_f32(orig[at]), s));
    }
}

struct dev_buf {
    void *p = nullptr;
    ~dev_buf() {
        if (p) cudaFree(p);
    }
};

} // namespace

extern "C" int mj_apply_cuda_available(void) {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess) {
        (void)cudaGetLastError();
        return 0;
    }
    return n > 0;
}

extern "C" mj_status mj_apply_cuda(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols,
                                   const mj_apply_term *t, size_t nt, uint64_t *device_peak) {
    if (device_peak) *device_peak = 0;
    if (orig == NULL || out == NULL || rows == 0 || cols == 0 || (nt && t == NULL) || nt > 4096) return MJ_E_INVAL;
    for (size_t i = 0; i < nt; ++i)
        if (t[i].U == NULL || t[i].V == NULL || t[i].rank == 0) return MJ_E_INVAL;
    if (!mj_apply_cuda_available()) return MJ_E_NOTFOUND;

    const size_t elems = (size_t)rows * cols, tensor_bytes = elems * sizeof(uint16_t);
    size_t floats = 0;
    for (size_t i = 0; i < nt; ++i) floats += (size_t)t[i].rank * ((size_t)rows + cols);
    const size_t total = 2 * tensor_bytes + floats * sizeof(float) + (nt ? nt : 1) * sizeof(dterm);

    dev_buf d_orig, d_out, d_f, d_t;
    if (cudaMalloc(&d_orig.p, tensor_bytes) != cudaSuccess || cudaMalloc(&d_out.p, tensor_bytes) != cudaSuccess ||
        cudaMalloc(&d_f.p, floats ? floats * sizeof(float) : 4) != cudaSuccess ||
        cudaMalloc(&d_t.p, (nt ? nt : 1) * sizeof(dterm)) != cudaSuccess) {
        (void)cudaGetLastError();
        return MJ_E_LIMIT;
    }
    std::vector<dterm> host(nt ? nt : 1);
    float *fp = (float *)d_f.p;
    size_t off = 0;
    for (size_t i = 0; i < nt; ++i) {
        const size_t nu = (size_t)rows * t[i].rank, nv = (size_t)t[i].rank * cols;
        if (cudaMemcpy(fp + off, t[i].U, nu * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess ||
            cudaMemcpy(fp + off + nu, t[i].V, nv * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess)
            return MJ_E_IO;
        host[i].U = fp + off;
        host[i].V = fp + off + nu;
        host[i].rank = t[i].rank;
        host[i].scale = t[i].scale;
        off += nu + nv;
    }
    if (cudaMemcpy(d_orig.p, orig, tensor_bytes, cudaMemcpyHostToDevice) != cudaSuccess ||
        cudaMemcpy(d_t.p, host.data(), host.size() * sizeof(dterm), cudaMemcpyHostToDevice) != cudaSuccess)
        return MJ_E_IO;
    dim3 block(256), grid((cols + 255) / 256, rows < 65535u ? rows : 65535u);
    apply_kernel<<<grid, block>>>((const uint16_t *)d_orig.p, (uint16_t *)d_out.p, rows, cols, (const dterm *)d_t.p,
                                  (int)nt);
    if (cudaGetLastError() != cudaSuccess || cudaDeviceSynchronize() != cudaSuccess) return MJ_E_IO;
    if (cudaMemcpy(out, d_out.p, tensor_bytes, cudaMemcpyDeviceToHost) != cudaSuccess) return MJ_E_IO;
    if (device_peak) *device_peak = total;
    return MJ_OK;
}
