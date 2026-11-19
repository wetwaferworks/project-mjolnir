/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_lowrank.h"
#include "mjolnir/mj_par.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- products on the CPU ---------------- */

static double dot4(const double *a, const double *b, uint32_t d) {
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    uint32_t k = 0;
    for (; k + 4 <= d; k += 4) {
        s0 += a[k] * b[k];
        s1 += a[k + 1] * b[k + 1];
        s2 += a[k + 2] * b[k + 2];
        s3 += a[k + 3] * b[k + 3];
    }
    for (; k < d; ++k) s0 += a[k] * b[k];
    return (s0 + s1) + (s2 + s3);
}

typedef struct nt_ctx {
    const double *A, *B;
    double *C;
    uint32_t p, q, d;
    int sym;
} nt_ctx;

static void nt_rows(void *vc, uint32_t lo, uint32_t hi) {
    nt_ctx *c = (nt_ctx *)vc;
    uint32_t t, j;
    for (t = lo; t < hi; ++t) {
        if (!c->sym) {
            const double *a = c->A + (size_t)t * c->d;
            for (j = 0; j < c->q; ++j) c->C[(size_t)t * c->q + j] = dot4(a, c->B + (size_t)j * c->d, c->d);
        } else {
            /* task t does rows t and p-1-t of the lower triangle, so the work is even */
            uint32_t rows[2], nr = 1, r;
            rows[0] = t;
            if (c->p - 1 - t != t) rows[nr++] = c->p - 1 - t;
            for (r = 0; r < nr; ++r) {
                uint32_t i = rows[r];
                const double *a = c->A + (size_t)i * c->d;
                for (j = 0; j <= i; ++j) {
                    double v = dot4(a, c->A + (size_t)j * c->d, c->d);
                    c->C[(size_t)i * c->p + j] = v;
                    c->C[(size_t)j * c->p + i] = v;
                }
            }
        }
    }
}

mj_status mj_lr_gemm_nt_cpu(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d, int sym,
                            unsigned threads) {
    nt_ctx c;
    if (A == NULL || B == NULL || C == NULL || p == 0 || q == 0 || d == 0 || (sym && (p != q || A != B)))
        return MJ_E_INVAL;
    c.A = A; c.B = B; c.C = C; c.p = p; c.q = q; c.d = d; c.sym = sym;
    mj_par_for(sym ? (p + 1) / 2 : p, threads, nt_rows, &c);
    return MJ_OK;
}

typedef struct nn_ctx {
    const double *A, *B;
    double *C;
    uint32_t p, q, d;
} nn_ctx;

static void nn_rows(void *vc, uint32_t lo, uint32_t hi) {
    nn_ctx *c = (nn_ctx *)vc;
    uint32_t i, j, k;
    for (i = lo; i < hi; ++i) {
        double *ci = c->C + (size_t)i * c->d;
        for (k = 0; k < c->d; ++k) ci[k] = 0.0;
        for (j = 0; j < c->q; ++j) {
            const double a = c->A[(size_t)i * c->q + j];
            const double *bj = c->B + (size_t)j * c->d;
            for (k = 0; k < c->d; ++k) ci[k] += a * bj[k];
        }
    }
}

mj_status mj_lr_gemm_nn_cpu(const double *A, const double *B, double *C, uint32_t p, uint32_t q, uint32_t d,
                            unsigned threads) {
    nn_ctx c;
    if (A == NULL || B == NULL || C == NULL || p == 0 || q == 0 || d == 0) return MJ_E_INVAL;
    c.A = A; c.B = B; c.C = C; c.p = p; c.q = q; c.d = d;
    mj_par_for(p, threads, nn_rows, &c);
    return MJ_OK;
}

/* ---------------- small dense linear algebra ---------------- */

/* Cholesky of the symmetric positive definite n x n matrix A (row-major),
 * in place: the lower triangle becomes L with A = L L^T. */
