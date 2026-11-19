/* The real-weight path on a tiny stand-in model file: copy, deltas, apply,
 * roll back, the dirty marker, the refusals, and CPU/GPU agreement.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mj_test.h"

#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_bf16.h"
#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_file.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_safet.h"
#include "mjolnir/mj_session.h"
#include "mjolnir/mj_workcopy.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define NAME_A "model.language_model.layers.0.mlp.down_proj.weight"
#define NAME_B "model.language_model.layers.1.mlp.down_proj.weight"
#define ROWS 8
#define COLS 12

static mj_budget b;
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static float frand(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (float)((double)(rng >> 11) / 9007199254740992.0 * 2.0 - 1.0);
}

static void put_u64(uint8_t *p, uint64_t v) {
    int k;
    for (k = 0; k < 8; ++k) p[k] = (uint8_t)(v >> (8 * k));
}

static void write_file(const char *dir, const char *name, const void *d, size_t n) {
    char p[MJ_PATH_MAX];
    mj_path_join(p, sizeof p, dir, name);
    CHECK_ST(mj_fs_write_atomic(p, d, n, 0), MJ_OK);
}

static void write_fake_model(const char *dir) {
    char json[512];
    uint8_t *f;
    size_t hn, n, i, tb = ROWS * COLS * 2;
    hn = (size_t)snprintf(json, sizeof json,
                          "{\"%s\":{\"dtype\":\"BF16\",\"shape\":[%d,%d],\"data_offsets\":[0,%lu]},"
                          "\"%s\":{\"dtype\":\"BF16\",\"shape\":[%d,%d],\"data_offsets\":[%lu,%lu]},"
                          "\"model.language_model.norm.weight\":{\"dtype\":\"BF16\",\"shape\":[%d],"
                          "\"data_offsets\":[%lu,%lu]}}",
                          NAME_A, ROWS, COLS, (unsigned long)tb, NAME_B, ROWS, COLS, (unsigned long)tb,
                          (unsigned long)(2 * tb), ROWS, (unsigned long)(2 * tb), (unsigned long)(2 * tb + 2 * ROWS));
    n = 8 + hn + 2 * tb + 2 * ROWS;
    f = (uint8_t *)malloc(n);
    put_u64(f, hn);
    memcpy(f + 8, json, hn);
    for (i = 0; i < 2 * ROWS * COLS; ++i) {
        uint16_t v = mj_f32_to_bf16(frand() * 2.0f);
        f[8 + hn + 2 * i] = (uint8_t)v;
        f[8 + hn + 2 * i + 1] = (uint8_t)(v >> 8);
    }
    for (i = 0; i < ROWS; ++i) {
        f[8 + hn + 2 * tb + 2 * i] = 0x80;
        f[8 + hn + 2 * tb + 2 * i + 1] = 0x3f; /* 1.0 */
    }
    (void)mj_fs_mkdir(dir);
    write_file(dir, MJ_WC_MODEL_FILE, f, n);
    write_file(dir, "config.json", "{}", 2);
    free(f);
}

static void sha_of(const char *dir, const char *name, char hex[MJ_SHA256_HEX]) {
    char p[MJ_PATH_MAX];
    uint8_t h[MJ_SHA256_LEN];
    mj_path_join(p, sizeof p, dir, name);
    if (mj_fs_sha256_file(p, h, NULL) != MJ_OK) memset(h, 0, sizeof h);
    mj_hex_encode(h, MJ_SHA256_LEN, hex);
}

