/* The low-rank edit: the products, the whitening, the truncation, against
 * plain textbook formulas on small sizes, and CPU against GPU.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"

#include "mjolnir/mj_lowrank.h"

#include <math.h>

static uint64_t rng = 0x5DEECE66Dull;
static double drand(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (double)(rng >> 11) / 9007199254740992.0 * 2.0 - 1.0;
}

static double rel_err(const double *a, const double *b, size_t n) {
    double num = 0, den = 0;
    size_t i;
    for (i = 0; i < n; ++i) {
        num += (a[i] - b[i]) * (a[i] - b[i]);
        den += b[i] * b[i];
    }
    return sqrt(num / (den > 0 ? den : 1));
}

/* Invert the n x n matrix A (row-major) by Gauss-Jordan with partial pivoting. */
static int invert(double *A, double *X, int n) {
    int i, j, k;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) X[i * n + j] = i == j;
    for (k = 0; k < n; ++k) {
        int piv = k;
        double d;
        for (i = k + 1; i < n; ++i) if (fabs(A[i * n + k]) > fabs(A[piv * n + k])) piv = i;
        if (A[piv * n + k] == 0) return 0;
        for (j = 0; j < n; ++j) {
            double t = A[k * n + j]; A[k * n + j] = A[piv * n + j]; A[piv * n + j] = t;
            t = X[k * n + j]; X[k * n + j] = X[piv * n + j]; X[piv * n + j] = t;
        }
        d = A[k * n + k];
        for (j = 0; j < n; ++j) { A[k * n + j] /= d; X[k * n + j] /= d; }
        for (i = 0; i < n; ++i) {
            double f;
            if (i == k) continue;
            f = A[i * n + k];
            for (j = 0; j < n; ++j) { A[i * n + j] -= f * A[k * n + j]; X[i * n + j] -= f * X[k * n + j]; }
        }
    }
    return 1;
}

/* dW straight from the definition (doubles). */
static void reference(const mj_lr_input *in, double *dW) {
    int n = (int)in->n, out = (int)in->out, d = (int)in->in, i, j, t;
    double *K = (double *)calloc((size_t)n * d, sizeof(double));
    if (in->m == 0) {
        for (j = 0; j < n; ++j) {
            double nn = 0;
            for (t = 0; t < d; ++t) nn += (double)in->k[j * d + t] * in->k[j * d + t];
            for (t = 0; t < d; ++t) K[j * d + t] = in->k[j * d + t] / (nn + 1e-12);
        }
    } else {
        int m = (int)(in->m + in->shared_weight);
        double *B = (double *)calloc((size_t)m * d, sizeof(double)), *C = (double *)calloc((size_t)d * d, sizeof(double)),
               *Ci = (double *)calloc((size_t)d * d, sizeof(double)), lam = 0;
        for (i = 0; i < (int)in->m; ++i) for (t = 0; t < d; ++t) B[i * d + t] = in->bg[i * d + t];
        for (i = (int)in->m; i < m; ++i)
            for (t = 0; t < d; ++t) {
                double s = 0;
                for (j = 0; j < n; ++j) s += in->k[j * d + t];
                B[i * d + t] = s / n;
            }
        for (i = 0; i < m * d; ++i) lam += B[i] * B[i];
        lam = in->ridge * lam / ((double)m * d);
        for (i = 0; i < d; ++i)
            for (j = 0; j < d; ++j) {
                double s = 0;
                for (t = 0; t < m; ++t) s += B[t * d + i] * B[t * d + j];
                C[i * d + j] = s / m + (i == j ? lam : 0);
            }
        invert(C, Ci, d);
        for (j = 0; j < n; ++j) {
            double dotv = 0;
            for (i = 0; i < d; ++i) {
                double s = 0;
                for (t = 0; t < d; ++t) s += Ci[i * d + t] * in->k[j * d + t];
                K[j * d + i] = s;
                dotv += s * in->k[j * d + i];
            }
            for (i = 0; i < d; ++i) K[j * d + i] /= dotv;
        }
        free(B); free(C); free(Ci);
    }
    for (i = 0; i < out; ++i)
        for (t = 0; t < d; ++t) {
            double s = 0;
            for (j = 0; j < n; ++j) {
                double gn = 0, on = 0;
                int q;
                for (q = 0; q < out; ++q) { gn += (double)in->g[j * out + q] * in->g[j * out + q]; on += (double)in->o[j * out + q] * in->o[j * out + q]; }
                s += in->g[j * out + i] / (sqrt(gn) + 1e-12) * sqrt(on) * K[j * d + t];
            }
            dW[i * d + t] = s;
        }
    free(K);
}

