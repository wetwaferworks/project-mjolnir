/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "mjolnir/mj_file.h"
#include "mjolnir/mj_fs.h"

#include <stdlib.h>
#include <string.h>

#define MJ_COPY_CHUNK (8u * 1024u * 1024u)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

static int widen(const char *s, wchar_t *w, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, cap);
    if (n <= 0) return 0;
    for (int i = 0; i < n; ++i) if (w[i] == L'/') w[i] = L'\\';
    return 1;
}
#define WPATH(var, src)                                   \
    wchar_t var[MJ_PATH_MAX];                             \
    if (!widen((src), var, (int)MJ_PATH_MAX)) return MJ_E_INVAL

mj_status mj_file_open(mj_file *f, const char *path, int writable) {
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION fi;
    DWORD acc = GENERIC_READ | (writable ? GENERIC_WRITE : 0);
    WPATH(w, path);
    f->h = -1;
    f->writable = writable;
    h = CreateFileW(w, acc, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? MJ_E_NOTFOUND : MJ_E_IO;
    }
    if (!GetFileInformationByHandle(h, &fi) || (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(h);
        return MJ_E_IO;
    }
    f->h = (intptr_t)h;
    return MJ_OK;
}

mj_status mj_file_size(mj_file *f, uint64_t *out) {
    LARGE_INTEGER s;
    if (!GetFileSizeEx((HANDLE)f->h, &s)) return MJ_E_IO;
    *out = (uint64_t)s.QuadPart;
    return MJ_OK;
}

mj_status mj_file_pread(mj_file *f, uint64_t off, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    while (n) {
        OVERLAPPED o;
        DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n, got = 0;
        memset(&o, 0, sizeof o);
        o.Offset = (DWORD)off;
        o.OffsetHigh = (DWORD)(off >> 32);
        if (!ReadFile((HANDLE)f->h, p, want, &got, &o)) {
            return GetLastError() == ERROR_HANDLE_EOF ? MJ_E_CORRUPT : MJ_E_IO;
        }
        if (got == 0) return MJ_E_CORRUPT;
        p += got;
        off += got;
        n -= got;
    }
    return MJ_OK;
}

mj_status mj_file_pwrite(mj_file *f, uint64_t off, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    if (!f->writable) return MJ_E_INVAL;
    while (n) {
        OVERLAPPED o;
        DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n, put = 0;
        memset(&o, 0, sizeof o);
        o.Offset = (DWORD)off;
        o.OffsetHigh = (DWORD)(off >> 32);
        if (!WriteFile((HANDLE)f->h, p, want, &put, &o) || put == 0) return MJ_E_IO;
        p += put;
        off += put;
        n -= put;
    }
    return MJ_OK;
}

mj_status mj_file_sync(mj_file *f) { return FlushFileBuffers((HANDLE)f->h) ? MJ_OK : MJ_E_IO; }

void mj_file_close(mj_file *f) {
    if (f->h != -1) CloseHandle((HANDLE)f->h);
    f->h = -1;
}

mj_status mj_file_identity(const char *path, mj_file_id *out) {
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION fi;
    WPATH(w, path);
    h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? MJ_E_NOTFOUND : MJ_E_IO;
    }
    if (!GetFileInformationByHandle(h, &fi)) { CloseHandle(h); return MJ_E_IO; }
    CloseHandle(h);
    out->dev = fi.dwVolumeSerialNumber;
    out->ino = ((uint64_t)fi.nFileIndexHigh << 32) | fi.nFileIndexLow;
    out->nlink = fi.nNumberOfLinks;
    out->size = ((uint64_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
    return MJ_OK;
}

mj_status mj_file_handle_identity(mj_file *f, mj_file_id *out) {
    BY_HANDLE_FILE_INFORMATION fi;
    if (f == NULL || f->h == -1 || !GetFileInformationByHandle((HANDLE)f->h, &fi)) return MJ_E_IO;
    out->dev = fi.dwVolumeSerialNumber;
    out->ino = ((uint64_t)fi.nFileIndexHigh << 32) | fi.nFileIndexLow;
    out->nlink = fi.nNumberOfLinks;
    out->size = ((uint64_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
    return MJ_OK;
}

static mj_status real_existing(const char *path, char *out, size_t cap) {
    HANDLE h;
    wchar_t r[MJ_PATH_MAX];
    DWORD n;
    const wchar_t *s = r;
    WPATH(w, path);
    h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return MJ_E_NOTFOUND;
    n = GetFinalPathNameByHandleW(h, r, (DWORD)MJ_PATH_MAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= MJ_PATH_MAX) return MJ_E_LIMIT;
    if (wcsncmp(s, L"\\\\?\\UNC\\", 8) == 0) s += 6; /* keep two leading separators */
    else if (wcsncmp(s, L"\\\\?\\", 4) == 0) s += 4;
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)cap, NULL, NULL) <= 0) return MJ_E_LIMIT;
    for (char *c = out; *c; ++c) {
        if (*c == '\\') *c = '/';
        else if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a'); /* the file system ignores case */
    }
    return MJ_OK;
}

mj_status mj_fs_set_readonly(const char *path) {
    DWORD a;
    WPATH(w, path);
    a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return MJ_E_NOTFOUND;
    return SetFileAttributesW(w, a | FILE_ATTRIBUTE_READONLY) ? MJ_OK : MJ_E_IO;
}

mj_status mj_fs_set_writable(const char *path) {
    DWORD a;
    WPATH(w, path);
    a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return MJ_E_NOTFOUND;
    return SetFileAttributesW(w, a & ~(DWORD)FILE_ATTRIBUTE_READONLY) ? MJ_OK : MJ_E_IO;
}

static mj_status create_new(const char *path, mj_file *f) {
    HANDLE h;
    WPATH(w, path);
    h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) ? MJ_E_EXISTS : MJ_E_IO;
    }
    f->h = (intptr_t)h;
    f->writable = 1;
    return MJ_OK;
}

