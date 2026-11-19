/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mjolnir/mj_fs.h"

#include <string.h>

mj_status mj_path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t la, lb;
    if (out == NULL || a == NULL || b == NULL || cap == 0) return MJ_E_INVAL;
    la = strlen(a);
    lb = strlen(b);
    if (la == 0 || la + 1 + lb + 1 > cap || la + 1 + lb + 1 > MJ_PATH_MAX) return MJ_E_LIMIT;
    memcpy(out, a, la);
    out[la] = '/';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return MJ_OK;
}

/* A temporary name next to path that no other writer uses: the process id
 * and a per-process counter, so two writers of the same file never share
 * (or delete) each other's temporary. */
#include <stdio.h>
static int make_tmp_n(char *tmp, size_t cap, const char *path, unsigned long pid, unsigned long long n) {
    int w = snprintf(tmp, cap, "%s.%lu.%llu.tmp", path, pid, n);
    return w > 0 && (size_t)w < cap && (size_t)w < MJ_PATH_MAX;
}

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static volatile LONG64 tmp_counter;
static int make_tmp(char *tmp, size_t cap, const char *path) {
    return make_tmp_n(tmp, cap, path, (unsigned long)GetCurrentProcessId(),
                      (unsigned long long)InterlockedIncrement64(&tmp_counter));
}

static int widen(const char *s, wchar_t *w, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, cap);
    if (n <= 0) return 0;
    for (int i = 0; i < n; ++i) if (w[i] == L'/') w[i] = L'\\';
    return 1;
}
#define WPATH(var, src)                                   \
    wchar_t var[MJ_PATH_MAX];                             \
    if (!widen((src), var, (int)MJ_PATH_MAX)) return MJ_E_INVAL

mj_status mj_fs_mkdir(const char *path) {
    WPATH(w, path);
    if (CreateDirectoryW(w, NULL)) return MJ_OK;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD a = GetFileAttributesW(w);
        return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? MJ_OK : MJ_E_EXISTS;
    }
    return MJ_E_IO;
}

int mj_fs_exists(const char *path) {
    wchar_t w[MJ_PATH_MAX];
    if (!widen(path, w, (int)MJ_PATH_MAX)) return 0;
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

mj_status mj_fs_size(const char *path, uint64_t *out) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    WPATH(w, path);
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &d)) return MJ_E_NOTFOUND;
    *out = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return MJ_OK;
}

/* a missing file or a missing folder on the way to it, as ENOENT on POSIX */
static int win_absent(void) {
    DWORD e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

mj_status mj_fs_remove(const char *path) {
    WPATH(w, path);
    if (DeleteFileW(w)) return MJ_OK;
    return win_absent() ? MJ_OK : MJ_E_IO;
}

mj_status mj_fs_free_bytes(const char *path, uint64_t *out) {
    ULARGE_INTEGER avail;
    WPATH(w, path);
    if (!GetDiskFreeSpaceExW(w, &avail, NULL, NULL)) return MJ_E_IO;
    *out = avail.QuadPart;
    return MJ_OK;
}

mj_status mj_fs_read_all(const char *path, size_t max, mj_budget *b, uint8_t **out, size_t *len) {
    HANDLE h;
    LARGE_INTEGER sz;
    uint8_t *buf;
    size_t got = 0;
    WPATH(w, path);
    *out = NULL;
    *len = 0;
    h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return win_absent() ? MJ_E_NOTFOUND : MJ_E_IO;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) { CloseHandle(h); return MJ_E_IO; }
    if ((uint64_t)sz.QuadPart > (uint64_t)max) { CloseHandle(h); return MJ_E_LIMIT; }
    buf = (uint8_t *)mj_balloc(b, sz.QuadPart ? (size_t)sz.QuadPart : 1u);
    if (buf == NULL) { CloseHandle(h); return MJ_E_LIMIT; }
    while (got < (size_t)sz.QuadPart) {
        DWORD want = (DWORD)(((size_t)sz.QuadPart - got) > 0x40000000u ? 0x40000000u : ((size_t)sz.QuadPart - got));
        DWORD n = 0;
        if (!ReadFile(h, buf + got, want, &n, NULL) || n == 0) {
            CloseHandle(h);
            mj_bfree(b, buf, sz.QuadPart ? (size_t)sz.QuadPart : 1u);
            return MJ_E_IO;
        }
        got += n;
    }
    CloseHandle(h);
    *out = buf;
    *len = got;
    return MJ_OK;
}

