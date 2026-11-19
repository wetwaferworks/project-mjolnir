/* Mjolnir: replays from recorded PCHIE runs, read-only.
 *
 * The C version of python/mjolnir/replay.py. A PCHIE research log (format
 * pchie.research.v1) is a hash-chained JSON-lines file: a start record, then
 * for each decision an "advise", a "decided" and a "transition" record, then
 * an end record. Each line's SHA-256 covers the exact bytes before its
 * ',"sha256"' and names the line before it in "prev".
 *
 * mj_replay_load checks the chain and turns a run into steps with the
 * automatic partial-credit label (design principle 8):
 *   productive  the step earned a reward (key or goal)
 *   good        the step reached ground the run had not stood on before
 *   not-yet     the step went back to ground already visited (a loop)
 * Nothing is ever written next to the source files.
 *
 * Where Python would stop with an uncaught error (a record that is not an
 * object, a reward that is not a number, a list used as a state), the C
 * version refuses the run with a message instead. The JSON rules are those of
 * mj_json.h. The folder scan does not follow symbolic links or junctions.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_REPLAY_H
#define MJ_REPLAY_H
#include "mjolnir/mj_common.h"
#include "mjolnir/mj_json.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MJ_REPLAY_FORMAT "pchie.research.v1"
#define MJ_REPLAY_MAX_FILE (64u * 1024u * 1024u)
#define MJ_REPLAY_MAX_STEPS 4096u
#define MJ_REPLAY_MAX_RUNS 4096u    /* run logs looked at in one scan */
#define MJ_REPLAY_MAX_DEPTH 32u     /* folder depth of the scan */
#define MJ_REPLAY_ERR_MAX 160u

typedef enum mj_auto_label { MJ_AUTO_GOOD = 0, MJ_AUTO_PRODUCTIVE = 1, MJ_AUTO_NOT_YET = 2 } mj_auto_label;
const char *mj_auto_label_name(mj_auto_label a); /* "good", "productive", "not-yet" */

typedef struct mj_replay_prop {
    uint32_t token; /* the token as first written in the log (index in the run's document) */
    double score;   /* its best score / 2^20 */
} mj_replay_prop;

/* Values copied from the log keep their JSON type; MJ_JSON_NONE means the key
 * was missing (Python's None). */
typedef struct mj_replay_step {
    uint32_t index;
    uint32_t decision, task, x, y, key, open, token, action, state, next, ok, record;
    double reward;
    uint32_t prop_first, prop_n; /* into mj_replay_run.props */
    mj_auto_label autolab;
    int revisit;
    double margin;
} mj_replay_step;

typedef struct mj_replay_run {
    char id[MJ_ID_MAX + 1];
    char source[MJ_PATH_MAX];  /* file name only */
    char source_sha256[65];
    uint32_t flags;            /* start record's "flags", or MJ_JSON_NONE for "" */
    int ended;
    mj_json doc;               /* holds every copied value */
    mj_replay_step *steps;
    uint32_t n_steps, cap_steps;
    mj_replay_prop *props;
    uint32_t n_props, cap_props;
    mj_budget *b;
} mj_replay_run;

/* Read one run log (read-only). On MJ_E_INVAL or MJ_E_CHAIN or MJ_E_LIMIT, err
 * holds the reason in Python's words. The run must be freed with
 * mj_replay_free on success. */
mj_status mj_replay_load(const char *path, mj_budget *b, mj_replay_run *run, char err[MJ_REPLAY_ERR_MAX]);
void mj_replay_free(mj_replay_run *run);

/* Good and productive steps over all steps, in basis points. */
uint32_t mj_replay_score_bp(const mj_replay_run *run);

/* Every pchie.research.v1 log under the given folders, sorted by path as
 * Python's sorted(glob(root/ ** / *.jsonl)) gives them. *paths is one malloc'd
 * block of NUL-separated paths; free it with free(). */
mj_status mj_replay_find(const char *const *roots, uint32_t n_roots, char **paths, uint32_t *n_paths);

/* Load every run under the folders, skipping the ones that fail (on_error is
 * told about each) and the second copy of a run seen twice. */
typedef void (*mj_replay_error_fn)(const char *path, const char *why, void *user);
typedef struct mj_replay_set {
    mj_replay_run *runs;
    uint32_t n;
} mj_replay_set;
mj_status mj_replay_load_all(const char *const *roots, uint32_t n_roots, mj_budget *b, mj_replay_error_fn on_error,
                             void *user, mj_replay_set *out);
void mj_replay_set_free(mj_replay_set *s);

/* Python's safe_id: unknown characters (by code point) become '_', leading
 * '.' and '-' go, empty becomes "x", then at most limit characters. */
void mj_safe_id(const char *s, size_t limit, char *out, size_t cap);

/* The runs as Python's json.dumps(runs, sort_keys=True, separators=(",", ":")). */
void mj_replay_dump(mj_jout *o, const mj_replay_run *runs, uint32_t n);

#ifdef __cplusplus
}
#endif
#endif
