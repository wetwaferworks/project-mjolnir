// The hemisphere batch scorer on the GPU: the move of k toy policies for n
// states, one thread per (policy, state). Integer math only (int64 sums in the
// same order as mj_toy_act, ties to the lower move), so the GPU and the CPU
// give the same moves bit for bit; test_hemi checks it.
// Worth it only for big holdouts: the policies are 32 weights each.
// MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
#include "mjolnir/mj_hemi.h"

#include <cuda_runtime.h>
#include <stdio.h>

namespace {

__constant__ int32_t c_weights[64 * MJ_TOY_A * MJ_TOY_F];

__global__ void act_kernel(uint32_t k, const int8_t *__restrict__ states, uint32_t n, uint8_t *__restrict__ out) {
    const uint64_t id = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (id >= (uint64_t)k * n) return;
    const uint32_t m = (uint32_t)(id / n), i = (uint32_t)(id % n);
    // eight feature bytes in one 64-bit load (the states are 8-byte aligned)
    const uint64_t packed = reinterpret_cast<const uint64_t *>(states)[i];
    int64_t best = 0;
    uint8_t arg = 0;
    for (uint32_t a = 0; a < MJ_TOY_A; ++a) {
        int64_t z = 0;
        for (uint32_t f = 0; f < MJ_TOY_F; ++f) {
            const int8_t x = (int8_t)(uint8_t)(packed >> (8u * f));
            z += (int64_t)c_weights[(m * MJ_TOY_A + a) * MJ_TOY_F + f] * (int64_t)x;
        }
        if (a == 0 || z > best) { best = z; arg = (uint8_t)a; }
    }
    out[(size_t)m * n + i] = arg;
}

}  // namespace

extern "C" int mj_hemi_gpu_built(void) { return 1; }

extern "C" mj_status mj_hemi_toy_act_batch_gpu(const mj_toy_model *models, uint32_t k, const int8_t *states,
                                               uint32_t n, uint8_t *actions, char err[128]) {
    int8_t *d_states = nullptr;
    uint8_t *d_out = nullptr;
    int devices = 0;
    cudaError_t e;
    if (models == nullptr || states == nullptr || actions == nullptr || k == 0 || k > 64) {
        if (err) snprintf(err, 128, "bad arguments (at most 64 policies)");
        return MJ_E_INVAL;
    }
    if (n == 0) return MJ_OK;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
        if (err) snprintf(err, 128, "no CUDA device");
        return MJ_E_INVAL;
    }
    static_assert(sizeof(mj_toy_model) == MJ_TOY_A * MJ_TOY_F * sizeof(int32_t), "packed toy weights");
    e = cudaMemcpyToSymbol(c_weights, models, (size_t)k * sizeof(mj_toy_model));
    if (e == cudaSuccess) e = cudaMalloc(&d_states, (size_t)n * MJ_TOY_F);
    if (e == cudaSuccess) e = cudaMalloc(&d_out, (size_t)n * k);
    if (e == cudaSuccess) e = cudaMemcpy(d_states, states, (size_t)n * MJ_TOY_F, cudaMemcpyHostToDevice);
    if (e == cudaSuccess) {
        const uint64_t total = (uint64_t)k * n;
        const unsigned threads = 256;
        const unsigned blocks = (unsigned)((total + threads - 1) / threads);
        act_kernel<<<blocks, threads>>>(k, d_states, n, d_out);
        e = cudaGetLastError();
        if (e == cudaSuccess) e = cudaDeviceSynchronize();
    }
    if (e == cudaSuccess) e = cudaMemcpy(actions, d_out, (size_t)n * k, cudaMemcpyDeviceToHost);
    if (d_states) cudaFree(d_states);
    if (d_out) cudaFree(d_out);
    if (e != cudaSuccess) {
        if (err) snprintf(err, 128, "CUDA: %s", cudaGetErrorString(e));
        return MJ_E_IO;
    }
    return MJ_OK;
}
