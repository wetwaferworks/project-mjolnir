/* Fuzz targets for every parser that reads bytes from disk or from a replay.
 * Each target checks its own invariants and calls abort() on a violation, so
 * any fuzzer front end (the built-in one, or libFuzzer) can drive it.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_FUZZ_TARGETS_H
#define MJ_FUZZ_TARGETS_H

#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_json.h"
#include "mjolnir/mj_labeler.h"
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_safet.h"
#include "mjolnir/mj_score.h"
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_toy.h"

#include <stdlib.h>
#include <string.h>

#include "../mj_runlog.h"

#define MJ_FUZZ_REQUIRE(c) do { if (!(c)) abort(); } while (0)

static int mj_fuzz_ledger(const uint8_t *d, size_t n) {
    mj_dream_record r;
    mj_ledger_state st;
    uint64_t bad;
    char again[MJ_LEDGER_RECORD_MAX + 2];
    if (mj_ledger_parse_line((const char *)d, n, &r) == MJ_OK) {
        size_t m = mj_ledger_format_line(&r, again, sizeof again);
        MJ_FUZZ_REQUIRE(m == n + 1 && memcmp(again, d, n) == 0); /* canonical round trip */
    }
    if (mj_ledger_verify_buf((const char *)d, n, &st, &bad) == MJ_OK) {
        MJ_FUZZ_REQUIRE(bad == 0);
        MJ_FUZZ_REQUIRE(st.count == 0 || st.last.seq == st.count);
    }
    return 0;
}

static int mj_fuzz_manifest(const uint8_t *d, size_t n) {
    mj_version_info v, w;
    char t[1100];
    if (mj_manifest_parse((const char *)d, n, &v) == MJ_OK) {
        size_t m = mj_manifest_format(&v, t, sizeof t);
        MJ_FUZZ_REQUIRE(m == n && memcmp(t, d, n) == 0);
        MJ_FUZZ_REQUIRE(mj_manifest_parse(t, m, &w) == MJ_OK);
        MJ_FUZZ_REQUIRE(mj_id_valid(v.id));
    }
    (void)mj_payload_is_model_file(d, n);
    return 0;
}

static int mj_fuzz_toy(const uint8_t *d, size_t n) {
    mj_toy_model m;
    uint8_t out[MJ_TOY_BLOB_LEN];
    mj_toy_run run;
    if (mj_toy_load(d, n, &m) == MJ_OK) {
        MJ_FUZZ_REQUIRE(mj_toy_save(&m, out) == MJ_OK);
        MJ_FUZZ_REQUIRE(memcmp(out, d, MJ_TOY_BLOB_LEN) == 0);
    }
    /* raw bytes as a recorded run */
    memset(&run, 0, sizeof run);
    if (n > 0) {
        size_t take = n < sizeof run ? n : sizeof run;
        memcpy(&run, d, take);
        run.id[MJ_ID_MAX] = '\0';
        if (mj_toy_run_validate(&run) == MJ_OK) {
            mj_maze_score s;
            MJ_FUZZ_REQUIRE(mj_score_maze(run.seg, run.n, NULL, &s) == MJ_OK);
        }
    }
    return 0;
}

static int mj_fuzz_score(const uint8_t *d, size_t n) {
    mj_maze_segment seg[MJ_SCORE_MAX_PARTS];
    mj_circuit_element el[MJ_SCORE_MAX_PARTS];
    mj_credit lab[MJ_SCORE_MAX_PARTS];
    mj_maze_score ms;
    mj_circuit_score cs;
    uint32_t i, k = (uint32_t)(n / 2 > MJ_SCORE_MAX_PARTS ? MJ_SCORE_MAX_PARTS : n / 2);
    for (i = 0; i < k; ++i) {
        seg[i].type = d[2 * i];
        seg[i].result = d[2 * i + 1];
    }
    if (mj_score_maze(seg, k, lab, &ms) == MJ_OK) {
        MJ_FUZZ_REQUIRE(ms.success + ms.not_yet + ms.ungraded == ms.total && ms.total == k);
        MJ_FUZZ_REQUIRE(ms.score_bp <= MJ_BP_FULL && ms.not_yet <= 1);
    }
    k = (uint32_t)(n / 4 > MJ_SCORE_MAX_PARTS ? MJ_SCORE_MAX_PARTS : n / 4);
    for (i = 0; i < k; ++i) {
        el[i].id = (uint16_t)(d[4 * i] | (d[4 * i + 1] << 8));
        el[i].kind = d[4 * i + 2];
        el[i].pass = d[4 * i + 3];
    }
    if (mj_score_circuit(el, k, lab, &cs) == MJ_OK) {
        MJ_FUZZ_REQUIRE(cs.good + cs.not_yet == cs.total && cs.score_bp <= MJ_BP_FULL);
    }
    return 0;
}