int mj_fs_is_file(const char *path) {
    wchar_t w[MJ_PATH_MAX];
    DWORD a;
    if (!widen(path, w, (int)MJ_PATH_MAX)) return 0;
    a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int mj_fs_is_dir(const char *path) {
    wchar_t w[MJ_PATH_MAX];
    DWORD a;
    if (!widen(path, w, (int)MJ_PATH_MAX)) return 0;
    a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

#else /* POSIX */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

mj_status mj_file_open(mj_file *f, const char *path, int writable) {
    struct stat s;
    int fd;
    f->h = -1;
    f->writable = writable;
    fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? MJ_E_NOTFOUND : MJ_E_IO;
    if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode)) { close(fd); return MJ_E_IO; }
    f->h = fd;
    return MJ_OK;
}

mj_status mj_file_size(mj_file *f, uint64_t *out) {
    struct stat s;
    if (fstat((int)f->h, &s) != 0) return MJ_E_IO;
    *out = (uint64_t)s.st_size;
    return MJ_OK;
}

mj_status mj_file_pread(mj_file *f, uint64_t off, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    while (n) {
        size_t want = n > 0x40000000u ? 0x40000000u : n;
        ssize_t got = pread((int)f->h, p, want, (off_t)off);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) return MJ_E_IO;
        if (got == 0) return MJ_E_CORRUPT;
        p += got;
        off += (uint64_t)got;
        n -= (size_t)got;
    }
    return MJ_OK;
}

mj_status mj_file_pwrite(mj_file *f, uint64_t off, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    if (!f->writable) return MJ_E_INVAL;
    while (n) {
        size_t want = n > 0x40000000u ? 0x40000000u : n;
        ssize_t put = pwrite((int)f->h, p, want, (off_t)off);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) return MJ_E_IO;
        p += put;
        off += (uint64_t)put;
        n -= (size_t)put;
    }
    return MJ_OK;
}

