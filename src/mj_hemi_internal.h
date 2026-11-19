/* Helpers shared by the two hemisphere stages (mj_hemi.c, mj_classroom.c).
 * Not part of the public interface.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_HEMI_INTERNAL_H
#define MJ_HEMI_INTERNAL_H
#include "mjolnir/mj_hemi.h"

/* Save a toy policy as dir/name (MJTOY1) and give its SHA-256. */
mj_status mj_hemi_save_toy(const char *dir, const char *name, const mj_toy_model *m, uint8_t sha[MJ_SHA256_LEN]);
/* Load dir/name back, refusing it unless its SHA-256 is want. */
mj_status mj_hemi_restore_toy(const char *dir, const char *name, const uint8_t want[MJ_SHA256_LEN], mj_budget *b,
                              mj_toy_model *m);
/* One TSV line per step where the watcher and the answer key had something to say. */
mj_status mj_hemi_write_disagreements(const char *path, const mj_hemi_set *const *sets, uint32_t n_sets);
/* The first 12 hex digits of a SHA-256. */
void mj_hemi_hex12(const uint8_t *sha, char out[13]);
/* Holdout scores (basis points) of k toy policies; returns 1 when the GPU scored them. */
uint32_t mj_hemi_holdout_scores(const mj_hemi_toy_world *w, const mj_toy_model *models, uint32_t k, int use_gpu,
                                uint32_t *out, mj_budget *b);
#endif
