/* Mjolnir: open files read at or written at an offset, plus the few file
 * facts the real-weight path needs (identity, link count, real path,
 * read-only flag, streamed hash and copy). Windows (Win32) and POSIX.
 * Paths are UTF-8.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_FILE_H
#define MJ_FILE_H

#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mj_file {
    intptr_t h;   /* HANDLE on Windows, file descriptor elsewhere; -1 when closed */
    int writable;
} mj_file;

/* Open an existing regular file. writable != 0 opens it for reading and
 * writing in place; it is never created or truncated. */
mj_status mj_file_open(mj_file *f, const char *path, int writable);
mj_status mj_file_size(mj_file *f, uint64_t *out);
/* Exactly n bytes at off, or MJ_E_IO (a short file is MJ_E_CORRUPT). */
mj_status mj_file_pread(mj_file *f, uint64_t off, void *buf, size_t n);
mj_status mj_file_pwrite(mj_file *f, uint64_t off, const void *buf, size_t n);
mj_status mj_file_sync(mj_file *f); /* flush to the disk */
void mj_file_close(mj_file *f);

typedef struct mj_file_id {
    uint64_t dev;   /* volume serial / st_dev */
    uint64_t ino;   /* file index / st_ino */
    uint64_t nlink; /* hard links */
    uint64_t size;
} mj_file_id;

/* Identity of the file at path (links followed). MJ_E_NOTFOUND if missing. */
mj_status mj_file_identity(const char *path, mj_file_id *out);
/* Identity of an open file, so a check made by name can be repeated on the
 * handle actually used (closing the gap between check and open). */
mj_status mj_file_handle_identity(mj_file *f, mj_file_id *out);
/* 1 when a and b are the same file (volume and file index). */
int mj_file_same(const mj_file_id *a, const mj_file_id *b);
/* Stream the whole open file through SHA-256 (size in *size when not NULL). */
mj_status mj_file_sha256(mj_file *f, uint8_t out[MJ_SHA256_LEN], uint64_t *size);

/* The canonical absolute path of path. When path itself does not exist yet,
 * its parent is resolved and the last name appended. Separators are '/'. */
mj_status mj_fs_realpath(const char *path, char *out, size_t cap);

/* Mark a file read-only (what chmod S_IREAD does). */
mj_status mj_fs_set_readonly(const char *path);
/* Undo it (tests only clean up with this). */
mj_status mj_fs_set_writable(const char *path);

/* SHA-256 and size of a file, streamed. */
mj_status mj_fs_sha256_file(const char *path, uint8_t out[MJ_SHA256_LEN], uint64_t *size);

/* Copy src to a new file dst (refused with MJ_E_EXISTS if dst exists),
 * flushed to the disk before returning. */
mj_status mj_fs_copy_new(const char *src, const char *dst);

/* Regular file (links followed)? */
int mj_fs_is_file(const char *path);
int mj_fs_is_dir(const char *path);

#ifdef __cplusplus
}
#endif
#endif
