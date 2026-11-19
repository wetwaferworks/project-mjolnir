/* MJDELTA1 pack and unpack, Python's float repr, and every refusal.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"

#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_hash.h"

#include <math.h>

static const struct { uint64_t bits; const char *repr; } REPR[] = {
    {0x3fe0000000000000ull, "0.5"},
    {0x3fb999999999999aull, "0.1"},
    {0x3ee4f8b588e368f1ull, "1e-05"},
    {0x4341c37937e08000ull, "1e+16"},
    {0x430c6bf526340000ull, "1000000000000000.0"},
    {0x3f1a36e2eb1c432dull, "0.0001"},
    {0x8000000000000000ull, "-0.0"},
    {0x0000000000000000ull, "0.0"},
    {0x3ff0000000000000ull, "1.0"},
    {0x437b69b4ba630f35ull, "1.2345678901234568e+17"},
    {0x0011fa182c40c60dull, "2.5e-308"},
    {0x0000000000000001ull, "5e-324"},
    {0x7fefffffffffffffull, "1.7976931348623157e+308"},
    {0x3fd3333333333333ull, "0.3"},
    {0x3fd5555555555555ull, "0.3333333333333333"},
    {0x4059000000000000ull, "100.0"},
    {0x40934a0000000000ull, "1234.5"},
    {0x3f947ae147ae147bull, "0.02"},
    {0x4480f0cf064dd592ull, "1e+22"},
    {0xbfd0000000000000ull, "-0.25"},
    {0x3eef75104d551d69ull, "1.5e-05"},
    {0x3f202e4b6ce5dc68ull, "0.00012345"},
    {0x4341c37937e07fffull, "9999999999999998.0"},
    {0x0000000000000003ull, "1.5e-323"},
    {0x3fd3333333333334ull, "0.30000000000000004"},
    {0xbe7ad7f29abcaf48ull, "-1e-07"},
    {0x4271f7182a000000ull, "1234560000000.0"},
    {0x3f19f3c70c996b77ull, "9.900000000000001e-05"},
};

static double from_bits(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static uint64_t rng = 0x243F6A8885A308D3ull;
static uint64_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static const char *BASE = "3b1f0c5e8d2a4b6c7e9f0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f";

/* re-seal the trailing hash after an edit */
static void reseal(uint8_t *d, size_t n) { mj_sha256(d, n - 32, d + n - 32); }

