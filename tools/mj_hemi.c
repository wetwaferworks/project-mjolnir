/* mj_hemi: the left and right hemispheres, on toy policies.
 *
 *   mj_hemi pipeline --data DIR [--classroom-rounds N] [--rounds N] ...
 *       Stage 1, the classroom (left acts, right watches, grades and flags;
 *       the actor learns from its errors and the watcher from its misses),
 *       then stage 2, the hemisphere swap, starting from the classroom's
 *       left and right. The final model is the last one kept.
 *   mj_hemi classroom --data DIR [--classroom-rounds N] ...   stage 1 only
 *   mj_hemi gate --data DIR --kind actor|watcher|swap --round N --lesson FILE... --holdout FILE...
 *                --parent SHA --candidate SHA [--other SHA] [--note TEXT]
 *       The keep-or-roll-back decision for a run on a real model (PCHIE's
 *       pchie_hemispheres asks it). Mjolnir works out every score itself from
 *       the sealed holdout sets, which must be bound to the named models by
 *       their SHA-256 (see mj_hemi_gate.h), then decides and journals.
 *       Exit 0 keep, 3 roll back, 2 evidence refused, 1 unreadable.
 *   mj_hemi swap --data DIR [--rounds N] [--scenario complement|ternary]
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
#include "mjolnir/mj_classroom.h"
#include "mjolnir/mj_hemi.h"
#include "mjolnir/mj_hemi_gate.h"
#include "mjolnir/mj_ledger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int usage(void) {
    fprintf(stderr,
            "usage: mj_hemi pipeline|classroom|swap --data DIR [--classroom-rounds N] [--rounds N]\n"
            "                   [--scenario complement|ternary] [--seed S]\n"
            "                   [--train-mazes N] [--holdout-mazes N] [--segments N] [--patience N] [--gpu]\n"
            "                   [--no-watcher] [--no-merge]\n"
            "       mj_hemi gate --data DIR --kind actor|watcher|swap --round N --lesson FILE... --holdout FILE...\n"
            "                --parent SHA --candidate SHA [--other SHA] [--note TEXT]\n"
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

static int cmd_run(int argc, char **argv, int do_classroom, int do_swap) {
    const char *data = NULL, *scenario = "complement";
    uint64_t rounds = 6, classroom_rounds = 4, seed = 1, train_mazes = 512, holdout_mazes = 2048, segments = 16, patience = 2, v;
    int gpu = 0, no_watcher = 0, no_merge = 0, i;
    uint32_t r, misses = 0, kept = 0, best_bp = 0;
    char best[MJ_SHA256_HEX] = "";
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
            else if (strcmp(argv[i], "--classroom-rounds") == 0 && v >= 1 && v <= 999) classroom_rounds = v;
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
    if (do_classroom) {
        uint32_t c, c_misses = 0, c_kept = 0;
        printf("== stage 1: the classroom (left acts, right watches)\n");
        for (c = 1; c <= classroom_rounds; ++c) {
            mj_classroom_cfg cc;
            mj_classroom_report cr;
            mj_status st;
            mj_classroom_default(&cc);
            cc.round = c;
            cc.world_seed = seed * 1000003ull + 900000ull + c;
            cc.holdout_seed = seed * 1000003ull + 950000ull + c;
            cc.mazes = (uint32_t)train_mazes;
            cc.holdout_mazes = (uint32_t)holdout_mazes;
            cc.segments = (uint32_t)segments;
            cc.train.seed = cc.world_seed;
            cc.use_gpu = gpu;
            if (no_watcher) cc.actor_lessons.watcher_weight = 0.0f;
            st = mj_classroom_toy(data, ledger, &teacher, &left, &right, &cc, &b, &cr);
            if (st != MJ_OK) {
                fprintf(stderr, "mj_hemi: classroom %u failed: %s\n", (unsigned)c, mj_status_name(st));
                return 1;
            }
            printf("classroom %u: actor %u -> %u bp (%s); watcher judgments %u -> %u bp, own play %u -> %u bp (%s)\n",
                   (unsigned)c, (unsigned)cr.actor_before_bp, (unsigned)cr.actor_after_bp,
                   cr.actor_kept ? "kept" : "restored", (unsigned)cr.watcher_judge_before_bp,
                   (unsigned)cr.watcher_judge_after_bp, (unsigned)cr.watcher_play_before_bp,
                   (unsigned)cr.watcher_play_after_bp, cr.watcher_kept ? "kept" : "restored");
            printf("  lesson: %u steps, cleared %u; watcher agreed-right %u, shared mistakes %u, objected-right %u, "
                   "objected-wrong %u, unsure %u; %u actor targets, %u watcher targets\n",
                   (unsigned)cr.tally.steps, (unsigned)cr.tally.cleared, (unsigned)cr.tally.agree_right,
                   (unsigned)cr.tally.agree_wrong, (unsigned)cr.tally.object_right, (unsigned)cr.tally.object_wrong,
                   (unsigned)cr.tally.unsure, (unsigned)cr.actor_targets, (unsigned)cr.watcher_targets);
            if (cr.actor_kept || cr.watcher_kept) { ++c_kept; c_misses = 0; }
            else if (++c_misses >= patience) {
                printf("classroom stopping: %u rounds in a row restored\n", (unsigned)c_misses);
                break;
            }
        }
        printf("classroom done: %u rounds changed a side\n", (unsigned)c_kept);
        print_model("left", &left);
        print_model("right", &right);
    }
    if (do_swap) printf("== stage 2: the hemisphere swap\n");
    for (r = 1; do_swap && r <= rounds; ++r) {
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
        st = mj_hemisphere_swap_toy(data, ledger, &teacher, &left, &right, &cfg, &b, &rep);
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
        if (rep.kept) {
            ++kept;
            misses = 0;
            memcpy(best, cs, sizeof best);
            best_bp = rep.child_holdout_bp;
        }
        else if (++misses >= patience) {
            printf("stopping: %u rounds in a row rolled back\n", (unsigned)misses);
            break;
        }
    }
    if (do_swap) printf("swap done: %u rounds kept\n", (unsigned)kept);
    if (do_swap && kept) printf("new model: child %s, holdout %u bp\n", best, (unsigned)best_bp);
    else if (do_swap) printf("new model: no swap child beat both parents; the left and right above stand\n");
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
    printf("%s set, round %u pass %u, %s side plays, world %u, %u features, %u moves, seed %llu\n", s.h.purpose == MJ_HEMI_HOLDOUT ? "holdout" : "lesson", (unsigned)s.h.round,
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

/* The keep-or-roll-back decision for a hemisphere run outside Mjolnir (a real
 * model behind PCHIE). The caller measured the scores on holdout worlds and
 * wrote the sample sets; Mjolnir checks the sets' seals, makes the decision
 * and journals it. Exit 0 keep, 3 roll back, 1 or 2 refused. */
