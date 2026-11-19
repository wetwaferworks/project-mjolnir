/* Mjolnir: bfloat16 <-> float32, the way torch converts them on the CPU.
 * float32 -> bf16 rounds to nearest, ties to even. A NaN becomes the quiet
 * NaN 0x7FC0 (torch's scalar path; its vector path may give 0xFFFF, which
 * real weights never meet). Usable from C, C++ and CUDA.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_BF16_H
#define MJ_BF16_H

#include <stdint.h>
#include <string.h>

#if defined(__CUDACC__)
#define MJ_HD __host__ __device__ __forceinline__
#else
#define MJ_HD static inline
#endif

MJ_HD float mj_bf16_to_f32(uint16_t h) {
    uint32_t u = (uint32_t)h << 16;
    float f;
    memcpy(&f, &u, 4);
    return f;
}

MJ_HD uint16_t mj_f32_to_bf16(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u) return (uint16_t)0x7FC0u;
    u += 0x7FFFu + ((u >> 16) & 1u);
    return (uint16_t)(u >> 16);
}

#endif