int main(void) {
    mj_budget b;
    size_t i;
    char out[32];
    mj_budget_init(&b, 64u << 20);

    /* ---- repr ---- */
    for (i = 0; i < sizeof REPR / sizeof REPR[0]; ++i) {
        size_t n = mj_float_repr(from_bits(REPR[i].bits), out);
        CHECK(n == strlen(REPR[i].repr) && strcmp(out, REPR[i].repr) == 0);
        if (strcmp(out, REPR[i].repr) != 0) fprintf(stderr, "  repr got %s want %s\n", out, REPR[i].repr);
    }
    CHECK_EQ(mj_float_repr(INFINITY, out), 0u);
    CHECK_EQ(mj_float_repr(NAN, out), 0u);
    {
        unsigned bad = 0;
        for (i = 0; i < 200000; ++i) {
            double x = from_bits(next());
            if (!isfinite(x)) continue;
            if (mj_float_repr(x, out) == 0 || strtod(out, NULL) != x || strlen(out) > 24) ++bad;
        }
        CHECK_EQ(bad, 0u);
    }

    /* ---- pack, exact text ---- */
    {
        float U[6] = {1, 2, 3, 4, 5, 6}, V[6] = {0.5f, -1, 2, 0.25f, 3, -4};
        mj_delta_entry e[2];
        uint8_t *p;
        size_t n;
        static mj_delta d;
        const char *want_head;
        memset(e, 0, sizeof e);
        strcpy(e[0].name, "model.layers.0.mlp.down_proj.weight");
        e[0].rows = 3; e[0].cols = 3; e[0].rank = 2; e[0].scale = 0.05; e[0].U = U; e[0].V = V;
        strcpy(e[1].name, "b_2");
        e[1].rows = 1; e[1].cols = 2; e[1].rank = 1; e[1].scale = -1e-05; e[1].U = U; e[1].V = V;
        CHECK_ST(mj_delta_pack(BASE, e, 2, &b, &p, &n), MJ_OK);
        want_head = "MJDELTA1\nbase=3b1f0c5e8d2a4b6c7e9f0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f\ncount=2\n"
                    "tensor=model.layers.0.mlp.down_proj.weight rows=3 cols=3 rank=2 scale=0.05\n"
                    "tensor=b_2 rows=1 cols=2 rank=1 scale=-1e-05\nend\n";
        CHECK(n == strlen(want_head) + 4 * (6 + 6 + 1 + 2) + 32);
        CHECK(memcmp(p, want_head, strlen(want_head)) == 0);
        CHECK_ST(mj_delta_unpack(p, n, &b, &d), MJ_OK);
        CHECK_EQ(d.count, 2u);
        CHECK(strcmp(d.base, BASE) == 0);
        CHECK(d.e[0].rows == 3 && d.e[0].cols == 3 && d.e[0].rank == 2 && d.e[0].scale == 0.05);
        CHECK(d.e[0].U[5] == 6.0f && d.e[0].V[3] == 0.25f && d.e[1].V[1] == -1.0f && d.e[1].scale == -1e-05);
        {   /* pack again: same bytes */
            uint8_t *q;
            size_t m;
            CHECK_ST(mj_delta_pack(d.base, d.e, d.count, &b, &q, &m), MJ_OK);
            CHECK(m == n && memcmp(p, q, n) == 0);
            mj_bfree(&b, q, m);
        }
        mj_delta_free(&d);

        /* refusals on unpack */
        {
            uint8_t *c = (uint8_t *)malloc(n + 64);
            static mj_delta x;
            size_t hl = strlen(want_head);
            memcpy(c, p, n);
            c[n - 1] ^= 1; /* checksum */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            memcpy(c, p, n);
            c[hl + 3] ^= 0x40; reseal(c, n); /* body change, resealed: still valid */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_OK);
            mj_delta_free(&x);
            memcpy(c, p, n);
            memcpy(c + n, p + n - 32, 32); memset(c + n - 32, 0, 32); reseal(c, n + 32); /* extra bytes */
            CHECK_ST(mj_delta_unpack(c, n + 32, &b, &x), MJ_E_CORRUPT);
            memcpy(c, p, n);
            c[0] = 'X'; reseal(c, n); /* magic */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            memcpy(c, p, n);
            c[14] = 'A'; reseal(c, n); /* upper-case hex */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            memcpy(c, p, n);
            c[strlen("MJDELTA1\nbase=") + 64 + strlen("\ncount=")] = '3'; reseal(c, n); /* count vs lines */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            memcpy(c, p, n);
            {   /* rows=3 -> rows=0 */
                char *r = strstr((char *)c, "rows=3");
                r[5] = '0'; reseal(c, n);
                CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            }
            memcpy(c, p, n);
            {   /* a scale Python's float() refuses */
                char *s = strstr((char *)c, "scale=0.05");
                memcpy(s + 6, "0.e.", 4); reseal(c, n);
                CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            }
            memcpy(c, p, n);
            {   /* a name character outside [A-Za-z0-9._] */
                char *s = strstr((char *)c, "b_2");
                s[1] = '-'; reseal(c, n);
                CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            }
            memcpy(c, p, n);
            {   /* rows one larger than the body holds */
                char *s = strstr((char *)c, "rows=1");
                s[5] = '2'; reseal(c, n);
                CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            }
            memcpy(c, p, n);
            c[10] = 0xC3; reseal(c, n); /* non-ASCII header */
            CHECK_ST(mj_delta_unpack(c, n, &b, &x), MJ_E_CORRUPT);
            CHECK_ST(mj_delta_unpack(c, 63, &b, &x), MJ_E_CORRUPT);
            free(c);
        }
        mj_bfree(&b, p, n);

        /* refusals on pack */
        CHECK_ST(mj_delta_pack("ABC", e, 1, &b, &p, &n), MJ_E_INVAL);
        CHECK_ST(mj_delta_pack(BASE, e, 0, &b, &p, &n), MJ_E_INVAL);
        e[0].rank = 65;
        CHECK_ST(mj_delta_pack(BASE, e, 1, &b, &p, &n), MJ_E_INVAL);
        e[0].rank = 2;
        e[0].scale = INFINITY;
        CHECK_ST(mj_delta_pack(BASE, e, 1, &b, &p, &n), MJ_E_INVAL);
        e[0].scale = 0.5;
        strcpy(e[0].name, "bad name");
        CHECK_ST(mj_delta_pack(BASE, e, 1, &b, &p, &n), MJ_E_INVAL);
    }

    /* a scale in each accepted spelling */
    {
        static const char *ok[] = {"1", "+1.5", "-.5", "5.", "1e5", "1E-5", "+0.0", "1e999999"};
        static const double want[] = {1, 1.5, -0.5, 5, 1e5, 1e-5, 0.0, 0};
        float U[1] = {1}, V[1] = {1};
        mj_delta_entry e;
        uint8_t *p;
        size_t n, k;
        memset(&e, 0, sizeof e);
        strcpy(e.name, "t");
        e.rows = e.cols = e.rank = 1;
        e.scale = 2.0;
        e.U = U;
        e.V = V;
        CHECK_ST(mj_delta_pack(BASE, &e, 1, &b, &p, &n), MJ_OK);
        for (k = 0; k < sizeof ok / sizeof ok[0]; ++k) {
            /* rebuild the file with this spelling of the scale */
            static mj_delta x;
            size_t hl = (size_t)(strstr((char *)p, "scale=2.0") - (char *)p) + 6, sl = strlen(ok[k]);
            size_t m = hl + sl + (n - (hl + 3));
            uint8_t *c = (uint8_t *)malloc(m);
            memcpy(c, p, hl);
            memcpy(c + hl, ok[k], sl);
            memcpy(c + hl + sl, p + hl + 3, n - (hl + 3));
            reseal(c, m);
            if (k == 7) CHECK_ST(mj_delta_unpack(c, m, &b, &x), MJ_E_CORRUPT); /* overflows: refused */
            else {
                CHECK_ST(mj_delta_unpack(c, m, &b, &x), MJ_OK);
                CHECK(x.e[0].scale == want[k]);
                mj_delta_free(&x);
            }
            free(c);
        }
        mj_bfree(&b, p, n);
    }
    CHECK_EQ(b.used, 0u);
    return mj_t_done("test_delta");
}