static int cholesky(double *A, uint32_t n) {
    uint32_t i, j, k;
    for (j = 0; j < n; ++j) {
        double s = A[(size_t)j * n + j];
        for (k = 0; k < j; ++k) s -= A[(size_t)j * n + k] * A[(size_t)j * n + k];
        if (!(s > 0.0)) return 0;
        s = sqrt(s);
        A[(size_t)j * n + j] = s;
        for (i = j + 1; i < n; ++i) {
            double t = A[(size_t)i * n + j];
            for (k = 0; k < j; ++k) t -= A[(size_t)i * n + k] * A[(size_t)j * n + k];
            A[(size_t)i * n + j] = t / s;
        }
    }
    return 1;
}

/* Solve L L^T X = P for X (n x c, row-major), in place in P. */
static void chol_solve(const double *L, double *P, uint32_t n, uint32_t c) {
    uint32_t i, k, col;
    for (col = 0; col < c; ++col) {
        for (i = 0; i < n; ++i) { /* forward: L y = p */
            double t = P[(size_t)i * c + col];
            for (k = 0; k < i; ++k) t -= L[(size_t)i * n + k] * P[(size_t)k * c + col];
            P[(size_t)i * c + col] = t / L[(size_t)i * n + i];
        }
        for (i = n; i-- > 0;) { /* back: L^T x = y */
            double t = P[(size_t)i * c + col];
            for (k = i + 1; k < n; ++k) t -= L[(size_t)k * n + i] * P[(size_t)k * c + col];
            P[(size_t)i * c + col] = t / L[(size_t)i * n + i];
        }
    }
}

/* Thin Householder QR of A (p x n, column-major, p >= n; destroyed):
 * Q (p x n, column-major, orthonormal columns) and R (n x n, column-major,
 * upper triangular) with A = Q R. vs is scratch of p * n doubles. */
static void qr_thin(double *A, uint32_t p, uint32_t n, double *Q, double *R, double *vs, double *beta) {
    uint32_t i, j, c;
    for (j = 0; j < n; ++j) {
        double *x = A + (size_t)j * p, *v = vs + (size_t)j * p, norm = 0.0, alpha, vn = 0.0;
        for (i = j; i < p; ++i) norm += x[i] * x[i];
        norm = sqrt(norm);
        alpha = x[j] >= 0.0 ? -norm : norm;
        for (i = 0; i < j; ++i) v[i] = 0.0;
        for (i = j; i < p; ++i) v[i] = x[i];
        v[j] -= alpha;
        for (i = j; i < p; ++i) vn += v[i] * v[i];
        beta[j] = vn > 0.0 ? 2.0 / vn : 0.0;
        for (c = j; c < n; ++c) {
            double *a = A + (size_t)c * p, t = 0.0;
            for (i = j; i < p; ++i) t += v[i] * a[i];
            t *= beta[j];
            for (i = j; i < p; ++i) a[i] -= t * v[i];
        }
    }
    for (c = 0; c < n; ++c)
        for (i = 0; i < n; ++i) R[(size_t)c * n + i] = i <= c ? A[(size_t)c * p + i] : 0.0;
    memset(Q, 0, (size_t)p * n * sizeof(double));
    for (c = 0; c < n; ++c) Q[(size_t)c * p + c] = 1.0;
    for (j = n; j-- > 0;) {
        const double *v = vs + (size_t)j * p;
        for (c = 0; c < n; ++c) {
            double *q = Q + (size_t)c * p, t = 0.0;
            for (i = j; i < p; ++i) t += v[i] * q[i];
            t *= beta[j];
            for (i = j; i < p; ++i) q[i] -= t * v[i];
        }
    }
}

/* One-sided Jacobi SVD of the n x n matrix W (column-major, destroyed):
 * W_in = U diag(s) V^T, with U and V column-major and s largest first. */
