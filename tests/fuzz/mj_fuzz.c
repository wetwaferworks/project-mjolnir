/* The built-in fuzzer: deterministic mutation from valid seeds, no outside
 * library, so it runs the same on Windows and Linux and inside ctest.
 * Usage: mj_fuzz [iterations-per-target] [seed]
 *        mj_fuzz --write-seeds DIR   (seed corpora for libFuzzer: DIR/<target>.seed)
 * Build with the sanitizers for the strongest check.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_fuzz_targets.h"

#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_synth.h"

#include "mjolnir/mj_hash.h"

#include <stdio.h>

#define BUF_MAX 8192u

static uint64_t rng_state;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

static const char *TOKENS[] = {"|", "\n", "MJL1", "MJVER1", "id=", "seq=", "kind=", "nightly", "-", "0", "1",
                               "10000", "10001", "18446744073709551616", "=", "\r", ",", ".", "/", "..", "\\",
                               "0000000000000000000000000000000000000000000000000000000000000000", "\0", "Z", "T",
                               "\"", "{", "}", "[", "]", ":", "\\u", "\\ud800", "BF16", "data_offsets", "shape",
                               "__metadata__", "\nend\n", "rows=", "cols=", "rank=", "scale=", "e-", "1e999",
                               "4294967296", "tensor=", "count="};

static size_t mutate(uint8_t *b, size_t n) {
    uint32_t ops = 1 + rnd() % 6, k;
    for (k = 0; k < ops; ++k) {
        uint32_t op = rnd() % 8;
        size_t at = n ? rnd() % n : 0;
        switch (op) {
        case 0: if (n) b[at] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: if (n) b[at] = (uint8_t)rnd(); break;
        case 2: if (n) n = at; break;                               /* truncate */
        case 3: if (n > 1) { memmove(b + at, b + at + 1, n - at - 1); --n; } break; /* delete */
        case 4: if (n + 1 < BUF_MAX) { memmove(b + at + 1, b + at, n - at); b[at] = (uint8_t)rnd(); ++n; } break;
        case 5: { /* insert a token */
            const char *t = TOKENS[rnd() % (sizeof TOKENS / sizeof TOKENS[0])];
            size_t tl = strlen(t) ? strlen(t) : 1;
            if (n + tl < BUF_MAX) { memmove(b + at + tl, b + at, n - at); memcpy(b + at, t, tl); n += tl; }
            break;
        }
        case 6: if (n) b[at] = (uint8_t)("0123456789abcdef|\n"[rnd() % 18]); break;
        default: if (n > 8) { size_t src = rnd() % n, len = 1 + rnd() % 8; if (src + len <= n && at + len <= n) memmove(b + at, b + src, len); } break;
        }
    }
    return n;
}

/* Re-seal mutated input so the fuzzer gets past the checksums and reaches the
 * field parsers behind them. */
static void reseal(size_t t, uint8_t *b, size_t n) {
    size_t i, start = 0;
    if (t == 0) { /* each ledger line: recompute its trailing hash */
        for (i = 0; i <= n; ++i) {
            if (i == n || b[i] == '\n') {
                size_t e = i, bar = e;
                while (bar > start && b[bar - 1] != '|') --bar;
                if (bar > start && e - bar == 64) {
                    uint8_t h[32];
                    char hex[65];
                    mj_sha256(b + start, bar - 1 - start, h);
                    mj_hex_encode(h, 32, hex);
                    memcpy(b + bar, hex, 64);
                }
                start = i + 1;
            }
        }
    } else if (t == 1) { /* the manifest's mcrc line */
        for (i = 0; i + 5 + 8 <= n; ++i) {
            if (memcmp(b + i, "mcrc=", 5) == 0) {
                char hex[9];
                snprintf(hex, sizeof hex, "%08lx", (unsigned long)mj_crc32(b, i));
                memcpy(b + i + 5, hex, 8);
                break;
            }
        }
    } else if (t == 8 && n >= 32) { /* a hemisphere sample set's trailing hash */
        mj_sha256(b, n - 32, b + n - 32);
    } else if (t == 5 && n >= 64) { /* a delta's trailing hash */
        mj_sha256(b, n - 32, b + n - 32);
    } else if (t == 2 && n == MJ_TOY_BLOB_LEN) {
        uint32_t c = mj_crc32(b, n - 4);
        b[n - 4] = (uint8_t)c; b[n - 3] = (uint8_t)(c >> 8); b[n - 2] = (uint8_t)(c >> 16); b[n - 1] = (uint8_t)(c >> 24);
    }
}

typedef struct target {
    const char *name;
    mj_fuzz_fn fn;
    uint8_t seed[BUF_MAX];
    size_t seed_len;
} target;

#define N_TARGETS 9
static target T[N_TARGETS];

