/* mj_hemi: the left and right hemispheres, on toy policies.
 *
 *   mj_hemi toy --data DIR [--rounds N] [--scenario complement|ternary]
 *               [--seed S] [--train-mazes N] [--holdout-mazes N] [--segments N]
 *               [--patience N] [--gpu] [--no-watcher] [--no-merge]
 *       Runs rounds until N rounds are done or N rounds in a row are rolled
 *       back (patience). Writes DIR/round-NNN/{left,right,child}.mjtoy,
 *       pass1.mjhemi, pass2.mjhemi, disagreements.tsv and the journal
 *       DIR/hemispheres.mjl. DIR must not exist yet. --no-watcher and
 *       --no-merge are ablations: they drop the watchers' targets or the
 *       weight merge, to measure what each one adds.
 *   mj_hemi show FILE           header and tallies of a sample set
 *   mj_hemi replay FILE [--limit N]   every step of a sample set, as TSV
 *   mj_hemi bench [--states N]  CPU and GPU batch scorer: time and parity
 *
 * Only toy policies are written here. Real models take part through PCHIE's
 * pchie_hemispheres tool, which writes the same sample-set format.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_hemi.h"
#include "mjolnir/mj_ledger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int usage(void) {
    fprintf(stderr,
            "usage: mj_hemi toy --data DIR [--rounds N] [--scenario complement|ternary] [--seed S]\n"
            "                   [--train-mazes N] [--holdout-mazes N] [--segments N] [--patience N] [--gpu]\n"
            "                   [--no-watcher] [--no-merge]\n"
            "       mj_hemi show FILE\n"
            "       mj_hemi replay FILE [--limit N]\n"
            "       mj_hemi bench [--states N]\n");
    return 2;
}

static int parse_u64(const char *s, uint64_t *out) {
    return s != NULL && mj_parse_u64(s, strlen(s), out);
}

static void hex(const uint8_t *p, size_t n, char *out) { mj_hex_encode(p, n, out); }

static void print_model(const char *label, const mj_toy_model *m) {
    uint8_t blob[MJ_TOY_BLOB_LEN], sha[MJ_SHA256_LEN];
    char h[MJ_SHA256_HEX];
    if (mj_toy_save(m, blob) != MJ_OK) return;
    mj_sha256(blob, sizeof blob, sha);
    hex(sha, MJ_SHA256_LEN, h);
    printf("%-6s sha256 %s\n", label, h);
}

static int cmd_toy(int argc, char **argv) {
    const char *data = NULL, *scenario = "complement";
    uint64_t rounds = 6, seed = 1, train_mazes = 512, holdout_mazes = 2048, segments = 16, patience = 2, v;
    int gpu = 0, no_watcher = 0, no_merge = 0, i;
    uint32_t r, misses = 0, kept = 0;
    mj_toy_model teacher, left, right;
    mj_budget b;
    char ledger[MJ_PATH_MAX];
    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) data = argv[++i];
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) scenario = argv[++i];
        else if (strcmp(argv[i], "--gpu") == 0) gpu = 1;
        else if (strcmp(argv[i], "--no-watcher") == 0) no_watcher = 1;
        else if (strcmp(argv[i], "--no-merge") == 0) no_merge = 1;
        else if (i + 1 < argc && parse_u64(argv[i + 1], &v)) {
            if (strcmp(argv[i], "--rounds") == 0 && v >= 1 && v <= 999) rounds = v;
            else if (strcmp(argv[i], "--seed") == 0) seed = v;
            else if (strcmp(argv[i], "--train-mazes") == 0 && v >= 1 && v <= 65536) train_mazes = v;
            else if (strcmp(argv[i], "--holdout-mazes") == 0 && v >= 1 && v <= 65536) holdout_mazes = v;
            else if (strcmp(argv[i], "--segments") == 0 && v >= 1 && v <= MJ_TOY_MAX_SEGMENTS) segments = v;
            else if (strcmp(argv[i], "--patience") == 0 && v >= 1 && v <= 999) patience = v;
            else return usage();
            ++i;
        } else return usage();
    }
    if (data == NULL) return usage();
    if (mj_fs_exists(data)) {
        fprintf(stderr, "mj_hemi: %s already exists; every run gets a new data folder\n", data);
        return 1;
    }
    if (mj_fs_mkdir(data) != MJ_OK || mj_path_join(ledger, sizeof ledger, data, "hemispheres.mjl") != MJ_OK) {
        fprintf(stderr, "mj_hemi: cannot make %s\n", data);
        return 1;
    }
    mj_budget_init(&b, (size_t)1 << 31);
    mj_hemi_toy_teacher(seed, &teacher);
    if (mj_hemi_toy_parents(scenario, seed, &teacher, &left, &right) != MJ_OK) return usage();
    printf("scenario %s%s%s, seed %llu, %llu train mazes, %llu holdout mazes, %llu segments, scorer %s\n", scenario,
           no_watcher ? ", no watcher targets" : "", no_merge ? ", no merge" : "",
           (unsigned long long)seed, (unsigned long long)train_mazes, (unsigned long long)holdout_mazes,
           (unsigned long long)segments, gpu && mj_hemi_gpu_built() ? "gpu" : "cpu");
    print_model("left", &left);
    print_model("right", &right);
    for (r = 1; r <= rounds; ++r) {
        mj_hemi_round_cfg cfg;
        mj_hemi_round_report rep;
        mj_status st;
        char ps1[MJ_SHA256_HEX], ps2[MJ_SHA256_HEX], cs[MJ_SHA256_HEX];
        memset(&cfg, 0, sizeof cfg);
        cfg.round = r;
        cfg.train_seed = seed * 1000003ull + r;
        cfg.holdout_seed = seed * 1000003ull + 500000ull + r; /* never a training seed */
        cfg.train_mazes = (uint32_t)train_mazes;
        cfg.holdout_mazes = (uint32_t)holdout_mazes;
        cfg.segments = (uint32_t)segments;
        mj_hemi_combine_default(&cfg.combine);
        mj_hemi_train_default(&cfg.train);
        cfg.train.seed = cfg.train_seed;
        cfg.use_gpu = gpu;
        cfg.no_merge = no_merge;
        if (no_watcher) cfg.combine.watcher_weight = 0.0f;
        st = mj_hemi_toy_round(data, ledger, &teacher, &left, &right, &cfg, &b, &rep);
        if (st != MJ_OK) {
            fprintf(stderr, "mj_hemi: round %u failed: %s\n", (unsigned)r, mj_status_name(st));
            return 1;
        }
        hex(rep.pass_sha[0], MJ_SHA256_LEN, ps1);
        hex(rep.pass_sha[1], MJ_SHA256_LEN, ps2);
        hex(rep.child_sha, MJ_SHA256_LEN, cs);
        printf("round %u: holdout left %u right %u child %u bp -> %s\n", (unsigned)r, (unsigned)rep.left_holdout_bp,
               (unsigned)rep.right_holdout_bp, (unsigned)rep.child_holdout_bp, rep.kept ? "kept" : "rolled back");
        printf("  pass 1 (left plays): %u steps, cleared %u; watcher agreed-right %u, shared mistakes %u, "
               "objected-right %u, objected-wrong %u, unsure %u\n",
               (unsigned)rep.tally[0].steps, (unsigned)rep.tally[0].cleared, (unsigned)rep.tally[0].agree_right,
               (unsigned)rep.tally[0].agree_wrong, (unsigned)rep.tally[0].object_right,
               (unsigned)rep.tally[0].object_wrong, (unsigned)rep.tally[0].unsure);
        printf("  pass 2 (right plays): %u steps, cleared %u; watcher agreed-right %u, shared mistakes %u, "
               "objected-right %u, objected-wrong %u, unsure %u\n",
               (unsigned)rep.tally[1].steps, (unsigned)rep.tally[1].cleared, (unsigned)rep.tally[1].agree_right,
               (unsigned)rep.tally[1].agree_wrong, (unsigned)rep.tally[1].object_right,
               (unsigned)rep.tally[1].object_wrong, (unsigned)rep.tally[1].unsure);
        printf("  merge right-share %u/65536, %u targets (%u from watchers), loss %.4f\n", (unsigned)rep.alpha_q16,
               (unsigned)rep.targets, (unsigned)rep.watcher_targets, (double)rep.loss);
        printf("  pass1 %s\n  pass2 %s\n  child %s\n  %s\n", ps1, ps2, cs, rep.why);
        if (rep.kept) { ++kept; misses = 0; }
        else if (++misses >= patience) {
            printf("stopping: %u rounds in a row rolled back\n", (unsigned)misses);
            break;
        }
    }
    printf("done: %u rounds kept\n", (unsigned)kept);
    print_model("left", &left);
    print_model("right", &right);
    {
        mj_ledger_state ls;
        uint64_t bad = 0;
        mj_status st = mj_ledger_verify_file(ledger, &b, &ls, &bad);
        printf("journal %s: %s, %llu records\n", ledger, st == MJ_OK ? "chain OK" : mj_status_name(st),
               (unsigned long long)ls.count);
        return st == MJ_OK ? 0 : 1;
    }
}