static void read_tensor(const char *dir, const char *name, uint16_t *out) {
    char p[MJ_PATH_MAX];
    mj_st_header h;
    mj_file f;
    uint8_t raw[ROWS * COLS * 2];
    const mj_st_tensor *t;
    size_t i;
    mj_path_join(p, sizeof p, dir, MJ_WC_MODEL_FILE);
    CHECK_ST(mj_st_read_header(p, &b, &h), MJ_OK);
    t = mj_st_find(&h, name);
    CHECK(t != NULL && t->nbytes == sizeof raw);
    CHECK_ST(mj_file_open(&f, p, 0), MJ_OK);
    CHECK_ST(mj_file_pread(&f, t->abs_offset, raw, sizeof raw), MJ_OK);
    mj_file_close(&f);
    mj_st_free(&h);
    for (i = 0; i < ROWS * COLS; ++i) out[i] = (uint16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
}

/* the documented formula, one element at a time */
static uint16_t ref_elem(uint16_t o, uint32_t r, uint32_t c, uint32_t cols, const mj_apply_term *t, size_t nt) {
    float s = 0.0f;
    size_t i;
    for (i = 0; i < nt; ++i) {
        double acc = 0.0;
        uint32_t k;
        float d;
        for (k = 0; k < t[i].rank; ++k) acc += (double)t[i].U[r * t[i].rank + k] * (double)t[i].V[k * cols + c];
        d = (float)acc;
        d = d * t[i].scale;
        s = s + d;
    }
    return mj_f32_to_bf16(mj_bf16_to_f32(o) + s);
}

static float U1[ROWS * 2], V1[2 * COLS], U2[ROWS * 3], V2[3 * COLS];

static void put_delta(mj_store *s, const char *id, const char *parent, const char *base, const char *name, float *U,
                      float *V, uint32_t rank, double scale) {
    mj_delta_entry e;
    uint8_t *p;
    size_t n;
    memset(&e, 0, sizeof e);
    strcpy(e.name, name);
    e.rows = ROWS;
    e.cols = COLS;
    e.rank = rank;
    e.scale = scale;
    e.U = U;
    e.V = V;
    CHECK_ST(mj_delta_pack(base, &e, 1, &b, &p, &n), MJ_OK);
    CHECK_ST(mj_store_put(s, id, MJ_KIND_CANDIDATE, parent, "test edit", p, n, NULL), MJ_OK);
    mj_bfree(&b, p, n);
}

static int count_lines(const char *path) {
    uint8_t *d;
    size_t n, i;
    int c = 0;
    if (mj_fs_read_all(path, 1u << 20, &b, &d, &n) != MJ_OK) return -1;
    for (i = 0; i < n; ++i) c += d[i] == '\n';
    mj_bfree(&b, d, n ? n : 1);
    return c;
}

static void unlock_dir(const char *dir) {
    char p[MJ_PATH_MAX];
    mj_path_join(p, sizeof p, dir, MJ_WC_MODEL_FILE);
    (void)mj_fs_set_writable(p);
    mj_path_join(p, sizeof p, dir, "config.json");
    (void)mj_fs_set_writable(p);
}

static void wipe(const char *dir) {
    static const char *names[] = {MJ_WC_MODEL_FILE, "config.json", MJ_WC_STATE_FILE, MJ_WC_DIRTY_FILE, "link.bin"};
    size_t i;
    unlock_dir(dir);
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        char p[MJ_PATH_MAX];
        mj_path_join(p, sizeof p, dir, names[i]);
        (void)mj_fs_remove(p);
    }
}

static void apply_parity(void) {
    /* CPU threads agree with each other, with the formula, and with the GPU when there is one */
    const uint32_t R = 257, C = 1031;
    size_t n = (size_t)R * C, i, nt = 3, bad = 0;
    uint16_t *o = (uint16_t *)malloc(n * 2), *a = (uint16_t *)malloc(n * 2), *c4 = (uint16_t *)malloc(n * 2);
    mj_apply_term t[3];
    float *buf[6];
    for (i = 0; i < n; ++i) o[i] = mj_f32_to_bf16(frand());
    for (i = 0; i < nt; ++i) {
        uint32_t k = (uint32_t)(4 + 6 * i);
        size_t j;
        buf[2 * i] = (float *)malloc((size_t)R * k * 4);
        buf[2 * i + 1] = (float *)malloc((size_t)k * C * 4);
        for (j = 0; j < (size_t)R * k; ++j) buf[2 * i][j] = frand();
        for (j = 0; j < (size_t)k * C; ++j) buf[2 * i + 1][j] = frand();
        t[i].U = buf[2 * i];
        t[i].V = buf[2 * i + 1];
        t[i].rank = k;
        t[i].scale = (float)(0.01 * (double)(i + 1));
    }
    CHECK_ST(mj_apply_cpu(o, a, R, C, t, nt, 1), MJ_OK);
    CHECK_ST(mj_apply_cpu(o, c4, R, C, t, nt, 4), MJ_OK);
    CHECK(memcmp(a, c4, n * 2) == 0);
    for (i = 0; i < n; ++i)
        if (a[i] != ref_elem(o[i], (uint32_t)(i / C), (uint32_t)(i % C), C, t, nt)) ++bad;
    CHECK_EQ(bad, 0u);
    if (mj_apply_cuda_available()) {
        uint64_t peak = 0;
        size_t diff = 0;
        CHECK_ST(mj_apply_cuda(o, c4, R, C, t, nt, &peak), MJ_OK);
        for (i = 0; i < n; ++i) diff += a[i] != c4[i];
        CHECK_EQ(diff, 0u);
        CHECK(peak > 0);
        printf("cuda apply: %lu elements, %lu differ from the CPU\n", (unsigned long)n, (unsigned long)diff);
    } else {
        CHECK_ST(mj_apply_cuda(o, c4, R, C, t, nt, NULL), MJ_E_NOTFOUND);
        printf("cuda apply: no CUDA device in this build, CPU path only\n");
    }
    for (i = 0; i < 6; ++i) free(buf[i]);
    free(o);
    free(a);
    free(c4);
}