static void seeds(void) {
    mj_dream_record r;
    mj_version_info v;
    mj_toy_model m;
    mj_toy_maze mz[MJ_SYNTH_MAZES];
    mj_toy_run run;
    size_t i;
    char body[MJ_LEDGER_RECORD_MAX + 2];
    T[0].name = "ledger"; T[0].fn = mj_fuzz_ledger;
    T[1].name = "manifest"; T[1].fn = mj_fuzz_manifest;
    T[2].name = "toy"; T[2].fn = mj_fuzz_toy;
    T[3].name = "score"; T[3].fn = mj_fuzz_score;
    T[4].name = "safet"; T[4].fn = mj_fuzz_safet;
    T[5].name = "delta"; T[5].fn = mj_fuzz_delta;
    T[6].name = "json"; T[6].fn = mj_fuzz_json;
    T[7].name = "http"; T[7].fn = mj_fuzz_http;
    T[8].name = "hemi"; T[8].fn = mj_fuzz_hemi;
    { /* a real sample set: one pass of two toy hemispheres over a few corridors */
        mj_budget hb;
        mj_hemi_toy_world w;
        mj_toy_model teacher;
        mj_hemi_toy lt, rt;
        mj_hemi_model lm, rm;
        mj_hemi_header h;
        mj_hemi_set hs;
        uint8_t *hp;
        size_t hn;
        mj_budget_init(&hb, 16u << 20);
        mj_hemi_toy_teacher(1, &teacher);
        (void)mj_hemi_toy_parents("complement", 1, &teacher, &lt.m, &rt.m);
        (void)mj_hemi_toy_bind(&lt, "left", &lm);
        (void)mj_hemi_toy_bind(&rt, "right", &rm);
        memset(&h, 0, sizeof h);
        h.round = 1; h.pass = 1; h.purpose = MJ_HEMI_LESSON; h.actor_side = MJ_HEMI_LEFT; h.world = MJ_HEMI_WORLD_TOY_MAZE;
        h.n_features = MJ_TOY_F; h.n_actions = MJ_TOY_A; h.seed = 5;
        mj_hemi_set_init(&hs, &hb, &h);
        if (mj_hemi_toy_world_make(5, 12, 8, &teacher, &hb, &w) == MJ_OK) {
            if (mj_hemi_toy_pass(&w, &lm, &rm, &hs) == MJ_OK && mj_hemi_encode(&hs, &hb, &hp, &hn) == MJ_OK) {
                if (hn <= BUF_MAX) { memcpy(T[8].seed, hp, hn); T[8].seed_len = hn; }
                mj_bfree(&hb, hp, hn);
            }
            mj_hemi_toy_world_free(&w);
        }
        mj_hemi_set_free(&hs);
    }
    {
        static const char req[] =
            "POST /api/grade HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nX-Mjolnir: 1\r\nContent-Type: application/json\r\n"
            "Content-Length: 44\r\n\r\n{\"replay\":\"r\",\"index\":2,\"label\":\"good\"}";
        memcpy(T[7].seed, req, sizeof req - 1);
        T[7].seed_len = sizeof req - 1;
    }
    {
        static const char line[] =
            "{\"format\":\"pchie.research.v1\",\"event\":\"advise\",\"decision\":1,\"x\":-0.0,\"key\":true,"
            "\"open\":5,\"proposals\":[[7,600000,1],[8,4.5e-3,1]],\"t\":\"caf\\u00e9 \\ud83d\\ude00\",\"n\":null,"
            "\"big\":123456789012345678901,\"o\":{\"b\":1,\"a\":[],\"b\":2},\"prev\":\"00\"}";
        memcpy(T[6].seed, line, sizeof line - 1);
        T[6].seed_len = sizeof line - 1;
    }

    /* a real two-line ledger image */
    {
        char path[MJ_PATH_MAX];
        mj_budget b;
        uint8_t *buf;
        size_t n;
        const char *base = getenv("MJ_TEST_TMP");
        snprintf(path, sizeof path, "%s/fuzz_seed.mjl", base ? base : ".");
        (void)mj_fs_remove(path);
        mj_budget_init(&b, 1u << 20);
        for (i = 0; i < 2; ++i) {
            mj_ledger_record_init(&r);
            memcpy(r.dream_id, i ? "seed-b" : "seed-a", 7);
            memcpy(r.grader, "auto-maze", 10);
            memcpy(r.outcome, "rejected", 9);
            memcpy(r.notes, "shift gate: 2500bp", 19);
            (void)mj_ledger_append(path, &b, &r);
        }
        if (mj_fs_read_all(path, BUF_MAX, &b, &buf, &n) == MJ_OK) {
            memcpy(T[0].seed, buf, n);
            T[0].seed_len = n;
            mj_bfree(&b, buf, n);
        }
        (void)body;
    }
    memset(&v, 0, sizeof v);
    memcpy(v.id, "night-07", 9);
    v.seq = 42;
    v.kind = MJ_KIND_NIGHTLY;
    v.size = 140;
    v.crc32 = 0xdeadbeefu;
    memcpy(v.parent, "night-06", 9);
    memcpy(v.label, "a label", 8);
    T[1].seed_len = mj_manifest_format(&v, (char *)T[1].seed, BUF_MAX);
    mj_synth_genesis(&m);
    (void)mj_toy_save(&m, T[2].seed);
    T[2].seed_len = MJ_TOY_BLOB_LEN;
    mj_synth_mazes_b(mz);
    (void)mj_toy_play(&m, &mz[0], "seed-run", &run, NULL);
    for (i = 0; i < run.n; ++i) {
        T[3].seed[2 * i] = run.seg[i].type;
        T[3].seed[2 * i + 1] = run.seg[i].result;
    }
    T[3].seed_len = 2 * run.n;
    {
        static const char hdr[] =
            "{\"__metadata__\":{\"format\":\"pt\"},\"a.w\":{\"dtype\":\"BF16\",\"shape\":[2,3],"
            "\"data_offsets\":[0,12]},\"b\":{\"dtype\":\"F32\",\"shape\":[],\"data_offsets\":[12,16]},"
            "\"c\":{\"dtype\":\"U8\",\"shape\":[0],\"data_offsets\":[16,16]}}  ";
        memcpy(T[4].seed, hdr, sizeof hdr - 1);
        T[4].seed_len = sizeof hdr - 1;
    }
    {
        static const float U[6] = {1, 2, 3, 4, 5, 6}, V[6] = {0.5f, -1, 2, 0.25f, 3, -4};
        mj_delta_entry e[2];
        mj_budget db;
        uint8_t *p;
        size_t pn;
        memset(e, 0, sizeof e);
        memcpy(e[0].name, "layers.0.w", 11);
        e[0].rows = 3; e[0].cols = 3; e[0].rank = 2; e[0].scale = 0.05; e[0].U = U; e[0].V = V;
        memcpy(e[1].name, "b_2", 4);
        e[1].rows = 1; e[1].cols = 2; e[1].rank = 1; e[1].scale = -1e-05; e[1].U = U; e[1].V = V;
        mj_budget_init(&db, 1u << 20);
        if (mj_delta_pack("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", e, 2, &db, &p, &pn) ==
            MJ_OK) {
            memcpy(T[5].seed, p, pn);
            T[5].seed_len = pn;
            mj_bfree(&db, p, pn);
        }
    }
}