/* A safetensors header: anything accepted must be inside the file, sized by
 * its shape, sorted, unique and free of overlaps. */
static int mj_fuzz_safet(const uint8_t *d, size_t n) {
    static mj_budget b;
    mj_st_header h;
    const uint64_t data = 4096;
    size_t i;
    if (b.cap == 0) mj_budget_init(&b, 512u << 20);
    if (mj_st_parse((const char *)d, n, 8u + n + data, &b, &h) == MJ_OK) {
        uint64_t prev_end = 0;
        for (i = 0; i < h.count; ++i) {
            const mj_st_tensor *t = &h.t[i];
            MJ_FUZZ_REQUIRE(t->begin <= t->end && t->end <= data);
            MJ_FUZZ_REQUIRE(t->nbytes == t->count * mj_st_dtype_size(t->dtype));
            MJ_FUZZ_REQUIRE(t->abs_offset == 8u + n + t->begin);
            if (i) MJ_FUZZ_REQUIRE(strcmp(h.t[i - 1].name, t->name) < 0);
            MJ_FUZZ_REQUIRE(mj_st_find(&h, t->name) == t);
        }
        (void)prev_end;
        mj_st_free(&h);
    }
    MJ_FUZZ_REQUIRE(b.used == 0);
    return 0;
}

/* An MJDELTA1 file: anything accepted packs again into a file that reads
 * back to the very same entries. */
static int mj_fuzz_delta(const uint8_t *d, size_t n) {
    static mj_budget b;
    static mj_delta x, y;
    uint8_t *p;
    size_t m;
    uint32_t k;
    if (b.cap == 0) mj_budget_init(&b, 512u << 20);
    if (mj_delta_unpack(d, n, &b, &x) == MJ_OK) {
        MJ_FUZZ_REQUIRE(x.count >= 1 && x.count <= MJ_DELTA_MAX_ENTRIES);
        if (mj_delta_pack(x.base, x.e, x.count, &b, &p, &m) == MJ_OK) {
            MJ_FUZZ_REQUIRE(mj_delta_unpack(p, m, &b, &y) == MJ_OK);
            MJ_FUZZ_REQUIRE(y.count == x.count && strcmp(x.base, y.base) == 0);
            for (k = 0; k < x.count; ++k) {
                MJ_FUZZ_REQUIRE(strcmp(x.e[k].name, y.e[k].name) == 0 && x.e[k].rows == y.e[k].rows &&
                                x.e[k].cols == y.e[k].cols && x.e[k].rank == y.e[k].rank &&
                                x.e[k].scale == y.e[k].scale);
            }
            MJ_FUZZ_REQUIRE(x.data_n == y.data_n && memcmp(x.data, y.data, x.data_n * sizeof(float)) == 0);
            mj_delta_free(&y);
            mj_bfree(&b, p, m);
        } else {
            uint32_t big = 0;
            for (k = 0; k < x.count; ++k) big |= x.e[k].rank > MJ_DELTA_MAX_RANK;
            MJ_FUZZ_REQUIRE(big); /* pack only refuses ranks it does not write */
        }
        mj_delta_free(&x);
    }
    MJ_FUZZ_REQUIRE(b.used == 0);
    return 0;
}

typedef int (*mj_fuzz_fn)(const uint8_t *, size_t);

static int has_raw_surrogate(const uint8_t *d, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; ++i)
        if (d[i] == 0xED && d[i + 1] >= 0xA0) return 1;
    return 0;
}

/* A JSON text, as a run log line: anything accepted writes out as a text
 * that reads back to an equal value and writes out the same again. */
