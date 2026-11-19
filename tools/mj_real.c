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
 *
 * Exit 0 on success, 1 when verify finds a mismatch, 2 when refused or on a
 * usage error.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_fs.h"
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
    "  mj_real delta-repack IN OUT\n";

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
