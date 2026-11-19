/* The real-weight path from the command line, with no model code: make the
 * working copy, apply any stored version to it (rolling back is applying
 * "base"), and check folders against a sums file. Writes the same store,
 * ledger and state files as the Python commands real-register, real-apply
 * and real-verify.
 *
 *   mj_real register --original DIR --working DIR --data DIR
 *   mj_real apply    --original DIR --working DIR --data DIR --version ID [--cpu] [--threads N]
 *   mj_real verify   --dir DIR --sums FILE
 *   mj_real status   --working DIR
 *   mj_real delta-info FILE
 *   mj_real delta-repack IN OUT     (unpack and pack again; the bytes must not change)
 *   mj_real lowrank IN OUT --rank R [--ridge X] [--shared N] [--cpu] [--threads N]
 *       IN holds float32 tensors k (n x in), o and g (n x out) and, optionally,
 *       background (m x in); OUT gets U (out x r), V (r x in) and s (n).
 *
 * Exit 0 on success, 1 when verify finds a mismatch, 2 when refused or on a
 * usage error.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_file.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_lowrank.h"
#include "mjolnir/mj_safet.h"
#include "mjolnir/mj_session.h"
#include "mjolnir/mj_workcopy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *USAGE =
    "usage:\n"
    "  mj_real register --original DIR --working DIR --data DIR\n"
    "  mj_real apply    --original DIR --working DIR --data DIR --version ID [--cpu] [--threads N]\n"
    "  mj_real verify   --dir DIR --sums FILE\n"
    "  mj_real status   --working DIR\n"
    "  mj_real delta-info FILE\n"
    "  mj_real delta-repack IN OUT\n"
    "  mj_real lowrank IN OUT --rank R [--ridge X] [--shared N] [--cpu] [--threads N]\n";

typedef struct args {
    const char *original, *working, *data, *version, *dir, *sums;
    int cpu;
    unsigned threads;
} args;

static int parse(int argc, char **argv, args *a) {
    int i;
    memset(a, 0, sizeof *a);
    for (i = 2; i < argc; ++i) {
        const char **slot = NULL;
        if (strcmp(argv[i], "--cpu") == 0) { a->cpu = 1; continue; }
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            unsigned long t = strtoul(argv[++i], NULL, 10);
            if (t == 0 || t > 64) return 0;
            a->threads = (unsigned)t;
            continue;
        }
        if (strcmp(argv[i], "--original") == 0) slot = &a->original;
        else if (strcmp(argv[i], "--working") == 0) slot = &a->working;
        else if (strcmp(argv[i], "--data") == 0) slot = &a->data;
        else if (strcmp(argv[i], "--version") == 0) slot = &a->version;
        else if (strcmp(argv[i], "--dir") == 0) slot = &a->dir;
        else if (strcmp(argv[i], "--sums") == 0) slot = &a->sums;
        if (slot == NULL || i + 1 >= argc || strlen(argv[i + 1]) >= MJ_PATH_MAX) return 0;
        *slot = argv[++i];
    }
    return 1;
}

static int refused(mj_status st, const char *why) {
    fprintf(stderr, "refused: %s (%s)\n", why && *why ? why : "see the status", mj_status_name(st));
    return 2;
}

static int cmd_verify(const args *a, mj_budget *b) {
    uint8_t *text;
    size_t n, i;
    mj_sums want, got;
    int bad = 0;
    mj_status st = mj_fs_read_all(a->sums, 16u * 1024u * 1024u, b, &text, &n);
    if (st != MJ_OK) return refused(st, "cannot read the sums file");
    st = mj_wc_parse_sums((const char *)text, n, b, &want);
    mj_bfree(b, text, n ? n : 1);
    if (st != MJ_OK) return refused(st, "the sums file does not parse");
    if ((st = mj_wc_checksums(a->dir, b, &got)) != MJ_OK) {
        mj_sums_free(&want);
        return refused(st, "cannot hash the folder");
    }
    for (i = 0; i < want.n; ++i) {
        const mj_sum *g = mj_wc_sums_find(&got, want.v[i].name);
        if (g == NULL || g->size != want.v[i].size || strcmp(g->sha, want.v[i].sha) != 0) {
            printf(bad ? ", %s" : "MISMATCH: %s", want.v[i].name);
            bad = 1;
        }
    }
    if (bad) printf("\n");
    else printf("OK %lu files match\n", (unsigned long)want.n);
    mj_sums_free(&want);
    mj_sums_free(&got);
    return bad;
}

static int cmd_status(const args *a, mj_budget *b) {
    mj_wc_state s;
    size_t i;
    mj_status st = mj_wc_read_state(a->working, b, &s);
    if (st != MJ_OK) return refused(st, "working copy state file");
    printf("applied %s\ntouched %lu\n", s.applied, (unsigned long)s.touched.n);
    for (i = 0; i < s.touched.n; ++i) printf("  %s\n", s.touched.v[i]);
    if (s.dirty) {
        printf("DIRTY: a write into the working copy was cut short; %lu tensors may be half-written.\n"
               "Apply any version (or base) again to repair it.\n",
               (unsigned long)s.dirty_touched.n);
    }
    mj_wc_state_free(&s);
    return s.dirty ? 1 : 0;
}

static int cmd_delta_info(const char *path, mj_budget *b) {
    uint8_t *data;
    size_t n;
    uint32_t k;
    static mj_delta d;
    mj_status st = mj_fs_read_all(path, (size_t)1 << 30, b, &data, &n);
    if (st != MJ_OK) return refused(st, "cannot read the delta");
    st = mj_delta_unpack(data, n, b, &d);
    mj_bfree(b, data, n ? n : 1);
    if (st != MJ_OK) return refused(st, "not a valid MJDELTA1 file");
    printf("base %s\nentries %u\n", d.base, d.count);
    for (k = 0; k < d.count; ++k) {
        char rep[32];
        (void)mj_float_repr(d.e[k].scale, rep);
        printf("  %s rows=%u cols=%u rank=%u scale=%s\n", d.e[k].name, d.e[k].rows, d.e[k].cols, d.e[k].rank, rep);
    }
    mj_delta_free(&d);
    return 0;
}

static int cmd_delta_repack(const char *in, const char *outp, mj_budget *b) {
    uint8_t *data, *again;
    size_t n, m;
    static mj_delta d;
    mj_status st = mj_fs_read_all(in, (size_t)1 << 30, b, &data, &n);
    if (st != MJ_OK) return refused(st, "cannot read the delta");
    st = mj_delta_unpack(data, n, b, &d);
    mj_bfree(b, data, n ? n : 1);
    if (st != MJ_OK) return refused(st, "not a valid MJDELTA1 file");
    st = mj_delta_pack(d.base, d.e, d.count, b, &again, &m);
    mj_delta_free(&d);
    if (st != MJ_OK) return refused(st, "cannot pack it again");
    st = mj_fs_write_atomic(outp, again, m, 0);
    mj_bfree(b, again, m);
    if (st != MJ_OK) return refused(st, "cannot write the output (it must not exist yet)");
    printf("repacked %lu bytes\n", (unsigned long)m);
    return 0;
}

/* ---- lowrank: safetensors in, safetensors out ---- */