static int load_set(const char *path, mj_budget *b, mj_hemi_set *s) {
    uint8_t sha[MJ_SHA256_LEN];
    mj_status st = mj_hemi_load(path, b, s, sha);
    if (st != MJ_OK) {
        fprintf(stderr, "mj_hemi: %s refused: %s\n", path, mj_status_name(st));
        return 0;
    }
    return 1;
}

static int cmd_show(int argc, char **argv) {
    mj_budget b;
    mj_hemi_set s;
    mj_hemi_tally t;
    char a[2 * MJ_HEMI_FINGERPRINT + 1], o[2 * MJ_HEMI_FINGERPRINT + 1];
    if (argc != 3) return usage();
    mj_budget_init(&b, (size_t)1 << 31);
    if (!load_set(argv[2], &b, &s)) return 1;
    mj_hemi_tally_set(&s, &t);
    hex(s.h.actor, MJ_HEMI_FINGERPRINT, a);
    hex(s.h.observer, MJ_HEMI_FINGERPRINT, o);
    printf("round %u pass %u, %s side plays, world %u, %u features, %u moves, seed %llu\n", (unsigned)s.h.round,
           (unsigned)s.h.pass, s.h.actor_side ? "right" : "left", (unsigned)s.h.world, (unsigned)s.h.n_features,
           (unsigned)s.h.n_actions, (unsigned long long)s.h.seed);
    printf("actor %s\nwatcher %s\n", a, o);
    printf("%u steps: cleared %u, not yet %u; watcher agreed-right %u, shared mistakes %u, objected-right %u, "
           "objected-wrong %u, unsure %u\n",
           (unsigned)t.steps, (unsigned)t.cleared, (unsigned)t.not_yet, (unsigned)t.agree_right,
           (unsigned)t.agree_wrong, (unsigned)t.object_right, (unsigned)t.object_wrong, (unsigned)t.unsure);
    mj_hemi_set_free(&s);
    return 0;
}

