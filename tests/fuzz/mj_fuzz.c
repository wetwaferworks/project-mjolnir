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
                               "0000000000000000000000000000000000000000000000000000000000000000", "\0", "Z", "T"};

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

static target T[4];

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
}

static int write_seeds(const char *dir) {
    size_t t;
    seeds();
    for (t = 0; t < 4; ++t) {
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
    for (t = 0; t < 4; ++t) {
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
    printf("fuzz total %lu inputs in %llu ms\n", iters * 4ul, (unsigned long long)(mj_clock_ms() - t0));
    return 0;
}