static void svd_jacobi(double *W, uint32_t n, double *U, double *s, double *V, uint32_t *order, double *tmp) {
    uint32_t i, p, q, sweep;
    memset(V, 0, (size_t)n * n * sizeof(double));
    for (i = 0; i < n; ++i) V[(size_t)i * n + i] = 1.0;
    for (sweep = 0; sweep < 100; ++sweep) {
        int rotated = 0;
        for (p = 0; p + 1 < n; ++p) {
            for (q = p + 1; q < n; ++q) {
                double *wp = W + (size_t)p * n, *wq = W + (size_t)q * n, *vp = V + (size_t)p * n,
                       *vq = V + (size_t)q * n;
                double alpha = 0.0, beta = 0.0, gamma = 0.0, zeta, t, c, sn;
                for (i = 0; i < n; ++i) {
                    alpha += wp[i] * wp[i];
                    beta += wq[i] * wq[i];
                    gamma += wp[i] * wq[i];
                }
                if (gamma == 0.0 || fabs(gamma) <= 1e-15 * sqrt(alpha * beta)) continue;
                rotated = 1;
                zeta = (beta - alpha) / (2.0 * gamma);
                t = (zeta >= 0.0 ? 1.0 : -1.0) / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
                c = 1.0 / sqrt(1.0 + t * t);
                sn = c * t;
                for (i = 0; i < n; ++i) {
                    double a = wp[i], b2 = wq[i];
                    wp[i] = c * a - sn * b2;
                    wq[i] = sn * a + c * b2;
                    a = vp[i];
                    b2 = vq[i];
                    vp[i] = c * a - sn * b2;
                    vq[i] = sn * a + c * b2;
                }
            }
        }
        if (!rotated) break;
    }
    for (i = 0; i < n; ++i) {
        double nrm = 0.0;
        uint32_t k;
        for (k = 0; k < n; ++k) nrm += W[(size_t)i * n + k] * W[(size_t)i * n + k];
        tmp[i] = sqrt(nrm);
        order[i] = i;
    }
    /* largest first; a stable insertion sort keeps ties in column order */
    for (i = 1; i < n; ++i) {
        uint32_t o = order[i], j = i;
        while (j > 0 && tmp[order[j - 1]] < tmp[o]) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = o;
    }
    for (i = 0; i < n; ++i) {
        uint32_t src = order[i], k;
        double sv = tmp[src];
        s[i] = sv;
        for (k = 0; k < n; ++k) U[(size_t)i * n + k] = sv > 0.0 ? W[(size_t)src * n + k] / sv : 0.0;
    }
    /* reorder V's columns the same way, through W (no longer needed) */
    memcpy(W, V, (size_t)n * n * sizeof(double));
    for (i = 0; i < n; ++i) memcpy(V + (size_t)i * n, W + (size_t)order[i] * n, (size_t)n * sizeof(double));
}

/* ---------------- the edit ---------------- */

typedef struct scratch {
    mj_budget *b;
    void *p[32];
    size_t n[32];
    int count;
} scratch;

static double *dalloc(scratch *s, size_t count) {
    double *p;
    if (s->count == 32 || count == 0) return NULL;
    p = (double *)mj_balloc(s->b, count * sizeof(double));
    if (p) {
        s->p[s->count] = p;
        s->n[s->count] = count * sizeof(double);
        ++s->count;
    }
    return p;
}

static void scratch_free(scratch *s) {
    while (s->count > 0) {
        --s->count;
        mj_bfree(s->b, s->p[s->count], s->n[s->count]);
    }
}

void mj_lr_free(mj_lr_result *r) {
    if (r == NULL || r->budget == NULL) return;
    if (r->U) mj_bfree(r->budget, r->U, r->nu * sizeof(float));
    if (r->V) mj_bfree(r->budget, r->V, r->nv * sizeof(float));
    r->U = r->V = NULL;
}

static mj_status gemm_nt(const mj_lr_input *in, int *gpu, const double *A, const double *B, double *C, uint32_t p,
                         uint32_t q, uint32_t d, int sym) {
    if (in->use_cuda && *gpu != 0) {
        mj_status st = mj_lr_gemm_nt_cuda(A, B, C, p, q, d);
        if (st == MJ_OK) { *gpu = 1; return MJ_OK; }
        if (st != MJ_E_NOTFOUND) return st;
        *gpu = 0;
    }
    return mj_lr_gemm_nt_cpu(A, B, C, p, q, d, sym, in->threads);
}

static mj_status gemm_nn(const mj_lr_input *in, int *gpu, const double *A, const double *B, double *C, uint32_t p,
                         uint32_t q, uint32_t d) {
    if (in->use_cuda && *gpu != 0) {
        mj_status st = mj_lr_gemm_nn_cuda(A, B, C, p, q, d);
        if (st == MJ_OK) { *gpu = 1; return MJ_OK; }
        if (st != MJ_E_NOTFOUND) return st;
        *gpu = 0;
    }
    return mj_lr_gemm_nn_cpu(A, B, C, p, q, d, in->threads);
}