static mj_status write_handle(HANDLE h, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    while (n) {
        DWORD want = (DWORD)(n > 0x40000000u ? 0x40000000u : n), put = 0;
        if (!WriteFile(h, p, want, &put, NULL) || put == 0) return MJ_E_IO;
        p += put;
        n -= put;
    }
    return FlushFileBuffers(h) ? MJ_OK : MJ_E_IO;
}

mj_status mj_fs_write_atomic(const char *path, const void *data, size_t n, int replace) {
    char tmp[MJ_PATH_MAX];
    wchar_t wt[MJ_PATH_MAX];
    HANDLE h;
    mj_status st;
    DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    WPATH(w, path);
    if (!make_tmp(tmp, sizeof tmp, path) || !widen(tmp, wt, (int)MJ_PATH_MAX)) return MJ_E_LIMIT;
    if (!replace && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES) return MJ_E_EXISTS;
    h = CreateFileW(wt, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return MJ_E_IO;
    st = write_handle(h, data, n);
    CloseHandle(h);
    if (st != MJ_OK) { DeleteFileW(wt); return st; }
    if (!MoveFileExW(wt, w, flags)) {
        DWORD e = GetLastError();
        DeleteFileW(wt);
        return (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) ? MJ_E_EXISTS : MJ_E_IO;
    }
    return MJ_OK;
}

mj_status mj_fs_append_durable(const char *path, const void *data, size_t n) {
    HANDLE h;
    mj_status st;
    WPATH(w, path);
    h = CreateFileW(w, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return MJ_E_IO;
    st = write_handle(h, data, n);
    CloseHandle(h);
    return st;
}


mj_status mj_fs_create_exclusive(const char *path, const void *data, size_t n) {
    HANDLE h;
    mj_status st;
    WPATH(w, path);
    h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) ? MJ_E_EXISTS : MJ_E_IO;
    }
    st = write_handle(h, data, n);
    CloseHandle(h);
    if (st != MJ_OK) DeleteFileW(w);
    return st;
}

mj_status mj_fs_truncate(const char *path, uint64_t size) {
    HANDLE h;
    LARGE_INTEGER cur, at;
    BY_HANDLE_FILE_INFORMATION fi;
    mj_status st = MJ_OK;
    WPATH(w, path);
    h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h == INVALID_HANDLE_VALUE) return win_absent() ? MJ_E_NOTFOUND : MJ_E_IO;
    if (!GetFileInformationByHandle(h, &fi) || (fi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !GetFileSizeEx(h, &cur))
        st = MJ_E_IO;
    else if ((uint64_t)cur.QuadPart < size) st = MJ_E_INVAL;
    else {
        at.QuadPart = (LONGLONG)size;
        if (!SetFilePointerEx(h, at, NULL, FILE_BEGIN) || !SetEndOfFile(h) || !FlushFileBuffers(h)) st = MJ_E_IO;
    }
    CloseHandle(h);
    return st;
}

mj_status mj_fs_rename_noreplace(const char *from, const char *to) {
    WPATH(wf, from);
    WPATH(wt, to);
    if (MoveFileExW(wf, wt, MOVEFILE_WRITE_THROUGH)) return MJ_OK;
    {
        DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) return MJ_E_EXISTS;
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? MJ_E_NOTFOUND : MJ_E_IO;
    }
}