static int cmd_replay(int argc, char **argv) {
    mj_budget b;
    mj_hemi_set s;
    uint64_t limit = UINT64_MAX;
    uint32_t i, a;
    if (argc != 3 && !(argc == 5 && strcmp(argv[3], "--limit") == 0 && parse_u64(argv[4], &limit))) return usage();
    mj_budget_init(&b, (size_t)1 << 31);
    if (!load_set(argv[2], &b, &s)) return 1;
    printf("episode\tstep\tmove\tgrade\twatcher_move\treason\tscore_bp\tconfidence_bp\tactor_probs\twatcher_probs\n");
    for (i = 0; i < s.n && i < limit; ++i) {
        const mj_hemi_record *r = &s.r[i];
        printf("%u\t%u\t%u\t%u\t%u\t%u\t%d\t%u\t", (unsigned)r->episode, (unsigned)r->step, (unsigned)r->actor_move,
               (unsigned)r->grade, (unsigned)r->verdict.choice, (unsigned)r->verdict.reason, (int)r->verdict.score_bp,
               (unsigned)r->verdict.confidence_bp);
        for (a = 0; a < s.h.n_actions; ++a) printf("%s%u", a ? "," : "", (unsigned)r->actor_probs[a]);
        printf("\t");
        for (a = 0; a < s.h.n_actions; ++a) printf("%s%u", a ? "," : "", (unsigned)r->watcher_probs[a]);
        printf("\n");
    }
    mj_hemi_set_free(&s);
    return 0;
}

static double now_ms(void) { return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC; }

static int cmd_bench(int argc, char **argv) {
    uint64_t n = 4u << 20, s = 7;
    uint32_t i, k = 3, diff = 0;
    int8_t *states;
    uint8_t *cpu, *gpu;
    mj_toy_model m[3];
    double t0, t1, t2;
    char err[128] = "";
    mj_status st;
    if (argc == 4 && strcmp(argv[2], "--states") == 0) {
        if (!parse_u64(argv[3], &n) || n == 0 || n > (64u << 20)) return usage();
    } else if (argc != 2) return usage();
    states = (int8_t *)malloc((size_t)n * MJ_TOY_F);
    cpu = (uint8_t *)malloc((size_t)n * k);
    gpu = (uint8_t *)malloc((size_t)n * k);
    if (!states || !cpu || !gpu) return 1;
    for (i = 0; i < (uint32_t)n * MJ_TOY_F; ++i) states[i] = (int8_t)(mj_hemi_next(&s) & 0xFF);
    for (i = 0; i < k; ++i) mj_hemi_toy_teacher(100 + i, &m[i]);
    t0 = now_ms();
    (void)mj_hemi_toy_act_batch_cpu(m, k, states, (uint32_t)n, cpu);
    t1 = now_ms();
    st = mj_hemi_toy_act_batch_gpu(m, k, states, (uint32_t)n, gpu, err);
    t2 = now_ms();
    printf("%llu states x %u policies: cpu %.1f ms\n", (unsigned long long)n, (unsigned)k, t1 - t0);
    if (st != MJ_OK) {
        printf("gpu: %s\n", err);
    } else {
        for (i = 0; i < (uint32_t)n * k; ++i) diff += cpu[i] != gpu[i];
        printf("gpu %.1f ms (with copies), %u of %llu moves differ\n", t2 - t1, (unsigned)diff,
               (unsigned long long)n * k);
    }
    free(states);
    free(cpu);
    free(gpu);
    return diff ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "toy") == 0) return cmd_toy(argc, argv);
    if (strcmp(argv[1], "show") == 0) return cmd_show(argc, argv);
    if (strcmp(argv[1], "replay") == 0) return cmd_replay(argc, argv);
    if (strcmp(argv[1], "bench") == 0) return cmd_bench(argc, argv);
    return usage();
}
