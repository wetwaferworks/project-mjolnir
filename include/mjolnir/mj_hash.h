/* Mjolnir M0: SHA-256 (FIPS 180-4) and CRC-32 (IEEE 802.3), written in-house.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_HASH_H
#define MJ_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_SHA256_LEN 32u
#define MJ_SHA256_HEX 65u /* 64 hex chars + NUL */

typedef struct mj_sha256_ctx {
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    uint32_t buflen;
} mj_sha256_ctx;

void mj_sha256_init(mj_sha256_ctx *c);
void mj_sha256_update(mj_sha256_ctx *c, const void *data, size_t n);
void mj_sha256_final(mj_sha256_ctx *c, uint8_t out[MJ_SHA256_LEN]);
void mj_sha256(const void *data, size_t n, uint8_t out[MJ_SHA256_LEN]);
void mj_sha256_hex(const void *data, size_t n, char out[MJ_SHA256_HEX]);

uint32_t mj_crc32_update(uint32_t crc, const void *data, size_t n); /* start with 0 */
uint32_t mj_crc32(const void *data, size_t n);

#ifdef __cplusplus
}
#endif
#endif