static uint64_t ft64(FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

void mj_proc_self(uint64_t *pid, uint64_t *start) {
    FILETIME c, e, k, u;
    *pid = (uint64_t)GetCurrentProcessId();
    *start = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? ft64(c) : 0;
}

int mj_proc_running(uint64_t pid, uint64_t start) {
    HANDLE h;
    DWORD code = 0;
    FILETIME c, e, k, u;
    int r;
    if (pid == 0 || pid > 0xFFFFFFFFu) return 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (h == NULL) return GetLastError() == ERROR_INVALID_PARAMETER ? 0 : -1; /* no such process / cannot look */
    if (!GetExitCodeProcess(h, &code)) r = -1;
    else if (code != STILL_ACTIVE) r = 0;
    else if (start == 0) r = 1;
    else if (!GetProcessTimes(h, &c, &e, &k, &u)) r = -1;
    else r = ft64(c) == start ? 1 : 0;
    CloseHandle(h);
    return r;
}

void mj_host_name(char *out, size_t cap) {
    wchar_t w[256];
    char a[256];
    DWORD n = 256;
    size_t i, k = 0;
    if (cap == 0) return;
    if (!GetComputerNameW(w, &n) || WideCharToMultiByte(CP_UTF8, 0, w, -1, a, (int)sizeof a, NULL, NULL) <= 0) a[0] = '\0';
    for (i = 0; a[i] && k + 1 < cap; ++i) {
        char ch = a[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')
            out[k++] = ch;
    }
    out[k] = '\0';
    if (k == 0) snprintf(out, cap, "unknown-host");
}

mj_status mj_fs_list(const char *dir, size_t max_entries, mj_fs_list_fn fn, void *user) {
    char pat[MJ_PATH_MAX];
    wchar_t wp[MJ_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    size_t seen = 0;
    mj_status st = MJ_OK;
    if (mj_path_join(pat, sizeof pat, dir, "*") != MJ_OK || !widen(pat, wp, (int)MJ_PATH_MAX)) return MJ_E_LIMIT;
    h = FindFirstFileW(wp, &fd);
    if (h == INVALID_HANDLE_VALUE) return win_absent() ? MJ_OK : MJ_E_IO;
    do {
        char name[MJ_PATH_MAX];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, (int)sizeof name, NULL, NULL) <= 0) continue;
        if (++seen > max_entries) { st = MJ_E_LIMIT; break; }
        if (fn(name, user)) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return st;
}

mj_status mj_fs_list_typed(const char *dir, size_t max_entries, mj_fs_list_typed_fn fn, void *user) {
    char pat[MJ_PATH_MAX];
    wchar_t wp[MJ_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    size_t seen = 0;
    mj_status st = MJ_OK;
    if (mj_path_join(pat, sizeof pat, dir, "*") != MJ_OK || !widen(pat, wp, (int)MJ_PATH_MAX)) return MJ_E_LIMIT;
    h = FindFirstFileW(wp, &fd);
    if (h == INVALID_HANDLE_VALUE) return win_absent() ? MJ_OK : MJ_E_IO;
    do {
        char name[MJ_PATH_MAX];
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, (int)sizeof name, NULL, NULL) <= 0) continue;
        if (++seen > max_entries) { st = MJ_E_LIMIT; break; }
        if (fn(name, is_dir, user)) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return st;
}

uint64_t mj_clock_ms(void) { return (uint64_t)GetTickCount64(); }

#else /* POSIX */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static unsigned long long tmp_counter;
static int make_tmp(char *tmp, size_t cap, const char *path) {
    return make_tmp_n(tmp, cap, path, (unsigned long)getpid(),
                      (unsigned long long)__atomic_add_fetch(&tmp_counter, 1, __ATOMIC_RELAXED));
}

mj_status mj_fs_mkdir(const char *path) {
    struct stat s;
    if (mkdir(path, 0700) == 0) return MJ_OK;
    if (errno == EEXIST && stat(path, &s) == 0) return S_ISDIR(s.st_mode) ? MJ_OK : MJ_E_EXISTS;
    return MJ_E_IO;
}

int mj_fs_exists(const char *path) {
    struct stat s;
    return lstat(path, &s) == 0;
}

mj_status mj_fs_size(const char *path, uint64_t *out) {
    struct stat s;
    if (stat(path, &s) != 0) return MJ_E_NOTFOUND;
    *out = (uint64_t)s.st_size;
    return MJ_OK;
}

mj_status mj_fs_remove(const char *path) {
    if (unlink(path) == 0 || errno == ENOENT) return MJ_OK;
    return MJ_E_IO;
}

mj_status mj_fs_free_bytes(const char *path, uint64_t *out) {
    struct statvfs v;
    if (statvfs(path, &v) != 0) return MJ_E_IO;
    *out = (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
    return MJ_OK;
}

mj_status mj_fs_read_all(const char *path, size_t max, mj_budget *b, uint8_t **out, size_t *len) {
    struct stat s;
    uint8_t *buf;
    size_t got = 0, sz;
    int fd;
    *out = NULL;
    *len = 0;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return (errno == ENOENT) ? MJ_E_NOTFOUND : MJ_E_IO;
    if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode)) { close(fd); return MJ_E_IO; }
    if ((uint64_t)s.st_size > (uint64_t)max) { close(fd); return MJ_E_LIMIT; }
    sz = (size_t)s.st_size;
    buf = (uint8_t *)mj_balloc(b, sz ? sz : 1u);
    if (buf == NULL) { close(fd); return MJ_E_LIMIT; }
    while (got < sz) {
        ssize_t n = read(fd, buf + got, sz - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); mj_bfree(b, buf, sz ? sz : 1u); return MJ_E_IO; }
        got += (size_t)n;
    }
    close(fd);
    *out = buf;
    *len = got;
    return MJ_OK;
}