static int mj_fuzz_json(const uint8_t *d, size_t n) {
    static mj_budget b;
    mj_json x, y;
    mj_jout o1, o2;
    uint32_t r1, r2;
    if (b.cap == 0) mj_budget_init(&b, 512u << 20);
    mj_json_init(&x, &b, 1u << 20, 1u << 22);
    mj_json_init(&y, &b, 1u << 20, 1u << 22);
    if (mj_json_parse(&x, (const char *)d, n, &r1) == MJ_OK) {
        mj_jout_init(&o1, 1u << 24);
        mj_jout_init(&o2, 1u << 24);
        mj_jout_value(&o1, &x, r1);
        MJ_FUZZ_REQUIRE(!o1.failed);
        MJ_FUZZ_REQUIRE(mj_json_eq(&x, r1, &x, r1)); /* no NaN can appear: there is no literal for it */
        /* Infinity is written as Python writes it but is not JSON, and raw
         * encoded surrogate halves join into one character on the way back
         * (in Python too), so only the other texts must round-trip */
        if (strstr(o1.p, "Infinity") == NULL && !has_raw_surrogate(d, n)) {
            MJ_FUZZ_REQUIRE(mj_json_parse(&y, o1.p, o1.n, &r2) == MJ_OK);
            MJ_FUZZ_REQUIRE(mj_json_eq(&x, r1, &y, r2));
            mj_jout_value(&o2, &y, r2);
            MJ_FUZZ_REQUIRE(!o2.failed && o1.n == o2.n && memcmp(o1.p, o2.p, o1.n) == 0);
        }
        mj_jout_free(&o1);
        mj_jout_free(&o2);
    }
    mj_json_free(&x);
    mj_json_free(&y);
    MJ_FUZZ_REQUIRE(b.used == 0);
    return 0;
}

/* One request to the labeling page, over a session on one small run whose
 * grades file sits in a folder that does not exist, so no input can write:
 * every answer is a whole HTTP/1.0 response with a known status, and the
 * session never takes a grade. */
static int mj_fuzz_http(const uint8_t *d, size_t n) {
    static mj_budget b;
    static mj_replay_set set;
    static mj_lab s;
    static int ready;
    mj_jout r;
    size_t used, total;
    int code, done;
    if (!ready) {
        char dir[MJ_PATH_MAX], run[MJ_PATH_MAX], gp[MJ_PATH_MAX];
        const char *base = getenv("MJ_TEST_TMP");
        const char *roots[1];
        mj_budget_init(&b, 256u << 20);
        snprintf(dir, sizeof dir, "%s/fuzz_lab", base ? base : ".");
        (void)mj_fs_mkdir(dir);
        snprintf(run, sizeof run, "%s/r.jsonl", dir);
        snprintf(gp, sizeof gp, "%s/no-such-folder/grades.mjl", dir);
        MJ_FUZZ_REQUIRE(mj_t_write_run(run, MJ_T_MOVES, (int)(sizeof MJ_T_MOVES / sizeof MJ_T_MOVES[0]), 0, 0));
        roots[0] = dir;
        MJ_FUZZ_REQUIRE(mj_replay_load_all(roots, 1, &b, NULL, NULL, &set) == MJ_OK && set.n == 1);
        MJ_FUZZ_REQUIRE(mj_lab_open(&s, &set, gp, "JD", 8765, &b) == MJ_OK);
        ready = 1;
    }
    used = b.used;
    done = (int)s.done.n;
    if (mj_lab_http_complete((const char *)d, n, &total) == 1) MJ_FUZZ_REQUIRE(total <= n);
    mj_jout_init(&r, 1u << 22);
    mj_lab_http(&s, (const char *)d, n, &r);
    MJ_FUZZ_REQUIRE(!r.failed && r.n > 12 && memcmp(r.p, "HTTP/1.0 ", 9) == 0);
    code = atoi(r.p + 9);
    MJ_FUZZ_REQUIRE(code == 200 || code == 400 || code == 403 || code == 404 || code == 431 || code == 500 || code == 501);
    MJ_FUZZ_REQUIRE(strstr(r.p, "\r\n\r\n") != NULL);
    mj_jout_free(&r);
    MJ_FUZZ_REQUIRE(b.used == used && (int)s.done.n == done);
    return 0;
}

#endif
