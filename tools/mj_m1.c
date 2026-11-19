/* mj_m1: the stage M1 commands of `python -m mjolnir`, in C.
 *
 *   mj_m1 replays      --runs FOLDER [FOLDER ...]
 *   mj_m1 replays-json --runs FOLDER [FOLDER ...]
 *   mj_m1 grade        --data FOLDER --replay ID --step N --label good|bad|productive [--grader NAME] [--auto LABEL]
 *   mj_m1 grades       --data FOLDER
 *   mj_m1 toy-seed     --data FOLDER
 *   mj_m1 toy-nap      --runs FOLDER ... --data FOLDER [--integrate] [--steps N] [--max-shift-bp N] [--id ID] [--seed N]
 *
 * --data is where stores, ledgers and grades live (toy-store, toy-dreams.mjl,
 * grades.mjl), the same layout as the Python commands. Keep it outside any
 * shared or published folder: it holds run data. Run folders are only read.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_grades.h"
#include "mjolnir/mj_replay.h"
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_toytrain.h"
#include "mjolnir/mj_delta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *USAGE =
    "usage:\n"
    "  mj_m1 replays      --runs FOLDER [FOLDER ...]\n"
    "  mj_m1 replays-json --runs FOLDER [FOLDER ...]\n"
    "  mj_m1 grade        --data FOLDER --replay ID --step N --label good|bad|productive [--grader NAME] [--auto LABEL]\n"
    "  mj_m1 grades       --data FOLDER\n"
    "  mj_m1 toy-seed     --data FOLDER\n"
    "  mj_m1 toy-nap      --runs FOLDER ... --data FOLDER [--integrate] [--steps N] [--max-shift-bp N] [--id ID] [--seed N]\n";

#define MAX_ROOTS 64

typedef struct opts {
    const char *roots[MAX_ROOTS];
    uint32_t n_roots;
    const char *data, *replay, *label, *grader, *autolab, *id;
    long long step, steps, max_shift, seed;
    int have_step, integrate;
} opts;

static int num(const char *s, long long *out) {
    char *e;
    long long v = strtoll(s, &e, 10);
    if (*s == '\0' || *e != '\0') return 0;
    *out = v;
    return 1;
}

static int parse(int argc, char **argv, opts *o) {
    int i;
    memset(o, 0, sizeof *o);
    o->grader = "JD";
    o->steps = 300;
    o->max_shift = 2000;
    o->seed = 1;
    for (i = 2; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--runs") == 0) {
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                if (o->n_roots >= MAX_ROOTS || strlen(argv[i + 1]) >= MJ_PATH_MAX) return 0;
                o->roots[o->n_roots++] = argv[++i];
            }
        } else if (strcmp(a, "--integrate") == 0) {
            o->integrate = 1;
        } else if (i + 1 < argc && strlen(argv[i + 1]) < MJ_PATH_MAX) {
            const char *v = argv[++i];
            if (strcmp(a, "--data") == 0) o->data = v;
            else if (strcmp(a, "--replay") == 0) o->replay = v;
            else if (strcmp(a, "--label") == 0) o->label = v;
            else if (strcmp(a, "--grader") == 0) o->grader = v;
            else if (strcmp(a, "--auto") == 0) o->autolab = v;
            else if (strcmp(a, "--id") == 0) o->id = v;
            else if (strcmp(a, "--step") == 0) { if (!num(v, &o->step)) return 0; o->have_step = 1; }
            else if (strcmp(a, "--steps") == 0) { if (!num(v, &o->steps) || o->steps < 1 || o->steps > 1000000) return 0; }
            else if (strcmp(a, "--max-shift-bp") == 0) { if (!num(v, &o->max_shift) || o->max_shift < 0 || o->max_shift > 10000) return 0; }
            else if (strcmp(a, "--seed") == 0) { if (!num(v, &o->seed)) return 0; }
            else return 0;
        } else {
            return 0;
        }
    }
    return 1;
}

static void skipped(const char *path, const char *why, void *user) {
    const char *b = path, *q;
    (void)user;
    for (q = path; *q; ++q)
        if (*q == '/' || *q == '\\') b = q + 1;
    fprintf(stderr, "skipped %s: %s\n", b, why);
}

static int join(char *out, const char *a, const char *b) { return mj_path_join(out, MJ_PATH_MAX, a, b) == MJ_OK; }

static int open_store(mj_store *s, const char *data, mj_budget *b) {
    char root[MJ_PATH_MAX];
    mj_store_limits lim;
    mj_store_default_limits(&lim);
    if (!join(root, data, "toy-store")) return 0;
    if (mj_fs_mkdir(data) != MJ_OK || mj_fs_mkdir(root) != MJ_OK) return 0;
    return mj_store_open(s, root, &lim, b) == MJ_OK;
}

static void print_float(double v) {
    char buf[32];
    mj_float_repr(v, buf);
    fputs(buf, stdout);
}

int main(int argc, char **argv) {
    opts o;
    mj_budget b;
    const char *cmd;
    mj_status st;
    if (argc < 2 || !parse(argc, argv, &o)) {
        fputs(USAGE, stderr);
        return 2;
    }
    cmd = argv[1];
    mj_budget_init(&b, (size_t)2 << 30);

    if (strcmp(cmd, "replays") == 0 || strcmp(cmd, "replays-json") == 0) {
        mj_replay_set set;
        uint32_t i, total = 0;
        if (o.n_roots == 0) { fputs(USAGE, stderr); return 2; }
        st = mj_replay_load_all(o.roots, o.n_roots, &b, skipped, NULL, &set);
        if (st != MJ_OK) { fprintf(stderr, "refused: could not scan the run folders (%s)\n", mj_status_name(st)); return 2; }
        if (strcmp(cmd, "replays-json") == 0) {
            mj_jout out;
            mj_jout_init(&out, (size_t)1 << 31);
            mj_replay_dump(&out, set.runs, set.n);
            if (out.failed) { fprintf(stderr, "refused: the dump is too large\n"); mj_replay_set_free(&set); return 2; }
            fwrite(out.p, 1, out.n, stdout);
            fputc('\n', stdout);
            mj_jout_free(&out);
        } else {
            for (i = 0; i < set.n; ++i) total += set.runs[i].n_steps;
            printf("%u runs, %u steps\n", (unsigned)set.n, (unsigned)total);
            for (i = 0; i < set.n; ++i) {
                const mj_replay_run *r = &set.runs[i];
                uint32_t k, c[3] = {0, 0, 0};
                for (k = 0; k < r->n_steps; ++k) c[r->steps[k].autolab]++;
                printf("  %-40s steps=%3u score=%5u bp {'good': %u, 'productive': %u, 'not-yet': %u}\n", r->id,
                       (unsigned)r->n_steps, (unsigned)mj_replay_score_bp(r), (unsigned)c[MJ_AUTO_GOOD],
                       (unsigned)c[MJ_AUTO_PRODUCTIVE], (unsigned)c[MJ_AUTO_NOT_YET]);
            }
        }
        mj_replay_set_free(&set);
        return 0;
    }

    if (o.data == NULL) { fputs(USAGE, stderr); return 2; }

    if (strcmp(cmd, "grade") == 0) {
        char lp[MJ_PATH_MAX], msg[96];
        uint64_t seq = 0;
        if (o.replay == NULL || o.label == NULL || !o.have_step) { fputs(USAGE, stderr); return 2; }
        if (mj_fs_mkdir(o.data) != MJ_OK || !join(lp, o.data, "grades.mjl")) { fprintf(stderr, "refused: bad data folder\n"); return 2; }
        st = mj_grade_add(lp, &b, o.replay, (int64_t)o.step, o.label, o.grader, o.autolab, &seq, msg);
        if (st != MJ_OK) { fprintf(stderr, "refused: %s\n", msg[0] ? msg : mj_status_name(st)); return 2; }
        printf("grade recorded at ledger line %llu\n", (unsigned long long)seq);
        return 0;
    }

    if (strcmp(cmd, "grades") == 0) {
        char lp[MJ_PATH_MAX];
        mj_grade_map m;
        uint32_t i;
        if (!join(lp, o.data, "grades.mjl")) return 2;
        st = mj_grades_latest(lp, &b, &m);
        if (st != MJ_OK) { fprintf(stderr, "refused: the grades ledger does not verify (%s)\n", mj_status_name(st)); return 2; }
        for (i = 0; i < m.n; ++i)
            printf("%s %lld %s %s\n", m.g[i].replay_id, (long long)m.g[i].index, m.g[i].label, m.g[i].grader);
        mj_grades_free(&m);
        return 0;
    }

    if (strcmp(cmd, "toy-seed") == 0) {
        mj_store s;
        mj_toy_model z;
        uint8_t blob[MJ_TOY_BLOB_LEN];
        char head[MJ_ID_MAX + 1];
        if (!open_store(&s, o.data, &b)) { fprintf(stderr, "refused: the toy store does not open\n"); return 2; }
        if (mj_store_head(&s, head) == MJ_OK) { fprintf(stderr, "refused: the toy store already has a live version\n"); return 2; }
        memset(&z, 0, sizeof z);
        st = mj_toy_save(&z, blob);
        if (st == MJ_OK) st = mj_store_put(&s, "toy-seed", MJ_KIND_MILESTONE, "-", "zero toy policy", blob, sizeof blob, NULL);
        if (st == MJ_OK) st = mj_store_set_head(&s, "toy-seed");
        if (st != MJ_OK) { fprintf(stderr, "refused: %s\n", mj_status_name(st)); return 2; }
        printf("toy store seeded\n");
        return 0;
    }

    if (strcmp(cmd, "toy-nap") == 0) {
        mj_replay_set set;
        mj_store s;
        mj_grade_map gm;
        mj_tt_cfg cfg;
        mj_tt_result r;
        const mj_replay_run **learn = NULL, **hold = NULL;
        uint32_t nl = 0, nh = 0;
        char lp[MJ_PATH_MAX], gp[MJ_PATH_MAX], msg[160];
        if (o.n_roots == 0) { fputs(USAGE, stderr); return 2; }
        st = mj_replay_load_all(o.roots, o.n_roots, &b, skipped, NULL, &set);
        if (st != MJ_OK) { fprintf(stderr, "refused: could not scan the run folders (%s)\n", mj_status_name(st)); return 2; }
        if (mj_tt_split(set.runs, set.n, 4, &learn, &nl, &hold, &nh) != MJ_OK) return 2;
        if (!open_store(&s, o.data, &b)) { fprintf(stderr, "refused: the toy store does not open\n"); return 2; }
        if (!join(gp, o.data, "grades.mjl") || !join(lp, o.data, "toy-dreams.mjl")) return 2;
        st = mj_grades_latest(gp, &b, &gm);
        if (st != MJ_OK) { fprintf(stderr, "refused: the grades ledger does not verify (%s)\n", mj_status_name(st)); return 2; }
        mj_tt_default_cfg(&cfg);
        cfg.steps = (uint32_t)o.steps;
        cfg.max_shift_bp = (uint32_t)o.max_shift;
        cfg.seed = (uint64_t)o.seed;
        st = mj_tt_nap(&s, lp, &b, learn, nl, hold, nh, &gm, o.id, o.integrate, &cfg, &r, msg);
        free(learn);
        free(hold);
        mj_grades_free(&gm);
        mj_replay_set_free(&set);
        if (st != MJ_OK) { fprintf(stderr, "refused: %s\n", msg[0] ? msg : mj_status_name(st)); return 2; }
        printf("{\n \"dream\": \"%s\",\n \"outcome\": \"%s\",\n \"gates\": {\n  \"step\": %s,\n  \"shift\": %s,\n"
               "  \"golden\": %s,\n  \"task\": %s\n },\n",
               r.dream, r.outcome, r.gate_step ? "true" : "false", r.gate_shift ? "true" : "false",
               r.gate_golden ? "true" : "false", r.gate_task ? "true" : "false");
        printf(" \"task_before\": %u,\n \"task_after\": %u,\n \"golden_before\": %u,\n \"golden_after\": %u,\n"
               " \"shift_bp\": %u,\n \"max_move_q16\": %lld,\n \"samples\": %u,\n \"jd_graded\": %u,\n \"gold\": %u,\n"
               " \"ledger_seq\": %llu,\n \"seconds\": ",
               r.task_before, r.task_after, r.golden_before, r.golden_after, r.shift_bp, (long long)r.max_move_q16,
               r.samples, r.graded, r.gold, (unsigned long long)r.ledger_seq);
        print_float((double)(long long)(r.seconds * 100.0 + 0.5) / 100.0);
        printf(",\n \"candidate\": \"%s\",\n \"live\": \"%s\"\n}\n", r.candidate, r.live);
        return 0;
    }

    fputs(USAGE, stderr);
    return 2;
}