static mj_status write_fd(int fd, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return MJ_E_IO;
        p += w;
        n -= (size_t)w;
    }
    return fsync(fd) == 0 ? MJ_OK : MJ_E_IO;
}

static void sync_parent(const char *path) {
    char d[MJ_PATH_MAX];
    size_t l = strlen(path);
    int fd;
    if (l >= sizeof d) return;
    memcpy(d, path, l + 1);
    while (l > 0 && d[l - 1] != '/') --l;
    if (l == 0) { d[0] = '.'; d[1] = '\0'; } else d[l - 1 == 0 ? 1 : l - 1] = '\0';
    fd = open(d, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) { (void)fsync(fd); close(fd); }
}

mj_status mj_fs_write_atomic(const char *path, const void *data, size_t n, int replace) {
    char tmp[MJ_PATH_MAX];
    int fd;
    mj_status st;
    if (!make_tmp(tmp, sizeof tmp, path)) return MJ_E_LIMIT;
    if (!replace && mj_fs_exists(path)) return MJ_E_EXISTS;
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return MJ_E_IO;
    st = write_fd(fd, data, n);
    close(fd);
    if (st != MJ_OK) { unlink(tmp); return st; }
    if (replace) {
        if (rename(tmp, path) != 0) { unlink(tmp); return MJ_E_IO; }
    } else {
        /* link() fails if the target exists, so a version can never be overwritten */
        if (link(tmp, path) != 0) {
            int e = errno;
            unlink(tmp);
            return (e == EEXIST) ? MJ_E_EXISTS : MJ_E_IO;
        }
        unlink(tmp);
    }
    sync_parent(path);
    return MJ_OK;
}

mj_status mj_fs_append_durable(const char *path, const void *data, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    mj_status st;
    if (fd < 0) return MJ_E_IO;
    st = write_fd(fd, data, n);
    close(fd);
    return st;
}


mj_status mj_fs_create_exclusive(const char *path, const void *data, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    mj_status st;
    if (fd < 0) return errno == EEXIST ? MJ_E_EXISTS : MJ_E_IO;
    st = write_fd(fd, data, n);
    close(fd);
    if (st != MJ_OK) unlink(path);
    else sync_parent(path);
    return st;
}

mj_status mj_fs_truncate(const char *path, uint64_t size) {
    struct stat s;
    mj_status st = MJ_OK;
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? MJ_E_NOTFOUND : MJ_E_IO;
    if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode)) st = MJ_E_IO;
    else if ((uint64_t)s.st_size < size) st = MJ_E_INVAL;
    else if (ftruncate(fd, (off_t)size) != 0 || fsync(fd) != 0) st = MJ_E_IO;
    close(fd);
    return st;
}

mj_status mj_fs_rename_noreplace(const char *from, const char *to) {
    /* link() refuses an existing target; then the old name goes */
    if (link(from, to) != 0) return errno == EEXIST ? MJ_E_EXISTS : (errno == ENOENT ? MJ_E_NOTFOUND : MJ_E_IO);
    if (unlink(from) != 0) { (void)unlink(to); return MJ_E_IO; }
    sync_parent(to);
    return MJ_OK;
}