mj_status mj_file_sync(mj_file *f) { return fsync((int)f->h) == 0 ? MJ_OK : MJ_E_IO; }

void mj_file_close(mj_file *f) {
    if (f->h != -1) close((int)f->h);
    f->h = -1;
}

mj_status mj_file_identity(const char *path, mj_file_id *out) {
    struct stat s;
    if (stat(path, &s) != 0) return errno == ENOENT ? MJ_E_NOTFOUND : MJ_E_IO;
    out->dev = (uint64_t)s.st_dev;
    out->ino = (uint64_t)s.st_ino;
    out->nlink = (uint64_t)s.st_nlink;
    out->size = (uint64_t)s.st_size;
    return MJ_OK;
}

mj_status mj_file_handle_identity(mj_file *f, mj_file_id *out) {
    struct stat s;
    if (f == NULL || f->h == -1 || fstat((int)f->h, &s) != 0) return MJ_E_IO;
    out->dev = (uint64_t)s.st_dev;
    out->ino = (uint64_t)s.st_ino;
    out->nlink = (uint64_t)s.st_nlink;
    out->size = (uint64_t)s.st_size;
    return MJ_OK;
}

static mj_status real_existing(const char *path, char *out, size_t cap) {
    char r[PATH_MAX];
    size_t l;
    if (realpath(path, r) == NULL) return MJ_E_NOTFOUND;
    l = strlen(r);
    if (l + 1 > cap) return MJ_E_LIMIT;
    memcpy(out, r, l + 1);
    return MJ_OK;
}

mj_status mj_fs_set_readonly(const char *path) {
    struct stat s;
    if (stat(path, &s) != 0) return MJ_E_NOTFOUND;
    return chmod(path, S_IRUSR) == 0 ? MJ_OK : MJ_E_IO;
}

mj_status mj_fs_set_writable(const char *path) {
    return chmod(path, S_IRUSR | S_IWUSR) == 0 ? MJ_OK : MJ_E_IO;
}

static mj_status create_new(const char *path, mj_file *f) {
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return errno == EEXIST ? MJ_E_EXISTS : MJ_E_IO;
    f->h = fd;
    f->writable = 1;
    return MJ_OK;
}

int mj_fs_is_file(const char *path) {
    struct stat s;
    return stat(path, &s) == 0 && S_ISREG(s.st_mode);
}

int mj_fs_is_dir(const char *path) {
    struct stat s;
    return stat(path, &s) == 0 && S_ISDIR(s.st_mode);
}
#endif

