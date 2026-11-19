/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_bf16.h"

#include <stdlib.h>
#include <string.h>

typedef struct job {
    const uint16_t *orig;
    uint16_t *out;
    uint32_t rows, cols, r0, r1;
    const mj_apply_term *t;
    size_t nt;
    int ok;
} job;

/* Rows r0..r1-1. Each element's sums run in the documented order; working a
 * whole row at a time only changes which element is computed first. */
static void run_rows(job *j) {
    double *acc = (double *)malloc((size_t)j->cols * sizeof(double));
    float *s = (float *)malloc((size_t)j->cols * sizeof(float));
    uint32_t r, c, k;
    size_t ti;
    if (acc == NULL || s == NULL) {
        free(acc);
        free(s);
        j->ok = 0;
        return;
    }
    for (r = j->r0; r < j->r1; ++r) {
        const uint16_t *o = j->orig + (size_t)r * j->cols;
        uint16_t *w = j->out + (size_t)r * j->cols;
        for (c = 0; c < j->cols; ++c) s[c] = 0.0f;
        for (ti = 0; ti < j->nt; ++ti) {
            const mj_apply_term *t = &j->t[ti];
            const float *u = t->U + (size_t)r * t->rank;
            for (c = 0; c < j->cols; ++c) acc[c] = 0.0;
            for (k = 0; k < t->rank; ++k) {
                const double uk = (double)u[k];
                const float *v = t->V + (size_t)k * j->cols;
                for (c = 0; c < j->cols; ++c) {
                    double p = uk * (double)v[c];
                    acc[c] = acc[c] + p;
                }
            }
            for (c = 0; c < j->cols; ++c) {
                float d = (float)acc[c];
                d = d * t->scale;
                s[c] = s[c] + d;
            }
        }
        for (c = 0; c < j->cols; ++c) {
            float x = mj_bf16_to_f32(o[c]) + s[c];
            w[c] = mj_f32_to_bf16(x);
        }
    }
    free(acc);
    free(s);
    j->ok = 1;
}

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static DWORD WINAPI thread_main(LPVOID p) {
    run_rows((job *)p);
    return 0;
}
static unsigned cpu_count(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? (unsigned)si.dwNumberOfProcessors : 1u;
}
#else
#include <pthread.h>
#include <unistd.h>
static void *thread_main(void *p) {
    run_rows((job *)p);
    return NULL;
}
static unsigned cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (unsigned)n : 1u;
}
#endif

#define MJ_APPLY_THREADS_MAX 8u

mj_status mj_apply_cpu(const uint16_t *orig, uint16_t *out, uint32_t rows, uint32_t cols, const mj_apply_term *t,
                       size_t nt, unsigned threads) {
    job jobs[MJ_APPLY_THREADS_MAX];
    unsigned n, i, started = 0;
    int ok = 1;
    size_t ti;
    if (orig == NULL || out == NULL || rows == 0 || cols == 0 || (nt && t == NULL)) return MJ_E_INVAL;
    for (ti = 0; ti < nt; ++ti)
        if (t[ti].U == NULL || t[ti].V == NULL || t[ti].rank == 0) return MJ_E_INVAL;
    n = threads ? threads : cpu_count();
    if (n > MJ_APPLY_THREADS_MAX) n = MJ_APPLY_THREADS_MAX;
    if (n > rows) n = rows;
    for (i = 0; i < n; ++i) {
        jobs[i].orig = orig;
        jobs[i].out = out;
        jobs[i].rows = rows;
        jobs[i].cols = cols;
        jobs[i].r0 = (uint32_t)((uint64_t)rows * i / n);
        jobs[i].r1 = (uint32_t)((uint64_t)rows * (i + 1) / n);
        jobs[i].t = t;
        jobs[i].nt = nt;
        jobs[i].ok = 0;
    }
    {
#if defined(_WIN32)
        HANDLE h[MJ_APPLY_THREADS_MAX];
        for (i = 1; i < n; ++i) {
            h[i] = CreateThread(NULL, 0, thread_main, &jobs[i], 0, NULL);
            if (h[i] == NULL) break;
            ++started;
        }
        run_rows(&jobs[0]);
        for (i = 1; i <= started; ++i) {
            WaitForSingleObject(h[i], INFINITE);
            CloseHandle(h[i]);
        }
#else
        pthread_t h[MJ_APPLY_THREADS_MAX];
        for (i = 1; i < n; ++i) {
            if (pthread_create(&h[i], NULL, thread_main, &jobs[i]) != 0) break;
            ++started;
        }
        run_rows(&jobs[0]);
        for (i = 1; i <= started; ++i) pthread_join(h[i], NULL);
#endif
        /* rows of threads that never started are done here */
        for (i = started + 1; i < n; ++i) run_rows(&jobs[i]);
    }
    for (i = 0; i < n; ++i) ok &= jobs[i].ok;
    return ok ? MJ_OK : MJ_E_LIMIT;
}