/* Start time of a process in clock ticks since boot (field 22 of
 * /proc/<pid>/stat); 0 when there is no /proc. */
static uint64_t proc_start_ticks(uint64_t pid) {
    char p[64], buf[1024];
    FILE *f;
    size_t n;
    char *q;
    int field;
    uint64_t v = 0;
    snprintf(p, sizeof p, "/proc/%llu/stat", (unsigned long long)pid);
    f = fopen(p, "r");
    if (f == NULL) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    q = strrchr(buf, ')');
    if (q == NULL) return 0;
    ++q;
    for (field = 2; *q && field < 22; ++q)
        if (*q == ' ') ++field;
    while (*q >= '0' && *q <= '9') { v = v * 10u + (uint64_t)(*q - '0'); ++q; }
    return v;
}

void mj_proc_self(uint64_t *pid, uint64_t *start) {
    *pid = (uint64_t)getpid();
    *start = proc_start_ticks(*pid);
}

#include <signal.h>
int mj_proc_running(uint64_t pid, uint64_t start) {
    uint64_t now;
    if (pid == 0 || pid > 0x7FFFFFFFu) return 0;
    if (kill((pid_t)pid, 0) != 0 && errno == ESRCH) return 0;
    if (start == 0) return 1;
    now = proc_start_ticks(pid);
    if (now == 0) return -1;
    return now == start ? 1 : 0;
}

void mj_host_name(char *out, size_t cap) {
    char a[256];
    size_t i, k = 0;
    if (cap == 0) return;
    if (gethostname(a, sizeof a) != 0) a[0] = '\0';
    a[sizeof a - 1] = '\0';
    for (i = 0; a[i] && k + 1 < cap; ++i) {
        char ch = a[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')
            out[k++] = ch;
    }
    out[k] = '\0';
    if (k == 0) snprintf(out, cap, "unknown-host");
}

mj_status mj_fs_list(const char *dir, size_t max_entries, mj_fs_list_fn fn, void *user) {
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t seen = 0;
    mj_status st = MJ_OK;
    if (d == NULL) return (errno == ENOENT) ? MJ_OK : MJ_E_IO;
    while ((e = readdir(d)) != NULL) {
        char full[MJ_PATH_MAX];
        struct stat s;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (mj_path_join(full, sizeof full, dir, e->d_name) != MJ_OK) continue;
        if (lstat(full, &s) != 0 || !S_ISREG(s.st_mode)) continue;
        if (++seen > max_entries) { st = MJ_E_LIMIT; break; }
        if (fn(e->d_name, user)) break;
    }
    closedir(d);
    return st;
}

mj_status mj_fs_list_typed(const char *dir, size_t max_entries, mj_fs_list_typed_fn fn, void *user) {
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t seen = 0;
    mj_status st = MJ_OK;
    if (d == NULL) return (errno == ENOENT || errno == ENOTDIR) ? MJ_OK : MJ_E_IO;
    while ((e = readdir(d)) != NULL) {
        char full[MJ_PATH_MAX];
        struct stat s;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (mj_path_join(full, sizeof full, dir, e->d_name) != MJ_OK) continue;
        if (lstat(full, &s) != 0 || !(S_ISREG(s.st_mode) || S_ISDIR(s.st_mode))) continue;
        if (++seen > max_entries) { st = MJ_E_LIMIT; break; }
        if (fn(e->d_name, S_ISDIR(s.st_mode), user)) break;
    }
    closedir(d);
    return st;
}

uint64_t mj_clock_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
#endif

#include <stdio.h>
#include <time.h>
mj_status mj_utc_now(char out[21]) {
    time_t t = time(NULL);
    struct tm g;
#if defined(_WIN32)
    if (gmtime_s(&g, &t) != 0) return MJ_E_IO;
#else
    if (gmtime_r(&t, &g) == NULL) return MJ_E_IO;
#endif
    if (snprintf(out, 21, "%04d-%02d-%02dT%02d:%02d:%02dZ", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour,
                 g.tm_min, g.tm_sec) != 20)
        return MJ_E_IO;
    return MJ_OK;
}
