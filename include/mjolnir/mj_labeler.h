/* Mjolnir: the labeling page, served on this machine only (127.0.0.1).
 *
 * The C version of python/mjolnir/labeler.py. One step at a time: where the
 * agent stood, what was open, what it proposed, what it chose and what
 * happened, next to the automatic label. The least certain steps come first.
 * The page (the same HTML as the Python version) is made for voice and for
 * hands that should not type much: Enter agrees with the suggestion, 1/G good,
 * 2/B bad, 3/P productive, Space/S skip, Backspace back. Every button is large
 * and named, so Windows Voice Access can press it.
 *
 * Grades go into their own hash-chained ledger (mj_grades.h). Run logs are
 * only read. The server answers one request at a time on one thread, so
 * nothing needs a lock. It only answers requests whose Host is
 * 127.0.0.1:PORT or localhost:PORT, and the API only answers requests that
 * carry the X-Mjolnir: 1 marker (a page on another site cannot set it).
 *
 * The session logic and the HTTP handling are plain functions over bytes
 * (mj_lab_http), so they are tested and fuzzed without a socket; mj_lab_serve
 * adds the listening socket. Beyond the Python version, a connection that
 * stalls is dropped after a few seconds instead of holding the page up, and
 * on Windows the port is bound exclusively.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_LABELER_H
#define MJ_LABELER_H
#include "mjolnir/mj_common.h"
#include "mjolnir/mj_grades.h"
#include "mjolnir/mj_json.h"
#include "mjolnir/mj_replay.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MJ_LAB_MAX_HEAD 16384u /* request line and headers */
#define MJ_LAB_MAX_BODY 4096u

typedef struct mj_lab_key {
    uint32_t run;  /* index in the replay set */
    int64_t step;  /* may be past the run's end for a skip, as in Python */
} mj_lab_key;

typedef struct mj_lab {
    const mj_replay_set *set;
    char grades_path[MJ_PATH_MAX];
    char grader[MJ_ID_MAX + 1];
    uint16_t port;
    mj_budget *b;
    mj_grade_map done;
    mj_lab_key *order;     /* every step, least certain first */
    uint32_t n_order;
    uint32_t *first;       /* first[r]: position of run r's step 0 in the flags below */
    uint8_t *flag;         /* per step: 1 graded, 2 skipped */
    mj_lab_key *skipped;   /* this session's skips, as Python's set */
    uint32_t n_skipped, cap_skipped;
    mj_lab_key *history;
    uint32_t n_history, cap_history;
} mj_lab;

/* A session over the runs (which must outlive it), reading the grades so far. */
mj_status mj_lab_open(mj_lab *s, const mj_replay_set *set, const char *grades_path, const char *grader, uint16_t port,
                      mj_budget *b);
void mj_lab_close(mj_lab *s);

/* The next step without a grade and not skipped; 0 when every step is done. */
int mj_lab_next(const mj_lab *s, mj_lab_key *out);
/* The view of a step (or the "finished" view when key is NULL), as the JSON
 * the Python page receives. MJ_E_INVAL when the step is past its run's end. */
mj_status mj_lab_view(const mj_lab *s, const mj_lab_key *key, mj_jout *o);
mj_status mj_lab_grade(mj_lab *s, uint32_t run, int64_t step, const char *label, char msg[96]);
mj_status mj_lab_skip(mj_lab *s, uint32_t run, int64_t step);
int mj_lab_back(mj_lab *s, mj_lab_key *out); /* 0 when there is nothing to go back to */
void mj_lab_stats_json(const mj_lab *s, mj_jout *o);

/* HTTP: is buf a whole request yet? 1 yes (*total its length), 0 not yet,
 * -1 never (too large or malformed; answer with mj_lab_http anyway). */
int mj_lab_http_complete(const char *buf, size_t n, size_t *total);
/* Answer one whole request with a whole response (status line, headers,
 * body). Never fails: bad requests get a 4xx answer. */
void mj_lab_http(mj_lab *s, const char *req, size_t n, mj_jout *resp);

/* Write the page itself (kept as lines: one string literal that long is past
 * what ISO C promises compilers handle). */
void mj_lab_page_write(mj_jout *o);

/* Listen on 127.0.0.1:port and answer requests until the process ends.
 * Prints the address; opens it in the default browser on Windows when
 * open_browser is set. */
mj_status mj_lab_serve(mj_lab *s, int open_browser);

#ifdef __cplusplus
}
#endif
#endif