int main(void) {
    char root[MJ_PATH_MAX], orig[MJ_PATH_MAX], work[MJ_PATH_MAX], data[MJ_PATH_MAX], p[MJ_PATH_MAX];
    char o_sha[MJ_SHA256_HEX], w_sha[MJ_SHA256_HEX], base[MJ_SHA256_HEX], msg[200];
    mj_real_paths rp;
    mj_store s;
    mj_store_limits lim;
    mj_wc_options opt;
    mj_wc_result res;
    mj_wc_state state;
    uint16_t a0[ROWS * COLS], a1[ROWS * COLS], b0[ROWS * COLS], b1[ROWS * COLS];
    size_t i;
    mj_budget_init(&b, 512u << 20);
    opt.use_cuda = 1;
    opt.threads = 2;

    mj_t_dir(root, sizeof root, "workcopy");
    (void)mj_fs_mkdir(root);
    mj_path_join(orig, sizeof orig, root, "original");
    mj_path_join(work, sizeof work, root, "working");
    mj_path_join(data, sizeof data, root, "data");
    wipe(orig);
    wipe(work);
    mj_real_paths_for(data, &rp);
    mj_t_clean(rp.store);
    (void)mj_fs_remove(rp.ledger);
    write_fake_model(orig);
    sha_of(orig, MJ_WC_MODEL_FILE, o_sha);

    /* refusals before anything exists */
    CHECK_ST(mj_real_register(orig, orig, data, &b, msg, sizeof msg), MJ_E_FORBIDDEN);
    mj_path_join(p, sizeof p, orig, "inner");
    CHECK_ST(mj_wc_guard(orig, p), MJ_E_FORBIDDEN);
    CHECK_ST(mj_wc_guard(root, orig), MJ_E_FORBIDDEN);

    /* register */
    CHECK_ST(mj_real_register(orig, work, data, &b, msg, sizeof msg), MJ_OK);
    sha_of(work, MJ_WC_MODEL_FILE, w_sha);
    CHECK(strcmp(o_sha, w_sha) == 0);
    CHECK_EQ(count_lines(rp.ledger), 1);
    CHECK_ST(mj_real_base_sha(data, &b, base), MJ_OK);
    CHECK(strcmp(base, o_sha) == 0);
    CHECK_ST(mj_wc_read_state(work, &b, &state), MJ_OK);
    CHECK(strcmp(state.applied, "base") == 0 && state.touched.n == 0 && !state.dirty);
    mj_wc_state_free(&state);
    {   /* the base record is MJBASE1 + the sums, as Python writes it */
        mj_sums sums;
        char *text;
        size_t tn, n;
        uint8_t *blob;
        mj_real_limits(&lim);
        CHECK_ST(mj_store_open(&s, rp.store, &lim, &b), MJ_OK);
        CHECK_ST(mj_wc_checksums(orig, &b, &sums), MJ_OK);
        CHECK_ST(mj_wc_format_sums(&sums, &b, &text, &tn), MJ_OK);
        CHECK_ST(mj_store_get(&s, "base", &blob, &n), MJ_OK);
        CHECK(n == tn + 8 && memcmp(blob, "MJBASE1\n", 8) == 0 && memcmp(blob + 8, text, tn) == 0);
        CHECK(strstr(text, "  2  config.json\n") != NULL);
        mj_bfree(&b, blob, n);
        mj_bfree(&b, text, tn + 1);
        mj_sums_free(&sums);
    }
    /* registering again is refused, as in Python: the working folder now
     * holds a state file, so it no longer matches the original */
    CHECK_ST(mj_real_register(orig, work, data, &b, msg, sizeof msg), MJ_E_FORBIDDEN);
    CHECK_EQ(count_lines(rp.ledger), 1);

    /* an edit on A */
    for (i = 0; i < ROWS * 2; ++i) U1[i] = frand();
    for (i = 0; i < 2 * COLS; ++i) V1[i] = frand();
    for (i = 0; i < ROWS * 3; ++i) U2[i] = frand();
    for (i = 0; i < 3 * COLS; ++i) V2[i] = frand();
    put_delta(&s, "e1", "base", base, NAME_A, U1, V1, 2, 0.5);
    read_tensor(orig, NAME_A, a0);
    read_tensor(orig, NAME_B, b0);
    CHECK_ST(mj_real_apply(orig, work, data, "e1", &opt, &b, &res), MJ_OK);
    CHECK(res.touched == 1 && res.edited == 1);
    read_tensor(work, NAME_A, a1);
    read_tensor(work, NAME_B, b1);
    CHECK(memcmp(b0, b1, sizeof b0) == 0); /* untouched stays bit-identical */
    {
        mj_apply_term t = {U1, V1, 2, 0.5f};
        size_t bad = 0, changed = 0;
        for (i = 0; i < ROWS * COLS; ++i) {
            bad += a1[i] != ref_elem(a0[i], (uint32_t)(i / COLS), (uint32_t)(i % COLS), COLS, &t, 1);
            changed += a1[i] != a0[i];
        }
        CHECK_EQ(bad, 0u);
        CHECK(changed > ROWS * COLS / 2);
    }
    CHECK_ST(mj_wc_read_state(work, &b, &state), MJ_OK);
    CHECK(strcmp(state.applied, "e1") == 0 && state.touched.n == 1 && mj_names_has(&state.touched, NAME_A));
    mj_wc_state_free(&state);
    CHECK_EQ(count_lines(rp.ledger), 2);

    /* a second edit on top */
    put_delta(&s, "e2", "e1", base, NAME_B, U2, V2, 3, 0.25);
    CHECK_ST(mj_real_apply(orig, work, data, "e2", &opt, &b, &res), MJ_OK);
    CHECK(res.touched == 2 && res.edited == 2);
    {
        uint16_t a2[ROWS * COLS];
        read_tensor(work, NAME_A, a2);
        CHECK(memcmp(a1, a2, sizeof a1) == 0); /* A rebuilt from the original: same bytes */
        read_tensor(work, NAME_B, b1);
        CHECK(memcmp(b0, b1, sizeof b0) != 0);
    }
    /* roll back to the base: byte-identical to the original */
    CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_OK);
    CHECK(res.touched == 2 && res.edited == 0);
    sha_of(work, MJ_WC_MODEL_FILE, w_sha);
    CHECK(strcmp(o_sha, w_sha) == 0);
    CHECK_ST(mj_wc_read_state(work, &b, &state), MJ_OK);
    CHECK(strcmp(state.applied, "base") == 0 && state.touched.n == 0);
    mj_wc_state_free(&state);
    CHECK_EQ(count_lines(rp.ledger), 4);
    {   /* the ledger checks out and the last record reads as Python writes it */
        mj_ledger_state ls;
        uint64_t bad;
        CHECK_ST(mj_ledger_verify_file(rp.ledger, &b, &ls, &bad), MJ_OK);
        CHECK(ls.count == 4 && strcmp(ls.last.outcome, "rolled-back") == 0 &&
              strcmp(ls.last.edit, "wrote 2 tensors into the working copy") == 0 &&
              strcmp(ls.last.weights_before, "e2") == 0 && strcmp(ls.last.gates, "readback=pass") == 0);
    }

    /* a cut-short write: the marker names B, B holds junk; the next apply repairs it */
    {
        mj_file f;
        mj_st_header h;
        uint8_t junk[64];
        const char *marker = "MJDIRTY1\napplied=base\ntouched=" NAME_B "\n";
        mj_path_join(p, sizeof p, work, MJ_WC_MODEL_FILE);
        CHECK_ST(mj_st_read_header(p, &b, &h), MJ_OK);
        memset(junk, 0x7f, sizeof junk);
        CHECK_ST(mj_file_open(&f, p, 1), MJ_OK);
        CHECK_ST(mj_file_pwrite(&f, mj_st_find(&h, NAME_B)->abs_offset, junk, sizeof junk), MJ_OK);
        mj_file_close(&f);
        mj_st_free(&h);
        write_file(work, MJ_WC_DIRTY_FILE, marker, strlen(marker));
        CHECK_ST(mj_wc_read_state(work, &b, &state), MJ_OK);
        CHECK(state.dirty && mj_names_has(&state.dirty_touched, NAME_B));
        mj_wc_state_free(&state);
        CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_OK);
        CHECK_EQ(res.touched, 1u);
        sha_of(work, MJ_WC_MODEL_FILE, w_sha);
        CHECK(strcmp(o_sha, w_sha) == 0);
        mj_path_join(p, sizeof p, work, MJ_WC_DIRTY_FILE);
        CHECK(!mj_fs_exists(p));
    }

    /* refusals at apply time */
    {
        char other[MJ_SHA256_HEX];
        memset(other, 'a', 64);
        other[64] = '\0';
        put_delta(&s, "wrongbase", "base", other, NAME_A, U1, V1, 2, 0.5);
        CHECK_ST(mj_real_apply(orig, work, data, "wrongbase", &opt, &b, &res), MJ_E_FORBIDDEN);
        CHECK(strstr(res.reason, "different model file") != NULL);
        put_delta(&s, "noname", "base", base, "model.no.such.weight", U1, V1, 2, 0.5);
        CHECK_ST(mj_real_apply(orig, work, data, "noname", &opt, &b, &res), MJ_E_FORBIDDEN);
        CHECK(strstr(res.reason, "does not have") != NULL);
        CHECK_ST(mj_real_apply(orig, work, data, "nosuch", &opt, &b, &res), MJ_E_NOTFOUND);
        CHECK_ST(mj_real_apply(orig, orig, data, "e1", &opt, &b, &res), MJ_E_FORBIDDEN);
        /* nothing changed by the refusals */
        sha_of(work, MJ_WC_MODEL_FILE, w_sha);
        CHECK(strcmp(o_sha, w_sha) == 0);
        /* a corrupt state file */
        mj_path_join(p, sizeof p, work, MJ_WC_STATE_FILE);
        CHECK_ST(mj_fs_write_atomic(p, "MJWORK2\napplied=base\n", 21, 1), MJ_OK);
        CHECK_ST(mj_real_apply(orig, work, data, "e1", &opt, &b, &res), MJ_E_CORRUPT);
        CHECK_ST(mj_fs_write_atomic(p, "MJWORK1\napplied=base\n", 21, 1), MJ_OK);
        CHECK_ST(mj_real_apply(orig, work, data, "e1", &opt, &b, &res), MJ_OK);
    }