static void product(const mj_lr_result *r, uint32_t out, uint32_t in, double *uv) {
    uint32_t i, t, j;
    for (i = 0; i < out; ++i)
        for (t = 0; t < in; ++t) {
            double s = 0;
            for (j = 0; j < r->r; ++j) s += (double)r->U[i * r->r + j] * r->V[j * in + t];
            uv[i * in + t] = s;
        }
}

static float *frand_array(size_t n, double scale) {
    float *p = (float *)malloc(n * sizeof(float));
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (float)(drand() * scale);
    return p;
}

int main(void) {
    mj_budget b;
    mj_budget_init(&b, 256u << 20);

    /* products: CPU against the definition, thread counts against each other, GPU against CPU */
    {
        const uint32_t p = 37, q = 23, d = 301;
        double *A = (double *)malloc(sizeof(double) * p * d), *B = (double *)malloc(sizeof(double) * q * d),
               *C1 = (double *)malloc(sizeof(double) * p * p), *C2 = (double *)malloc(sizeof(double) * p * p),
               *R = (double *)malloc(sizeof(double) * p * p), *N = (double *)malloc(sizeof(double) * p * d);
        uint32_t i, j, k;
        for (i = 0; i < p * d; ++i) A[i] = drand();
        for (i = 0; i < q * d; ++i) B[i] = drand();
        CHECK_ST(mj_lr_gemm_nt_cpu(A, B, C1, p, q, d, 0, 1), MJ_OK);
        CHECK_ST(mj_lr_gemm_nt_cpu(A, B, C2, p, q, d, 0, 3), MJ_OK);
        CHECK(memcmp(C1, C2, sizeof(double) * p * q) == 0);
        for (i = 0; i < p; ++i) for (j = 0; j < q; ++j) { double s = 0; for (k = 0; k < d; ++k) s += A[i * d + k] * B[j * d + k]; R[i * q + j] = s; }
        CHECK(rel_err(C1, R, p * q) < 1e-14);
        CHECK_ST(mj_lr_gemm_nt_cpu(A, A, C1, p, p, d, 1, 3), MJ_OK);
        for (i = 0; i < p; ++i) for (j = 0; j < p; ++j) { double s = 0; for (k = 0; k < d; ++k) s += A[i * d + k] * A[j * d + k]; R[i * p + j] = s; }
        CHECK(rel_err(C1, R, p * p) < 1e-14);
        {   /* C = A B against the definition */
            double *X = (double *)malloc(sizeof(double) * q * p);
            for (i = 0; i < q * p; ++i) X[i] = drand();
            CHECK_ST(mj_lr_gemm_nn_cpu(X, A, N, q, p, d, 2), MJ_OK);
            for (i = 0; i < q; ++i) for (k = 0; k < d; ++k) { double s = 0; for (j = 0; j < p; ++j) s += X[i * p + j] * A[j * d + k]; R[0] = s; CHECK(fabs(N[i * d + k] - s) <= 1e-12 * (1 + fabs(s))); }
            free(X);
        }
        if (mj_lr_cuda_available()) {
            CHECK_ST(mj_lr_gemm_nt_cuda(A, B, C2, p, q, d), MJ_OK);
            CHECK_ST(mj_lr_gemm_nt_cpu(A, B, C1, p, q, d, 0, 2), MJ_OK);
            CHECK(rel_err(C2, C1, p * q) < 1e-13);
            printf("cuBLAS products agree with the CPU (relative difference %.1e)\n", rel_err(C2, C1, p * q));
        } else {
            CHECK_ST(mj_lr_gemm_nt_cuda(A, B, C2, p, q, d), MJ_E_NOTFOUND);
        }
        free(A); free(B); free(C1); free(C2); free(R); free(N);
    }

    /* the edit, with and without background keys */
    {
        const uint32_t n = 6, out = 19, in = 24, m = 40;
        float *k = frand_array((size_t)n * in, 1.0), *o = frand_array((size_t)n * out, 2.0),
              *g = frand_array((size_t)n * out, 0.01), *bg = frand_array((size_t)m * in, 1.0);
        double *dW = (double *)malloc(sizeof(double) * out * in), *uv = (double *)malloc(sizeof(double) * out * in);
        mj_lr_input li;
        mj_lr_result r, r2;
        int cfg;
        for (cfg = 0; cfg < 3; ++cfg) {
            memset(&li, 0, sizeof li);
            li.n = n; li.out = out; li.in = in; li.k = k; li.o = o; li.g = g;
            li.ridge = 0.1; li.rank = n; li.threads = 2; li.use_cuda = 0;
            if (cfg >= 1) { li.m = m; li.bg = bg; }
            if (cfg == 2) li.shared_weight = 64;
            reference(&li, dW);
            CHECK_ST(mj_lowrank(&li, &b, &r), MJ_OK);
            CHECK_EQ(r.r, n);
            product(&r, out, in, uv);
            CHECK(rel_err(uv, dW, (size_t)out * in) < 1e-6); /* float factors */
            {   /* singular values largest first, and truncation drops exactly the tail */
                uint32_t j;
                double tail = 0, all = 0, err = 0;
                for (j = 1; j < n; ++j) CHECK(r.s[j] <= r.s[j - 1]);
                li.rank = 3;
                CHECK_ST(mj_lowrank(&li, &b, &r2), MJ_OK);
                product(&r2, out, in, uv);
                for (j = 3; j < n; ++j) tail += r.s[j] * r.s[j];
                for (j = 0; j < n; ++j) all += r.s[j] * r.s[j];
                for (j = 0; j < out * in; ++j) err += (uv[j] - dW[j]) * (uv[j] - dW[j]);
                CHECK(fabs(err - tail) <= 1e-5 * all);
                if (mj_lr_cuda_available()) {
                    mj_lr_result r3;
                    size_t e;
                    double worst = 0;
                    li.use_cuda = 1;
                    CHECK_ST(mj_lowrank(&li, &b, &r3), MJ_OK);
                    CHECK(cfg == 0 || r3.used_cuda);
                    for (e = 0; e < (size_t)out * 3; ++e) worst = fmax(worst, fabs(fabs(r3.U[e]) - fabs(r2.U[e])));
                    CHECK(worst < 1e-5 * r.s[0]);
                    mj_lr_free(&r3);
                }
                mj_lr_free(&r2);
            }
            mj_lr_free(&r);
        }
        /* refusals */
        memset(&li, 0, sizeof li);
        li.n = n; li.out = out; li.in = in; li.k = k; li.o = o; li.g = g; li.ridge = 0.1; li.rank = 4;
        li.rank = 0;
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        li.rank = 65;
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        li.rank = 4;
        li.out = 5; /* fewer outputs than examples */
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        li.out = out;
        li.m = m; li.bg = bg; li.ridge = 0;
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        li.ridge = 0.1; li.bg = NULL;
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        li.m = 0; li.shared_weight = 64;
        CHECK_ST(mj_lowrank(&li, &b, &r), MJ_E_INVAL);
        free(k); free(o); free(g); free(bg); free(dW); free(uv);
    }
    CHECK_EQ(b.used, 0u);
    return mj_t_done("test_lowrank");
}