mj_status mj_lowrank(const mj_lr_input *in, mj_budget *b, mj_lr_result *res) {
    scratch sc;
    const uint32_t n = in ? in->n : 0, out = in ? in->out : 0, din = in ? in->in : 0;
    double *Gc, *K0, *K, *Qg, *Rg, *Qk, *Rk, *vs, *beta, *M, *u, *v, *sv, *tmp;
    uint32_t *order = NULL;
    uint32_t i, j, t, r;
    int gpu = -1; /* unknown until the first product */
    mj_status st = MJ_OK;
    memset(res, 0, sizeof *res);
    memset(&sc, 0, sizeof sc);
    sc.b = b;
    if (in == NULL || b == NULL || in->k == NULL || in->o == NULL || in->g == NULL) return MJ_E_INVAL;
    if (n == 0 || n > MJ_LR_MAX_EXAMPLES || out < n || din < n || out > MJ_LR_MAX_DIM || din > MJ_LR_MAX_DIM ||
        in->rank == 0 || in->rank > MJ_LR_MAX_RANK)
        return MJ_E_INVAL;
    if (in->m && (in->bg == NULL || !(in->ridge > 0.0) || !isfinite(in->ridge) ||
                  (uint64_t)in->m + in->shared_weight > MJ_LR_MAX_BACKGROUND))
        return MJ_E_INVAL;
    if (in->m == 0 && in->shared_weight) return MJ_E_INVAL;

    Gc = dalloc(&sc, (size_t)out * n);
    K0 = dalloc(&sc, (size_t)n * din);
    K = dalloc(&sc, (size_t)n * din);
    if (!Gc || !K0 || !K) { st = MJ_E_LIMIT; goto done; }
    for (j = 0; j < n; ++j) {
        double gn = 0.0, on = 0.0, f;
        const float *g = in->g + (size_t)j * out, *o = in->o + (size_t)j * out;
        for (i = 0; i < out; ++i) {
            gn += (double)g[i] * g[i];
            on += (double)o[i] * o[i];
        }
        gn = sqrt(gn);
        on = sqrt(on);
        f = gn + 1e-12;
        for (i = 0; i < out; ++i) Gc[(size_t)j * out + i] = (double)g[i] / f * on;
        for (i = 0; i < din; ++i) K0[(size_t)j * din + i] = in->k[(size_t)j * din + i];
    }
    if (in->m == 0) {
        for (j = 0; j < n; ++j) {
            double nn = 0.0;
            const double *k0 = K0 + (size_t)j * din;
            for (i = 0; i < din; ++i) nn += k0[i] * k0[i];
            for (i = 0; i < din; ++i) K[(size_t)j * din + i] = k0[i] / (nn + 1e-12);
        }
    } else {
        const uint32_t mp = in->m + in->shared_weight;
        double *B = dalloc(&sc, (size_t)mp * din), *Gm = dalloc(&sc, (size_t)mp * mp), *P = dalloc(&sc, (size_t)mp * n),
               *St = dalloc(&sc, (size_t)n * mp), lam = 0.0;
        if (!B || !Gm || !P || !St) { st = MJ_E_LIMIT; goto done; }
        for (i = 0; i < in->m; ++i)
            for (t = 0; t < din; ++t) B[(size_t)i * din + t] = in->bg[(size_t)i * din + t];
        if (in->shared_weight) {
            double *mean = B + (size_t)in->m * din;
            for (t = 0; t < din; ++t) {
                double s = 0.0;
                for (j = 0; j < n; ++j) s += K0[(size_t)j * din + t];
                mean[t] = s / n;
            }
            for (i = 1; i < in->shared_weight; ++i) memcpy(B + (size_t)(in->m + i) * din, mean, (size_t)din * sizeof(double));
        }
        for (i = 0; i < mp; ++i)
            for (t = 0; t < din; ++t) lam += B[(size_t)i * din + t] * B[(size_t)i * din + t];
        lam = in->ridge * lam / ((double)mp * din);
        if (!(lam > 0.0)) { st = MJ_E_INVAL; goto done; } /* an all-zero background */
        if ((st = gemm_nt(in, &gpu, B, B, Gm, mp, mp, din, 1)) != MJ_OK) goto done;
        for (i = 0; i < mp; ++i) Gm[(size_t)i * mp + i] += (double)mp * lam;
        if ((st = gemm_nt(in, &gpu, B, K0, P, mp, n, din, 0)) != MJ_OK) goto done;
        if (!cholesky(Gm, mp)) { st = MJ_E_INVAL; goto done; }
        chol_solve(Gm, P, mp, n);
        for (i = 0; i < mp; ++i)
            for (j = 0; j < n; ++j) St[(size_t)j * mp + i] = P[(size_t)i * n + j];
        if ((st = gemm_nn(in, &gpu, St, B, K, n, mp, din)) != MJ_OK) goto done;
        for (j = 0; j < n; ++j) {
            double *kw = K + (size_t)j * din, dotv = 0.0;
            const double *k0 = K0 + (size_t)j * din;
            for (i = 0; i < din; ++i) {
                kw[i] = (k0[i] - kw[i]) / lam;
                dotv += kw[i] * k0[i];
            }
            if (dotv == 0.0 || !isfinite(dotv)) { st = MJ_E_INVAL; goto done; }
            for (i = 0; i < din; ++i) kw[i] /= dotv;
        }
    }
    /* dW = G K = (Qg Rg)(Qk Rk)^T; K^T column-major is K row-major */
    Qg = dalloc(&sc, (size_t)out * n);
    Rg = dalloc(&sc, (size_t)n * n);
    Qk = dalloc(&sc, (size_t)din * n);
    Rk = dalloc(&sc, (size_t)n * n);
    vs = dalloc(&sc, (size_t)(out > din ? out : din) * n);
    beta = dalloc(&sc, n);
    M = dalloc(&sc, (size_t)n * n);
    u = dalloc(&sc, (size_t)n * n);
    v = dalloc(&sc, (size_t)n * n);
    sv = dalloc(&sc, n);
    tmp = dalloc(&sc, n);
    order = (uint32_t *)mj_balloc(b, (size_t)n * sizeof(uint32_t));
    if (!Qg || !Rg || !Qk || !Rk || !vs || !beta || !M || !u || !v || !sv || !tmp || !order) { st = MJ_E_LIMIT; goto done; }
    qr_thin(Gc, out, n, Qg, Rg, vs, beta);
    qr_thin(K, din, n, Qk, Rk, vs, beta);
    for (j = 0; j < n; ++j) /* M = Rg Rk^T, column-major */
        for (i = 0; i < n; ++i) {
            double s = 0.0;
            for (t = 0; t < n; ++t) s += Rg[(size_t)t * n + i] * Rk[(size_t)t * n + j];
            M[(size_t)j * n + i] = s;
        }
    svd_jacobi(M, n, u, sv, v, order, tmp);
    r = in->rank < n ? in->rank : n;
    res->budget = b;
    res->r = r;
    res->nu = (size_t)out * r;
    res->nv = (size_t)r * din;
    res->U = (float *)mj_balloc(b, res->nu * sizeof(float));
    res->V = (float *)mj_balloc(b, res->nv * sizeof(float));
    if (!res->U || !res->V) { mj_lr_free(res); st = MJ_E_LIMIT; goto done; }
    for (i = 0; i < out; ++i) /* U = Qg u[:, :r] diag(s) */
        for (t = 0; t < r; ++t) {
            double s = 0.0;
            for (j = 0; j < n; ++j) s += Qg[(size_t)j * out + i] * u[(size_t)t * n + j];
            res->U[(size_t)i * r + t] = (float)(s * sv[t]);
        }
    for (t = 0; t < r; ++t) /* V = v[:, :r]^T Qk^T */
        for (i = 0; i < din; ++i) {
            double s = 0.0;
            for (j = 0; j < n; ++j) s += v[(size_t)t * n + j] * Qk[(size_t)j * din + i];
            res->V[(size_t)t * din + i] = (float)s;
        }
    for (j = 0; j < n; ++j) res->s[j] = sv[j];
    res->used_cuda = gpu == 1;
done:
    if (order) mj_bfree(b, order, (size_t)n * sizeof(uint32_t));
    scratch_free(&sc);
    return st;
}