static mj_status load_f32(const char *path, const mj_st_header *h, const char *name, uint32_t want_dims, float **out,
                          uint64_t shape[2], mj_budget *b) {
    const mj_st_tensor *t = mj_st_find(h, name);
    mj_file f;
    uint8_t *raw;
    uint64_t i;
    mj_status st;
    *out = NULL;
    if (t == NULL) return MJ_E_NOTFOUND;
    if (t->dtype != MJ_ST_F32 || t->ndim != want_dims || t->count == 0 || t->nbytes > ((uint64_t)1 << 32)) return MJ_E_INVAL;
    shape[0] = t->shape[0];
    shape[1] = want_dims > 1 ? t->shape[1] : 1;
    raw = (uint8_t *)mj_balloc(b, (size_t)t->nbytes);
    *out = (float *)mj_balloc(b, (size_t)t->nbytes);
    if (raw == NULL || *out == NULL) return MJ_E_LIMIT;
    if ((st = mj_file_open(&f, path, 0)) != MJ_OK) return st;
    st = mj_file_pread(&f, t->abs_offset, raw, (size_t)t->nbytes);
    mj_file_close(&f);
    for (i = 0; st == MJ_OK && i < t->count; ++i) {
        uint32_t u = (uint32_t)raw[4 * i] | ((uint32_t)raw[4 * i + 1] << 8) | ((uint32_t)raw[4 * i + 2] << 16) |
                     ((uint32_t)raw[4 * i + 3] << 24);
        memcpy(&(*out)[i], &u, 4);
    }
    mj_bfree(b, raw, (size_t)t->nbytes);
    return st;
}

