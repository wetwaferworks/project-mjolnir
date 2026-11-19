/* Mjolnir: MJDELTA1, a low-rank weight edit small enough for the version
 * store. Byte for byte the format the Python side writes:
 *
 *     MJDELTA1\n
 *     base=<64 hex: SHA-256 of the model file the edit was computed on>\n
 *     count=<number of entries>\n
 *     then per entry: tensor=<name> rows=<r> cols=<c> rank=<k> scale=<float>\n
 *     end\n
 *     then per entry, little-endian float32: U (rows x rank), then V (rank x cols)
 *     then the SHA-256 of every byte before it (32 bytes)
 *
 * scale is written the way Python's repr() writes a float (shortest digits
 * that read back to the same double), so a delta packed here and one packed
 * by Python from the same numbers are the same bytes.
 * The change to a tensor is scale * U @ V.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_DELTA_H
#define MJ_DELTA_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_DELTA_MAX_ENTRIES 256u
#define MJ_DELTA_MAX_RANK 64u      /* what pack accepts */
#define MJ_DELTA_READ_RANK 999u    /* what unpack accepts: the format's three digits */
#define MJ_DELTA_NAME_MAX 200u
#define MJ_DELTA_DIM_MAX 999999u
#define MJ_DELTA_HEAD_MAX 65536u   /* "\nend\n" must start at or before this offset */

typedef struct mj_delta_entry {
    char name[MJ_DELTA_NAME_MAX + 1];
    uint32_t rows, cols, rank;
    double scale;
    const float *U; /* rows x rank, row-major */
    const float *V; /* rank x cols, row-major */
} mj_delta_entry;

typedef struct mj_delta {
    char base[65]; /* lower-case hex */
    uint32_t count;
    mj_delta_entry e[MJ_DELTA_MAX_ENTRIES];
    float *data;   /* all U and V values, owned (budget) */
    size_t data_n; /* floats */
    mj_budget *budget;
} mj_delta;

/* Python's repr(float) for a finite double. out must hold 32 bytes.
 * Returns the length, or 0 for inf/nan. */
size_t mj_float_repr(double x, char out[32]);

/* Pack entries into a new buffer charged to the budget (free with
 * mj_bfree(b, *out, *n)). Refuses what Python's pack refuses, plus
 * non-finite scales and names the format cannot carry. */
mj_status mj_delta_pack(const char *base_hex, const mj_delta_entry *e, uint32_t count, mj_budget *b,
                        uint8_t **out, size_t *n);

/* Parse and check a delta (checksum, header, sizes, no extra bytes). The
 * entries point into d->data. Free with mj_delta_free. */
mj_status mj_delta_unpack(const uint8_t *data, size_t n, mj_budget *b, mj_delta *d);
void mj_delta_free(mj_delta *d);

#ifdef __cplusplus
}
#endif
#endif
