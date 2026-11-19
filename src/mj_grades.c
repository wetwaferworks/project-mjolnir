/* Mjolnir: grades in their own ledger (see mj_grades.h).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_grades.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"
#include "mjolnir/mj_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mj_grade_label_score(const char *label) {
    if (strcmp(label, "good") == 0 || strcmp(label, "productive") == 0) return 10000;
    if (strcmp(label, "bad") == 0) return 0;
    return -1;
}

double mj_grade_reward(const char *label) {
    if (strcmp(label, "productive") == 0) return 1.5;
    if (strcmp(label, "bad") == 0) return -1.0;
    return 1.0;
}

double mj_grade_auto_reward(const char *autolab) {
    if (strcmp(autolab, "productive") == 0) return 1.5;
    if (strcmp(autolab, "not-yet") == 0) return -1.0;
    return 1.0;
}

static size_t cpy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return n + 1;
}

mj_status mj_grade_add(const char *ledger_path, mj_budget *b, const char *replay_id, int64_t index, const char *label,
                       const char *grader, const char *autolab, uint64_t *seq, char msg[96]) {
    mj_dream_record r;
    char sid[MJ_TEXT_MAX + 48], tmp[MJ_TEXT_MAX + 64], when[21];
    int score = mj_grade_label_score(label);
    mj_status st;
    if (msg) msg[0] = '\0';
    if (score < 0) {
        if (msg) snprintf(msg, 96, "unknown label: '%.60s'", label);
        return MJ_E_INVAL;
    }
    if (!mj_id_valid(grader)) {
        if (msg) snprintf(msg, 96, "bad grader name");
        return MJ_E_INVAL;
    }
    if (strlen(replay_id) > MJ_TEXT_MAX) return MJ_E_LIMIT;
    snprintf(sid, sizeof sid, "%s.s%04lld", replay_id, (long long)index);
    snprintf(tmp, sizeof tmp, "g-%s", sid);
    mj_ledger_record_init(&r);
    mj_safe_id(tmp, MJ_ID_MAX, r.dream_id, sizeof r.dream_id);
    st = mj_utc_now(when);
    if (st != MJ_OK) return st;
    cpy(r.when, sizeof r.when, when);
    cpy(r.trigger, sizeof r.trigger, "label");
    if (!cpy(r.grader, sizeof r.grader, grader)) return MJ_E_INVAL;
    mj_safe_id(replay_id, MJ_ID_MAX, r.inputs, sizeof r.inputs);
    cpy(r.edit, sizeof r.edit, "-");
    r.score_after_bp = (uint64_t)score;
    snprintf(r.gates, sizeof r.gates, "label=%s", label);
    cpy(r.outcome, sizeof r.outcome, "grade");
    snprintf(tmp, sizeof tmp, "step=%lld auto=%s", (long long)index, (autolab && autolab[0]) ? autolab : "-");
    if (!cpy(r.notes, sizeof r.notes, tmp)) return MJ_E_INVAL;
    st = mj_ledger_append(ledger_path, b, &r);
    if (st == MJ_OK && seq) *seq = r.seq;
    return st;
}

/* Python's int() on an ASCII word: optional sign, digits, single underscores
 * between digits. */
static int py_int(const char *s, size_t n, int64_t *out) {
    size_t i = 0;
    int neg = 0, any = 0, last_us = 0;
    uint64_t v = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    for (; i < n; ++i) {
        char c = s[i];
        if (c == '_') {
            if (!any || last_us) return 0;
            last_us = 1;
            continue;
        }
        if (c < '0' || c > '9') return 0;
        if (v > (UINT64_C(9223372036854775808) - (uint64_t)(c - '0')) / 10u) return 0;
        v = v * 10u + (uint64_t)(c - '0');
        any = 1;
        last_us = 0;
    }
    if (!any || last_us) return 0;
    if (!neg && v > (uint64_t)INT64_MAX) return 0;
    *out = neg ? (int64_t)(0 - v) : (int64_t)v;
    return 1;
}

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

const mj_grade *mj_grades_find(const mj_grade_map *m, const char *replay_id, int64_t index) {
    uint32_t i;
    for (i = 0; i < m->n; ++i)
        if (m->g[i].index == index && strcmp(m->g[i].replay_id, replay_id) == 0) return &m->g[i];
    return NULL;
}

void mj_grades_free(mj_grade_map *m) {
    free(m->g);
    memset(m, 0, sizeof *m);
}

mj_status mj_grades_latest(const char *ledger_path, mj_budget *b, mj_grade_map *out) {
    mj_ledger_state lst;
    uint64_t bad = 0;
    uint8_t *buf = NULL;
    size_t n = 0, pos = 0;
    mj_status st;
    memset(out, 0, sizeof *out);
    st = mj_ledger_verify_file(ledger_path, b, &lst, &bad);
    if (st != MJ_OK) return st;
    if (lst.count == 0) return MJ_OK;
    st = mj_fs_read_all(ledger_path, (size_t)1 << 30, b, &buf, &n);
    if (st != MJ_OK) return st;
    while (pos < n) {
        size_t e = pos;
        mj_dream_record r;
        while (e < n && buf[e] != '\n') ++e;
        if (mj_ledger_parse_line((const char *)buf + pos, e - pos, &r) == MJ_OK && strcmp(r.outcome, "grade") == 0 &&
            strncmp(r.gates, "label=", 6) == 0 && mj_grade_label_score(r.gates + 6) >= 0) {
            const char *p = r.notes;
            int64_t idx = 0;
            int have = 0;
            while (*p) { /* the last "step=" word wins, as in Python */
                const char *w;
                while (*p && is_ws(*p)) ++p;
                w = p;
                while (*p && !is_ws(*p)) ++p;
                if (p - w >= 5 && strncmp(w, "step=", 5) == 0) {
                    int64_t v;
                    if (py_int(w + 5, (size_t)(p - w - 5), &v)) { idx = v; have = 1; }
                    else have = 0;
                }
            }
            if (have) {
                mj_grade *g = (mj_grade *)mj_grades_find(out, r.inputs, idx);
                if (g == NULL) {
                    if (out->n == out->cap) {
                        uint32_t cap = out->cap ? out->cap * 2u : 64u;
                        mj_grade *ng;
                        if (out->n >= MJ_GRADES_MAX) { st = MJ_E_LIMIT; break; }
                        ng = (mj_grade *)realloc(out->g, (size_t)cap * sizeof *ng);
                        if (ng == NULL) { st = MJ_E_LIMIT; break; }
                        out->g = ng;
                        out->cap = cap;
                    }
                    g = &out->g[out->n++];
                    memset(g, 0, sizeof *g);
                    cpy(g->replay_id, sizeof g->replay_id, r.inputs);
                    g->index = idx;
                }
                cpy(g->label, sizeof g->label, r.gates + 6);
                cpy(g->grader, sizeof g->grader, r.grader);
            }
        }
        pos = e + 1;
    }
    mj_bfree(b, buf, n ? n : 1);
    if (st != MJ_OK) mj_grades_free(out);
    return st;
}
