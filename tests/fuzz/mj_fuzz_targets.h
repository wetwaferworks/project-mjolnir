/* Fuzz targets for every parser that reads bytes from disk or from a replay.
 * Each target checks its own invariants and calls abort() on a violation, so
 * any fuzzer front end (the built-in one, or libFuzzer) can drive it.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_FUZZ_TARGETS_H
#define MJ_FUZZ_TARGETS_H

#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_score.h"
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_toy.h"

#include <stdlib.h>
#include <string.h>

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

typedef int (*mj_fuzz_fn)(const uint8_t *, size_t);

#endif
