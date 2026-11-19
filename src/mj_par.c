/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mjolnir/mj_par.h"

typedef struct part {
    mj_par_fn fn;
    void *ctx;
    uint32_t lo, hi;
} part;

static void run(part *p) {
    if (p->lo < p->hi) p->fn(p->ctx, p->lo, p->hi);
}

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static DWORD WINAPI thread_main(LPVOID p) {
    run((part *)p);
    return 0;
}
unsigned mj_par_cpus(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? (unsigned)si.dwNumberOfProcessors : 1u;
}
#else
#include <pthread.h>
#include <unistd.h>
static void *thread_main(void *p) {
    run((part *)p);
    return NULL;
}
unsigned mj_par_cpus(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (unsigned)n : 1u;
}
#endif

void mj_par_for(uint32_t n, unsigned threads, mj_par_fn fn, void *ctx) {
    part parts[MJ_PAR_THREADS_MAX];
    unsigned t = threads ? threads : mj_par_cpus(), i, started = 0;
    if (n == 0) return;
    if (t > MJ_PAR_THREADS_MAX) t = MJ_PAR_THREADS_MAX;
    if (t > n) t = n;
    for (i = 0; i < t; ++i) {
        parts[i].fn = fn;
        parts[i].ctx = ctx;
        parts[i].lo = (uint32_t)((uint64_t)n * i / t);
        parts[i].hi = (uint32_t)((uint64_t)n * (i + 1) / t);
    }
    {
#if defined(_WIN32)
        HANDLE h[MJ_PAR_THREADS_MAX];
        for (i = 1; i < t; ++i) {
            h[i] = CreateThread(NULL, 0, thread_main, &parts[i], 0, NULL);
            if (h[i] == NULL) break;
            ++started;
        }
        run(&parts[0]);
        for (i = 1; i <= started; ++i) {
            WaitForSingleObject(h[i], INFINITE);
            CloseHandle(h[i]);
        }
#else
        pthread_t h[MJ_PAR_THREADS_MAX];
        for (i = 1; i < t; ++i) {
            if (pthread_create(&h[i], NULL, thread_main, &parts[i]) != 0) break;
            ++started;
        }
        run(&parts[0]);
        for (i = 1; i <= started; ++i) pthread_join(h[i], NULL);
#endif
    }
    for (i = started + 1; i < t; ++i) run(&parts[i]); /* any that could not start */
}
