/* Mjolnir M0: the thin platform layer. Every file and clock call goes through
 * here, so the rest of the code never talks to the operating system directly.
 * Windows (Win32) and POSIX. Paths are UTF-8.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_FS_H
#define MJ_FS_H

#include "mjolnir/mj_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Join a and b with one separator. MJ_E_LIMIT if it would not fit. */
mj_status mj_path_join(char *out, size_t cap, const char *a, const char *b);

mj_status mj_fs_mkdir(const char *path);            /* ok if it already exists */
int mj_fs_exists(const char *path);
mj_status mj_fs_size(const char *path, uint64_t *out);
mj_status mj_fs_remove(const char *path);          /* ok if already gone */
mj_status mj_fs_free_bytes(const char *path, uint64_t *out);

/* Read a whole file, refusing anything larger than max. The buffer is charged
 * to the budget; release it with mj_bfree(b, *out, *len ? *len : 1). */
mj_status mj_fs_read_all(const char *path, size_t max, mj_budget *b, uint8_t **out, size_t *len);

/* Atomic write: write <path>.tmp, flush it to the disk, then rename it into
 * place and flush the directory. With replace == 0 an existing target is
 * refused with MJ_E_EXISTS and nothing changes. */
mj_status mj_fs_write_atomic(const char *path, const void *data, size_t n, int replace);

/* Durable append: append n bytes and flush them to the disk before returning. */
mj_status mj_fs_append_durable(const char *path, const void *data, size_t n);

/* Calls fn(name, user) for each regular file in dir (no "." or ".."). Stops
 * early when fn returns non-zero. At most max_entries are visited; more is
 * MJ_E_LIMIT. */
typedef int (*mj_fs_list_fn)(const char *name, void *user);
mj_status mj_fs_list(const char *dir, size_t max_entries, mj_fs_list_fn fn, void *user);

/* Monotonic milliseconds, for time ceilings only (never recorded in hashes). */
uint64_t mj_clock_ms(void);

/* Current UTC time as YYYY-MM-DDTHH:MM:SSZ (21 bytes with the NUL). */
mj_status mj_utc_now(char out[21]);

#ifdef __cplusplus
}
#endif
#endif