static mj_status write_f32_safetensors(const char *path, const char **names, const uint64_t (*shapes)[2],
                                       const uint32_t *dims, const float **data, int count, mj_budget *b) {
    char json[1024];
    size_t o = 0, total = 0, hl, k, at;
    uint64_t off = 0;
    uint8_t *buf;
    int i;
    mj_status st;
    o += (size_t)snprintf(json + o, sizeof json - o, "{");
    for (i = 0; i < count; ++i) {
        uint64_t n = shapes[i][0] * (dims[i] > 1 ? shapes[i][1] : 1);
        if (dims[i] > 1)
            o += (size_t)snprintf(json + o, sizeof json - o, "%s\"%s\":{\"dtype\":\"F32\",\"shape\":[%llu,%llu],\"data_offsets\":[%llu,%llu]}",
                                  i ? "," : "", names[i], (unsigned long long)shapes[i][0], (unsigned long long)shapes[i][1],
                                  (unsigned long long)off, (unsigned long long)(off + 4 * n));
        else
            o += (size_t)snprintf(json + o, sizeof json - o, "%s\"%s\":{\"dtype\":\"F32\",\"shape\":[%llu],\"data_offsets\":[%llu,%llu]}",
                                  i ? "," : "", names[i], (unsigned long long)shapes[i][0], (unsigned long long)off,
                                  (unsigned long long)(off + 4 * n));
        off += 4 * n;
    }
    o += (size_t)snprintf(json + o, sizeof json - o, "}");
    while (o % 8) json[o++] = ' ';
    hl = o;
    total = 8 + hl + (size_t)off;
    buf = (uint8_t *)mj_balloc(b, total);
    if (buf == NULL) return MJ_E_LIMIT;
    for (k = 0; k < 8; ++k) buf[k] = (uint8_t)((uint64_t)hl >> (8 * k));
    memcpy(buf + 8, json, hl);
    at = 8 + hl;
    for (i = 0; i < count; ++i) {
        uint64_t n = shapes[i][0] * (dims[i] > 1 ? shapes[i][1] : 1), e;
        for (e = 0; e < n; ++e, at += 4) {
            uint32_t u;
            memcpy(&u, &data[i][e], 4);
            buf[at] = (uint8_t)u; buf[at + 1] = (uint8_t)(u >> 8); buf[at + 2] = (uint8_t)(u >> 16); buf[at + 3] = (uint8_t)(u >> 24);
        }
    }
    st = mj_fs_write_atomic(path, buf, total, 0);
    mj_bfree(b, buf, total);
    return st;
}

