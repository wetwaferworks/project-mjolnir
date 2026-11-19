/* Mjolnir: just enough of the safetensors format to read a header and find
 * one tensor's bytes. Every offset, size and shape is checked against the
 * file before anything is read or written at it:
 *   - the header length fits the file and the 100 MiB ceiling,
 *   - each tensor has begin <= end <= the data area,
 *   - end - begin equals the element count times the dtype size,
 *   - shapes are whole, non-negative numbers whose product does not overflow,
 *   - no two tensors overlap, and no name appears twice.
 * The header is parsed by a small strict JSON reader of its own.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_SAFET_H
#define MJ_SAFET_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_ST_HEADER_MAX (100u * 1024u * 1024u)
#define MJ_ST_TENSORS_MAX (1u << 20)
#define MJ_ST_DIMS_MAX 8u
#define MJ_ST_DEPTH_MAX 64u /* nesting inside __metadata__ */

typedef enum mj_st_dtype {
    MJ_ST_BF16 = 1,
    MJ_ST_F16,
    MJ_ST_F32,
    MJ_ST_I64,
    MJ_ST_I32,
    MJ_ST_U8,
    MJ_ST_I8,
    MJ_ST_BOOL
} mj_st_dtype;

size_t mj_st_dtype_size(mj_st_dtype d);
const char *mj_st_dtype_name(mj_st_dtype d);

typedef struct mj_st_tensor {
    const char *name;     /* UTF-8, NUL-terminated, owned by the header */
    mj_st_dtype dtype;
    uint32_t ndim;
    uint64_t shape[MJ_ST_DIMS_MAX];
    uint64_t count;       /* product of shape (1 for a scalar) */
    uint64_t begin, end;  /* data_offsets, relative to the data area */
    uint64_t abs_offset;  /* 8 + header length + begin */
    uint64_t nbytes;      /* end - begin */
} mj_st_tensor;

typedef struct mj_st_header {
    mj_budget *budget;
    uint64_t header_len;  /* the JSON length from the first 8 bytes */
    uint64_t data_start;  /* 8 + header_len */
    uint64_t file_size;
    size_t count;         /* tensors, sorted by name (byte order) */
    size_t cap;
    mj_st_tensor *t;
    char *names;
    size_t names_len;
} mj_st_header;

/* Parse the JSON header of a file of file_size bytes. json is the header_len
 * bytes that follow the 8-byte length. On failure nothing is left allocated. */
mj_status mj_st_parse(const char *json, size_t header_len, uint64_t file_size, mj_budget *b, mj_st_header *out);

/* Read and parse the header of the file at path. */
mj_status mj_st_read_header(const char *path, mj_budget *b, mj_st_header *out);

void mj_st_free(mj_st_header *h);

/* The tensor with this name, or NULL. */
const mj_st_tensor *mj_st_find(const mj_st_header *h, const char *name);

#ifdef __cplusplus
}
#endif
#endif
