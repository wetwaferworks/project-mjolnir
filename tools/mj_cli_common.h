/* Shared argument handling for the Mjolnir M0 tools.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_CLI_COMMON_H
#define MJ_CLI_COMMON_H

#include "mjolnir/mj_dream.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_synth.h"

#include <stdio.h>
#include <string.h>

typedef struct mj_cli {
    const char *store, *ledger, *id, *grader;
    int dry_run, live, seed;
} mj_cli;

static int mj_cli_parse(int argc, char **argv, mj_cli *o, const char *usage) {
    int i;
    memset(o, 0, sizeof *o);
    o->grader = "auto-maze";
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char **slot = NULL;
        if (strcmp(a, "--dry-run") == 0) { o->dry_run = 1; continue; }
        if (strcmp(a, "--live") == 0) { o->live = 1; continue; }
        if (strcmp(a, "--seed-toy") == 0) { o->seed = 1; continue; }
        if (strcmp(a, "--store") == 0) slot = &o->store;
        else if (strcmp(a, "--ledger") == 0) slot = &o->ledger;
        else if (strcmp(a, "--id") == 0) slot = &o->id;
        else if (strcmp(a, "--grader") == 0) slot = &o->grader;
        if (slot == NULL || i + 1 >= argc || strlen(argv[i + 1]) >= MJ_PATH_MAX) {
            fprintf(stderr, "%s", usage);
            return 2;
        }
        *slot = argv[++i];
    }
    if (o->live) {
        fprintf(stderr, "refused: stage M0 is dry-run only. Live weight edits are not part of M0.\n");
        return 3;
    }
    if (!o->dry_run || o->store == NULL || o->ledger == NULL || o->id == NULL) {
        fprintf(stderr, "%s", usage);
        return 2;
    }
    return 0;
}

static int mj_cli_run(const mj_cli *o, int nightly) {
    mj_dream_cfg cfg;
    mj_dream_result r;
    mj_retain_report rr;
    mj_budget b;
    mj_store s;
    mj_toy_model live;
    static mj_synth_material mat; /* large: keep it off the stack */
    mj_dream_material dm;
    char head[MJ_ID_MAX + 1];
    uint8_t *blob = NULL;
    size_t blen = 0;
    mj_status st;

    mj_dream_default_cfg(&cfg);
    cfg.store_root = o->store;
    cfg.ledger_path = o->ledger;
    cfg.dream_id = o->id;
    cfg.grader = o->grader;
    cfg.dry_run = 1;
    if (o->seed) {
        st = mj_dream_seed_toy(o->store, &cfg.limits, NULL);
        if (st != MJ_OK && st != MJ_E_EXISTS) {
            fprintf(stderr, "seed failed: %s\n", mj_status_name(st));
            return 1;
        }
    }
    /* build the synthetic dream material from the live toy weights */
    mj_budget_init(&b, cfg.mem_cap);
    st = mj_store_open(&s, o->store, &cfg.limits, &b);
    if (st == MJ_OK) st = mj_store_head(&s, head);
    if (st == MJ_OK) st = mj_store_get(&s, head, &blob, &blen);
    if (st == MJ_OK) {
        st = mj_toy_load(blob, blen, &live);
        mj_bfree(&b, blob, blen ? blen : 1);
    }
    if (st == MJ_OK) st = mj_synth_build(&live, &mat);
    if (st != MJ_OK) {
        fprintf(stderr, "cannot load live toy weights: %s (use --seed-toy on a new store)\n", mj_status_name(st));
        return 1;
    }
    dm.new_runs = mat.new_runs; dm.n_new = mat.n_new;
    dm.old_runs = mat.old_runs; dm.n_old = mat.n_old;
    dm.golden = mat.golden; dm.n_golden = mat.n_golden;
    dm.eval = mat.eval; dm.n_eval = mat.n_eval;

    memset(&rr, 0, sizeof rr);
    st = nightly ? mj_nightly(&cfg, &dm, &r, &rr) : mj_take_a_nap(&cfg, &dm, &r);
    if (st != MJ_OK) {
        fprintf(stderr, "dream refused: %s (nothing recorded)\n", mj_status_name(st));
        return 1;
    }
    printf("dream %s (%s, dry run)\n", o->id, nightly ? "nightly" : "nap");
    printf("  live weights  %s (unchanged)\n", r.head);
    printf("  snapshot      %s\n", r.snapshot);
    printf("  candidate     %s\n", r.candidate);
    printf("  gates         %s\n", r.rep.gates);
    printf("  outcome       %s\n", r.outcome);
    if (r.rep.why[0]) printf("  note          %s\n", r.rep.why);
    if (r.adapted) printf("  adapted       yes, from the last dream's failure note\n");
    printf("  ledger entry  %llu\n", (unsigned long long)r.ledger_seq);
    if (nightly)
        printf("  retention     %u kept, %u retired, %u blobs removed\n", rr.kept, rr.retired, rr.blobs_removed);
    return 0;
}

#endif
