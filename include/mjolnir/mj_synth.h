/* Mjolnir M0: a small synthetic world for the toy model, shared by the tests
 * and the dry-run tools. Nothing here is real data.
 *
 * Old skill (task A): four states, each lighting one of features 0..3, and the
 * matching action clears it. The genesis weights already know task A.
 * New skill (task B): three states lighting features 4..6 (plus a weaker copy
 * of feature 0, 1 or 2), cleared by actions 2, 3 and 1. The genesis weights do not know task B yet.
 * Feature 7 is always on (a bias), so careless learning on task B can damage
 * task A. That is the forgetting the gates must catch.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_SYNTH_H
#define MJ_SYNTH_H

#include "mjolnir/mj_toy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MJ_SYNTH_GOLDEN 16u
#define MJ_SYNTH_MAZES 4u

void mj_synth_genesis(mj_toy_model *m);
void mj_synth_golden(mj_toy_golden g[MJ_SYNTH_GOLDEN]);   /* task A, the old skill */
void mj_synth_mazes_a(mj_toy_maze mz[MJ_SYNTH_MAZES]);    /* old-skill corridors */
void mj_synth_mazes_b(mj_toy_maze mz[MJ_SYNTH_MAZES]);    /* corridors that need task B */
void mj_synth_drills_b(mj_toy_maze mz[MJ_SYNTH_MAZES]);   /* task B only, no old states */
/* A demonstration run through maze mz with the correct action at every step:
 * new verified experience (in M1 this comes from real, graded replays). */
void mj_synth_demo_run(const mj_toy_maze *mz, const char *id, mj_toy_run *run);

/* Dream material built from the synthetic world: the live model's own failed
 * runs on the task B drills, demonstration runs on the same drills, the old
 * task A runs for replay mixing, the golden set and the evaluation mazes. */
typedef struct mj_synth_material {
    mj_toy_run new_runs[2u * MJ_SYNTH_MAZES];
    mj_toy_run old_runs[MJ_SYNTH_MAZES];
    mj_toy_golden golden[MJ_SYNTH_GOLDEN];
    mj_toy_maze eval[MJ_SYNTH_MAZES];
    uint32_t n_new, n_old, n_golden, n_eval;
} mj_synth_material;

mj_status mj_synth_build(const mj_toy_model *live, mj_synth_material *m);

#ifdef __cplusplus
}
#endif
#endif
