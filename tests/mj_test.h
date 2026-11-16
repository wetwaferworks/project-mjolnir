/* Minimal test bench: counts checks, prints failures, exits non-zero on any.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_TEST_H
#define MJ_TEST_H

#include "mjolnir/mj_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned mj_t_pass, mj_t_fail;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (cond) ++mj_t_pass;                                                         \
        else { ++mj_t_fail; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_ST(expr, want)                                                           \
    do {                                                                               \
        mj_status got_ = (expr);                                                       \
        if (got_ == (want)) ++mj_t_pass;                                               \
        else { ++mj_t_fail; fprintf(stderr, "FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, \
                                    mj_status_name(got_), mj_status_name(want)); }      \
    } while (0)

static int mj_t_done(const char *name) {
    printf("%s: %u passed, %u failed\n", name, mj_t_pass, mj_t_fail);
    return mj_t_fail ? 1 : 0;
}

/* A fresh scratch directory under the build tree for one test. */
static inline void mj_t_dir(char *out, size_t cap, const char *name) {
    const char *base = getenv("MJ_TEST_TMP");
    snprintf(out, cap, "%s/%s", base ? base : ".", name);
}

/* Empty a store directory left by an earlier run (files only, one level of
 * the known sub-directories), so each test starts clean. */
#include "mjolnir/mj_fs.h"
typedef struct mj_t_rm { const char *dir; } mj_t_rm;
static inline int mj_t_rm_cb(const char *name, void *u) {
    char p[MJ_PATH_MAX];
    if (mj_path_join(p, sizeof p, ((mj_t_rm *)u)->dir, name) == MJ_OK) (void)mj_fs_remove(p);
    return 0;
}
static inline void mj_t_clean(const char *root) {
    static const char *subs[] = {"objects", "versions", "retired", "pins", NULL};
    char d[MJ_PATH_MAX];
    mj_t_rm u;
    int i;
    for (i = 0; subs[i]; ++i) {
        if (mj_path_join(d, sizeof d, root, subs[i]) != MJ_OK) continue;
        u.dir = d;
        (void)mj_fs_list(d, 100000, mj_t_rm_cb, &u);
    }
    u.dir = root;
    (void)mj_fs_list(root, 100000, mj_t_rm_cb, &u);
}

#endif