static int write_seeds(const char *dir) {
    size_t t;
    seeds();
    for (t = 0; t < N_TARGETS; ++t) {
        char p[MJ_PATH_MAX];
        snprintf(p, sizeof p, "%s/%s.seed", dir, T[t].name);
        (void)mj_fs_remove(p);
        if (mj_fs_write_atomic(p, T[t].seed, T[t].seed_len, 0) != MJ_OK) return 1;
        printf("wrote %s (%lu bytes)\n", p, (unsigned long)T[t].seed_len);
    }
    return 0;
}

int main(int argc, char **argv) {
    unsigned long iters;
    uint64_t seed, t0;
    static uint8_t buf[BUF_MAX];
    size_t t;
    if (argc == 3 && strcmp(argv[1], "--write-seeds") == 0) return write_seeds(argv[2]);
    iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 100000ul;
    seed = argc > 2 ? strtoull(argv[2], NULL, 10) : 0x6d6a6f6c6e6972ull;
    t0 = mj_clock_ms();
    if (iters == 0 || iters > 100000000ul) iters = 100000ul;
    seeds();
    for (t = 0; t < N_TARGETS; ++t) {
        unsigned long i;
        rng_state = seed ^ (0x9E3779B97F4A7C15ull * (t + 1));
        if (T[t].seed_len == 0) {
            fprintf(stderr, "no seed for %s\n", T[t].name);
            return 1;
        }
        T[t].fn(T[t].seed, T[t].seed_len); /* the seed itself */
        for (i = 0; i < iters; ++i) {
            size_t n;
            memcpy(buf, T[t].seed, T[t].seed_len);
            n = mutate(buf, T[t].seed_len);
            if ((rnd() & 3u) == 0) n = mutate(buf, n); /* deeper mutations now and then */
            if (rnd() & 1u) reseal(t, buf, n);
            T[t].fn(buf, n);
        }
        printf("fuzz %-8s %lu inputs, no crash, no invariant broken\n", T[t].name, iters);
    }
    printf("fuzz total %lu inputs in %llu ms\n", iters * (unsigned long)N_TARGETS, (unsigned long long)(mj_clock_ms() - t0));
    return 0;
}