#if !defined(_WIN32)
    {   /* a hard-linked working file is refused */
        char w[MJ_PATH_MAX], l[MJ_PATH_MAX];
        mj_path_join(w, sizeof w, work, MJ_WC_MODEL_FILE);
        mj_path_join(l, sizeof l, work, "link.bin");
        CHECK(link(w, l) == 0);
        CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_E_FORBIDDEN);
        CHECK(strstr(res.reason, "hard-linked") != NULL);
        (void)mj_fs_remove(l);
    }
#else
    {
        char w[MJ_PATH_MAX], l[MJ_PATH_MAX];
        wchar_t ww[MJ_PATH_MAX], wl[MJ_PATH_MAX];
        mj_path_join(w, sizeof w, work, MJ_WC_MODEL_FILE);
        mj_path_join(l, sizeof l, work, "link.bin");
        MultiByteToWideChar(CP_UTF8, 0, w, -1, ww, MJ_PATH_MAX);
        MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, MJ_PATH_MAX);
        CHECK(CreateHardLinkW(wl, ww, NULL) != 0);
        CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_E_FORBIDDEN);
        CHECK(strstr(res.reason, "hard-linked") != NULL);
        (void)mj_fs_remove(l);
    }
#endif
    CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_OK);
    sha_of(work, MJ_WC_MODEL_FILE, w_sha);
    CHECK(strcmp(o_sha, w_sha) == 0);
    sha_of(orig, MJ_WC_MODEL_FILE, w_sha);
    CHECK(strcmp(o_sha, w_sha) == 0); /* the original never changed */

    {   /* the original changes after register (a disk error, a stray write): apply and rollback refuse before
         * writing anything, so "base" can never be restored from a changed source */
        char of[MJ_PATH_MAX], dirty[MJ_PATH_MAX];
        mj_file f;
        uint8_t one, flipped;
        uint64_t at = 0;
        int lines = count_lines(rp.ledger);
        CHECK_ST(mj_real_apply(orig, work, data, "e1", &opt, &b, &res), MJ_OK);
        lines = count_lines(rp.ledger);
        sha_of(work, MJ_WC_MODEL_FILE, base); /* reuse: the working file now (e1 applied) */
        mj_path_join(of, sizeof of, orig, MJ_WC_MODEL_FILE);
        (void)mj_fs_set_writable(of);
        CHECK_ST(mj_file_open(&f, of, 1), MJ_OK);
        CHECK_ST(mj_file_size(&f, &at), MJ_OK);
        at -= 1; /* the last byte of tensor data */
        CHECK_ST(mj_file_pread(&f, at, &one, 1), MJ_OK);
        flipped = (uint8_t)(one ^ 0x01);
        CHECK_ST(mj_file_pwrite(&f, at, &flipped, 1), MJ_OK);
        mj_file_close(&f);
        CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_E_CORRUPT);
        CHECK(strstr(res.reason, "no longer matches the base record") != NULL);
        CHECK_ST(mj_real_apply(orig, work, data, "e1", &opt, &b, &res), MJ_E_CORRUPT);
        sha_of(work, MJ_WC_MODEL_FILE, w_sha);
        CHECK(strcmp(base, w_sha) == 0);        /* the working copy was not touched */
        mj_path_join(dirty, sizeof dirty, work, MJ_WC_DIRTY_FILE);
        CHECK(!mj_fs_exists(dirty));            /* not even the dirty marker */
        CHECK_EQ(count_lines(rp.ledger), lines); /* and nothing journaled */
        /* put the byte back: rollback works again and is byte-identical */
        CHECK_ST(mj_file_open(&f, of, 1), MJ_OK);
        CHECK_ST(mj_file_pwrite(&f, at, &one, 1), MJ_OK);
        mj_file_close(&f);
        (void)mj_fs_set_readonly(of);
        CHECK_ST(mj_real_apply(orig, work, data, "base", &opt, &b, &res), MJ_OK);
        sha_of(work, MJ_WC_MODEL_FILE, w_sha);
        CHECK(strcmp(o_sha, w_sha) == 0);
    }

    /* the working folder must be empty or identical */
    write_file(work, "extra.txt", "x", 1);
    wipe(work);
    write_file(work, "config.json", "{ }", 3);
    CHECK_ST(mj_real_register(orig, work, data, &b, msg, sizeof msg), MJ_E_FORBIDDEN);
    wipe(work);
    mj_path_join(p, sizeof p, work, "extra.txt");
    (void)mj_fs_remove(p);

    {   /* the atomic write's temporary has a name of its own: a file that
         * happens to be called <path>.tmp is never touched */
        char x[MJ_PATH_MAX], xt[MJ_PATH_MAX];
        uint8_t *d;
        size_t n;
        mj_path_join(x, sizeof x, root, "atomic.txt");
        snprintf(xt, sizeof xt, "%s.tmp", x);
        (void)mj_fs_remove(x);
        (void)mj_fs_remove(xt);
        CHECK_ST(mj_fs_write_atomic(xt, "keep", 4, 0), MJ_OK);
        CHECK_ST(mj_fs_write_atomic(x, "one", 3, 0), MJ_OK);
        CHECK_ST(mj_fs_write_atomic(x, "two", 3, 1), MJ_OK);
        CHECK_ST(mj_fs_read_all(xt, 64, &b, &d, &n), MJ_OK);
        CHECK(n == 4 && memcmp(d, "keep", 4) == 0);
        mj_bfree(&b, d, n);
        CHECK_ST(mj_fs_read_all(x, 64, &b, &d, &n), MJ_OK);
        CHECK(n == 3 && memcmp(d, "two", 3) == 0);
        mj_bfree(&b, d, n);
        (void)mj_fs_remove(x);
        (void)mj_fs_remove(xt);
    }

    apply_parity();
    unlock_dir(orig);
    CHECK_EQ(b.used, 0u);
    return mj_t_done("test_workcopy");
}