static int hex64_ok(const char *s) {
    size_t i;
    if (strlen(s) != 64) return 0;
    for (i = 0; i < 64; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

static int cmd_gate(int argc, char **argv) {
    const char *data = NULL, *kind = NULL, *parent = NULL, *candidate = NULL, *other = NULL, *note = "-";
    const char *lesson_paths[MJ_HEMI_GATE_MAX_SETS], *holdout_paths[MJ_HEMI_GATE_MAX_SETS];
    mj_hemi_set sets[2 * MJ_HEMI_GATE_MAX_SETS];
    uint32_t n_lessons = 0, n_holdout = 0, n_loaded = 0, k;
    uint64_t round = 0;
    int i, rc = 1;
    char ledger[MJ_PATH_MAX];
    mj_budget b;
    mj_dream_record rec;
    mj_ledger_state ls;
    mj_hemi_gate_in in;
    mj_hemi_gate_out g;
    mj_status st;
    uint64_t bad;
    char inputs[MJ_TEXT_MAX + 1] = "";
    char when[21];
    for (i = 2; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (v == NULL) return usage();
        if (strcmp(a, "--data") == 0) data = v;
        else if (strcmp(a, "--kind") == 0) kind = v;
        else if (strcmp(a, "--lesson") == 0 && n_lessons < MJ_HEMI_GATE_MAX_SETS) lesson_paths[n_lessons++] = v;
        else if (strcmp(a, "--holdout") == 0 && n_holdout < MJ_HEMI_GATE_MAX_SETS) holdout_paths[n_holdout++] = v;
        else if (strcmp(a, "--parent") == 0) parent = v;
        else if (strcmp(a, "--candidate") == 0) candidate = v;
        else if (strcmp(a, "--other") == 0) other = v;
        else if (strcmp(a, "--note") == 0) note = v;
        else if (strcmp(a, "--round") == 0) { if (!parse_u64(v, &round) || round > 999) return usage(); }
        else return usage();
        ++i;
    }
    if (data == NULL || kind == NULL || n_lessons == 0 || n_holdout == 0 || parent == NULL || candidate == NULL ||
        round == 0)
        return usage();
    memset(&in, 0, sizeof in);
    if (strcmp(kind, "actor") == 0) in.kind = MJ_HEMI_GATE_ACTOR;
    else if (strcmp(kind, "watcher") == 0) in.kind = MJ_HEMI_GATE_WATCHER;
    else if (strcmp(kind, "swap") == 0) in.kind = MJ_HEMI_GATE_SWAP;
    else return usage();
    if ((in.kind == MJ_HEMI_GATE_SWAP) != (other != NULL)) return usage();
    if (!hex64_ok(parent) || !hex64_ok(candidate) || (other && !hex64_ok(other))) {
        fprintf(stderr, "mj_hemi: --parent, --candidate and --other must be 64 lowercase hex digits\n");
        return 2;
    }
    if (strlen(note) > 160 || !mj_text_valid(note)) {
        fprintf(stderr, "mj_hemi: --note must be at most 160 printable characters, without '|'\n");
        return 2;
    }
    mj_hex_decode(parent, 64, in.parent);
    mj_hex_decode(candidate, 64, in.candidate);
    if (other) mj_hex_decode(other, 64, in.other);
    mj_budget_init(&b, (size_t)1 << 31);
    in.budget = &b;
    for (k = 0; k < n_lessons + n_holdout; ++k) {
        const char *path = k < n_lessons ? lesson_paths[k] : holdout_paths[k - n_lessons];
        uint8_t sha[MJ_SHA256_LEN];
        char h[2 * MJ_SHA256_LEN + 1];
        size_t n = strlen(inputs);
        st = mj_hemi_load(path, &b, &sets[k], sha);
        if (st != MJ_OK) {
            fprintf(stderr, "mj_hemi: %s: %s\n", path, mj_status_name(st));
            goto done;
        }
        ++n_loaded;
        if (k < n_lessons) in.lessons[in.n_lessons++] = &sets[k];
        else in.holdout[in.n_holdout++] = &sets[k];
        hex(sha, MJ_SHA256_LEN, h);
        h[12] = 0;
        snprintf(inputs + n, sizeof inputs - n, "%s%s-%s", k ? "," : "", k < n_lessons ? "lesson" : "holdout", h);
    }
    st = mj_hemi_gate_decide(&in, &g);
    if (st != MJ_OK) {
        fprintf(stderr, "mj_hemi: refused: %s\n", st == MJ_E_INVAL ? g.why : mj_status_name(st));
        rc = 2;
        goto done;
    }
    if (mj_path_join(ledger, sizeof ledger, data, "hemispheres.mjl") != MJ_OK) { rc = 2; goto done; }
    mj_ledger_record_init(&rec);
    snprintf(rec.dream_id, sizeof rec.dream_id, "real-%s-%03u", kind, (unsigned)round);
    if (mj_utc_now(when) == MJ_OK) memcpy(rec.when, when, sizeof when);
    snprintf(rec.trigger, sizeof rec.trigger, "%s", in.kind == MJ_HEMI_GATE_SWAP ? "hemispheres" : "classroom");
    memcpy(rec.grader, "answer-key", 11);
    snprintf(rec.inputs, sizeof rec.inputs, "%s", inputs);
    snprintf(rec.weights_before, sizeof rec.weights_before, "%.12s", parent);
    snprintf(rec.weights_candidate, sizeof rec.weights_candidate, "%.12s", candidate);
    {
        mj_hemi_tally t;
        uint32_t steps = 0, cleared = 0;
        for (k = 0; k < in.n_lessons; ++k) {
            mj_hemi_tally_set(in.lessons[k], &t);
            steps += t.steps;
            cleared += t.cleared;
        }
        snprintf(rec.edit, sizeof rec.edit, "%u lesson steps, %u cleared; scored by Mjolnir on %u held-out states",
                 (unsigned)steps, (unsigned)cleared, (unsigned)g.states);
    }
    rec.score_before_bp = in.kind == MJ_HEMI_GATE_SWAP && g.other_bp > g.before_bp ? g.other_bp : g.before_bp;
    rec.score_after_bp = g.after_bp;
    if (in.kind == MJ_HEMI_GATE_WATCHER)
        snprintf(rec.gates, sizeof rec.gates, "judge=%s,play=%s", g.judge_after_bp > g.judge_before_bp ? "pass" : "fail",
                 g.after_bp >= g.before_bp ? "pass" : "fail");
    else if (in.kind == MJ_HEMI_GATE_SWAP)
        snprintf(rec.gates, sizeof rec.gates, "beats-first=%s,beats-second=%s", g.after_bp > g.before_bp ? "pass" : "fail",
                 g.after_bp > g.other_bp ? "pass" : "fail");
    else snprintf(rec.gates, sizeof rec.gates, "holdout=%s", g.keep ? "pass" : "fail");
    memcpy(rec.outcome, g.keep ? "dry-run-pass" : "rolled-back", g.keep ? 13 : 12);
    if (in.kind == MJ_HEMI_GATE_WATCHER)
        snprintf(rec.notes, sizeof rec.notes, "judgments %u->%u bp; own play %u->%u bp; %s", (unsigned)g.judge_before_bp,
                 (unsigned)g.judge_after_bp, (unsigned)g.before_bp, (unsigned)g.after_bp, note);
    else if (in.kind == MJ_HEMI_GATE_SWAP)
        snprintf(rec.notes, sizeof rec.notes, "parents %u and %u bp, child %u bp; %s", (unsigned)g.before_bp,
                 (unsigned)g.other_bp, (unsigned)g.after_bp, note);
    else snprintf(rec.notes, sizeof rec.notes, "%u->%u bp; %s", (unsigned)g.before_bp, (unsigned)g.after_bp, note);
    if (mj_fs_mkdir(data) != MJ_OK || mj_ledger_append(ledger, &b, &rec) != MJ_OK ||
        mj_ledger_verify_file(ledger, &b, &ls, &bad) != MJ_OK) {
        fprintf(stderr, "mj_hemi: could not journal the decision in %s\n", ledger);
        rc = 1;
        goto done;
    }
    if (in.kind == MJ_HEMI_GATE_WATCHER)
        printf("%s: judgments %u -> %u bp, own play %u -> %u bp on %u held-out states\n", rec.dream_id,
               (unsigned)g.judge_before_bp, (unsigned)g.judge_after_bp, (unsigned)g.before_bp, (unsigned)g.after_bp,
               (unsigned)g.states);
    else if (in.kind == MJ_HEMI_GATE_SWAP)
        printf("%s: parents %u and %u bp, child %u bp on %u held-out states\n", rec.dream_id, (unsigned)g.before_bp,
               (unsigned)g.other_bp, (unsigned)g.after_bp, (unsigned)g.states);
    else
        printf("%s: %u -> %u bp on %u held-out states\n", rec.dream_id, (unsigned)g.before_bp, (unsigned)g.after_bp,
               (unsigned)g.states);
    printf("%s: %s (journal %s, %llu records)\n", rec.dream_id, g.keep ? "keep" : "roll back", ledger,
           (unsigned long long)ls.count);
    rc = g.keep ? 0 : 3;
done:
    for (k = 0; k < n_loaded; ++k) mj_hemi_set_free(&sets[k]);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "pipeline") == 0) return cmd_run(argc, argv, 1, 1);
    if (strcmp(argv[1], "classroom") == 0) return cmd_run(argc, argv, 1, 0);
    if (strcmp(argv[1], "swap") == 0) return cmd_run(argc, argv, 0, 1);
    if (strcmp(argv[1], "gate") == 0) return cmd_gate(argc, argv);
    if (strcmp(argv[1], "show") == 0) return cmd_show(argc, argv);
    if (strcmp(argv[1], "replay") == 0) return cmd_replay(argc, argv);
    if (strcmp(argv[1], "bench") == 0) return cmd_bench(argc, argv);
    return usage();
}