mj_status mj_fs_realpath(const char *path, char *out, size_t cap) {
    char parent[MJ_PATH_MAX];
    const char *leaf;
    size_t l = strlen(path), pl, ll;
    mj_status st;
    if (l == 0 || l >= MJ_PATH_MAX) return MJ_E_INVAL;
    st = real_existing(path, out, cap);
    if (st != MJ_E_NOTFOUND) return st;
    /* not there yet: resolve the parent, then add the last name */
    memcpy(parent, path, l + 1);
    while (l > 1 && (parent[l - 1] == '/' || parent[l - 1] == '\\')) parent[--l] = '\0';
    pl = l;
    while (pl > 0 && parent[pl - 1] != '/' && parent[pl - 1] != '\\') --pl;
    leaf = path + pl;
    ll = l - pl;
    if (ll == 0 || (ll == 1 && leaf[0] == '.') || (ll == 2 && leaf[0] == '.' && leaf[1] == '.')) return MJ_E_INVAL;
    if (pl == 0) {
        parent[0] = '.';
        parent[1] = '\0';
    } else {
        parent[pl > 1 ? pl - 1 : 1] = '\0';
    }
    st = real_existing(parent, out, cap);
    if (st != MJ_OK) return st;
    pl = strlen(out);
    if (pl + 1 + ll + 1 > cap) return MJ_E_LIMIT;
    if (pl == 0 || out[pl - 1] != '/') out[pl++] = '/';
    memcpy(out + pl, leaf, ll);
    out[pl + ll] = '\0';
#if defined(_WIN32)
    for (char *c = out + pl; *c; ++c) {
        if (*c == '\\') *c = '/';
        else if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
#endif
    return MJ_OK;
}

mj_status mj_fs_sha256_file(const char *path, uint8_t out[MJ_SHA256_LEN], uint64_t *size) {
    mj_file f;
    mj_sha256_ctx c;
    uint64_t sz, off = 0;
    uint8_t *buf;
    mj_status st = mj_file_open(&f, path, 0);
    if (st != MJ_OK) return st;
    if ((st = mj_file_size(&f, &sz)) != MJ_OK) { mj_file_close(&f); return st; }
    buf = (uint8_t *)malloc(MJ_COPY_CHUNK);
    if (buf == NULL) { mj_file_close(&f); return MJ_E_LIMIT; }
    mj_sha256_init(&c);
    while (off < sz) {
        size_t n = (sz - off) > MJ_COPY_CHUNK ? MJ_COPY_CHUNK : (size_t)(sz - off);
        if ((st = mj_file_pread(&f, off, buf, n)) != MJ_OK) break;
        mj_sha256_update(&c, buf, n);
        off += n;
    }
    free(buf);
    mj_file_close(&f);
    if (st != MJ_OK) return st;
    mj_sha256_final(&c, out);
    if (size) *size = sz;
    return MJ_OK;
}

mj_status mj_fs_copy_new(const char *src, const char *dst) {
    mj_file in, out;
    uint64_t sz, off = 0;
    uint8_t *buf;
    mj_status st = mj_file_open(&in, src, 0);
    if (st != MJ_OK) return st;
    if ((st = mj_file_size(&in, &sz)) != MJ_OK) { mj_file_close(&in); return st; }
    if ((st = create_new(dst, &out)) != MJ_OK) { mj_file_close(&in); return st; }
    buf = (uint8_t *)malloc(MJ_COPY_CHUNK);
    if (buf == NULL) st = MJ_E_LIMIT;
    while (st == MJ_OK && off < sz) {
        size_t n = (sz - off) > MJ_COPY_CHUNK ? MJ_COPY_CHUNK : (size_t)(sz - off);
        if ((st = mj_file_pread(&in, off, buf, n)) != MJ_OK) break;
        if ((st = mj_file_pwrite(&out, off, buf, n)) != MJ_OK) break;
        off += n;
    }
    if (st == MJ_OK) st = mj_file_sync(&out);
    free(buf);
    mj_file_close(&in);
    mj_file_close(&out);
    if (st != MJ_OK) (void)mj_fs_remove(dst);
    return st;
}

int mj_file_same(const mj_file_id *a, const mj_file_id *b) { return a->dev == b->dev && a->ino == b->ino; }

mj_status mj_file_sha256(mj_file *f, uint8_t out[MJ_SHA256_LEN], uint64_t *size) {
    const size_t chunk = MJ_COPY_CHUNK;
    uint64_t total = 0, done = 0;
    mj_sha256_ctx c;
    uint8_t *buf;
    mj_status st = mj_file_size(f, &total);
    if (st != MJ_OK) return st;
    buf = (uint8_t *)malloc(chunk);
    if (buf == NULL) return MJ_E_LIMIT;
    mj_sha256_init(&c);
    while (done < total && st == MJ_OK) {
        size_t n = (total - done) > chunk ? chunk : (size_t)(total - done);
        st = mj_file_pread(f, done, buf, n);
        if (st == MJ_OK) mj_sha256_update(&c, buf, n);
        done += n;
    }
    free(buf);
    if (st != MJ_OK) return st;
    mj_sha256_final(&c, out);
    if (size) *size = total;
    return MJ_OK;
}