static int cmd_lowrank(int argc, char **argv, mj_budget *b) {
    mj_lr_input li;
    mj_lr_result lr;
    mj_st_header h;
    uint64_t sk[2], so[2], sg[2], sb[2];
    float *k = NULL, *o = NULL, *g = NULL, *bg = NULL;
    int i;
    mj_status st;
    uint64_t t0 = mj_clock_ms();
    memset(&li, 0, sizeof li);
    li.ridge = 0.1;
    li.use_cuda = 1;
    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0) li.use_cuda = 0;
        else if (strcmp(argv[i], "--rank") == 0 && i + 1 < argc) li.rank = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--ridge") == 0 && i + 1 < argc) li.ridge = strtod(argv[++i], NULL);
        else if (strcmp(argv[i], "--shared") == 0 && i + 1 < argc) li.shared_weight = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) li.threads = (unsigned)strtoul(argv[++i], NULL, 10);
        else { fprintf(stderr, "%s", USAGE); return 2; }
    }
    if ((st = mj_st_read_header(argv[2], b, &h)) != MJ_OK) return refused(st, "the input is not a safetensors file");
    st = load_f32(argv[2], &h, "k", 2, &k, sk, b);
    if (st == MJ_OK) st = load_f32(argv[2], &h, "o", 2, &o, so, b);
    if (st == MJ_OK) st = load_f32(argv[2], &h, "g", 2, &g, sg, b);
    if (st == MJ_OK && mj_st_find(&h, "background")) st = load_f32(argv[2], &h, "background", 2, &bg, sb, b);
    else sb[0] = sb[1] = 0;
    mj_st_free(&h);
    if (st != MJ_OK) return refused(st, "the input needs float32 tensors k, o, g (and optionally background)");
    if (so[0] != sk[0] || sg[0] != sk[0] || so[1] != sg[1] || (bg && sb[1] != sk[1]) || sk[0] > MJ_LR_MAX_EXAMPLES ||
        sk[1] > MJ_LR_MAX_DIM || so[1] > MJ_LR_MAX_DIM || sb[0] > MJ_LR_MAX_BACKGROUND)
        return refused(MJ_E_INVAL, "the tensor shapes do not fit together");
    li.n = (uint32_t)sk[0];
    li.in = (uint32_t)sk[1];
    li.out = (uint32_t)so[1];
    li.k = k;
    li.o = o;
    li.g = g;
    li.m = (uint32_t)sb[0];
    li.bg = bg;
    if ((st = mj_lowrank(&li, b, &lr)) != MJ_OK) return refused(st, "the low-rank solve failed");
    {
        const char *names[3] = {"U", "V", "s"};
        uint64_t shapes[3][2];
        uint32_t dims[3] = {2, 2, 1};
        const float *data[3];
        float sv[MJ_LR_MAX_EXAMPLES];
        uint32_t j;
        for (j = 0; j < li.n; ++j) sv[j] = (float)lr.s[j];
        shapes[0][0] = li.out; shapes[0][1] = lr.r;
        shapes[1][0] = lr.r; shapes[1][1] = li.in;
        shapes[2][0] = li.n; shapes[2][1] = 1;
        data[0] = lr.U; data[1] = lr.V; data[2] = sv;
        st = write_f32_safetensors(argv[3], names, (const uint64_t(*)[2])shapes, dims, data, 3, b);
    }
    printf("rank %u from %u examples (out %u, in %u, background %u + %u shared) on the %s, %.2fs\n", lr.r, li.n, li.out,
           li.in, li.m, li.shared_weight, lr.used_cuda ? "GPU" : "CPU", (double)(mj_clock_ms() - t0) / 1000.0);
    mj_lr_free(&lr);
    if (st != MJ_OK) return refused(st, "cannot write the output (it must not exist yet)");
    return 0;
}

int main(int argc, char **argv) {
    args a;
    mj_budget b;
    const char *cmd;
    mj_status st;
    if (argc < 2) { fprintf(stderr, "%s", USAGE); return 2; }
    cmd = argv[1];
    mj_budget_init(&b, sizeof(size_t) > 4 ? (size_t)6 << 30 : (size_t)1 << 30);
    if (strcmp(cmd, "delta-info") == 0 && argc == 3) return cmd_delta_info(argv[2], &b);
    if (strcmp(cmd, "delta-repack") == 0 && argc == 4) return cmd_delta_repack(argv[2], argv[3], &b);
    if (strcmp(cmd, "lowrank") == 0 && argc >= 4) return cmd_lowrank(argc, argv, &b);
    if (!parse(argc, argv, &a)) { fprintf(stderr, "%s", USAGE); return 2; }
    if (strcmp(cmd, "register") == 0 && a.original && a.working && a.data) {
        char msg[200];
        uint64_t t0 = mj_clock_ms();
        st = mj_real_register(a.original, a.working, a.data, &b, msg, sizeof msg);
        if (st != MJ_OK) return refused(st, msg);
        printf("%s (%.1fs)\n", msg, (double)(mj_clock_ms() - t0) / 1000.0);
        return 0;
    }
    if (strcmp(cmd, "apply") == 0 && a.original && a.working && a.data && a.version) {
        mj_wc_options o;
        mj_wc_result r;
        uint64_t t0 = mj_clock_ms();
        o.use_cuda = !a.cpu;
        o.threads = a.threads;
        st = mj_real_apply(a.original, a.working, a.data, a.version, &o, &b, &r);
        if (st != MJ_OK) return refused(st, r.reason);
        printf("working copy now holds %s (%lu tensors written, %lu edited, %s, %.1fs)\n", a.version,
               (unsigned long)r.touched, (unsigned long)r.edited, r.used_cuda ? "GPU" : "CPU",
               (double)(mj_clock_ms() - t0) / 1000.0);
        if (r.used_cuda) printf("GPU memory used by the apply: %.1f MiB\n", (double)r.device_peak / 1048576.0);
        return 0;
    }
    if (strcmp(cmd, "verify") == 0 && a.dir && a.sums) return cmd_verify(&a, &b);
    if (strcmp(cmd, "status") == 0 && a.working) return cmd_status(&a, &b);
    fprintf(stderr, "%s", USAGE);
    return 2;
}
