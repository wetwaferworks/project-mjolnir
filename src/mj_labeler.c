/* Mjolnir: the labeling session and its HTTP answers (see mj_labeler.h).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#if !defined(_WIN32)
#define _XOPEN_SOURCE 700 /* gmtime_r, sockets */
#endif
#include "mjolnir/mj_labeler.h"
#include "mjolnir/mj_delta.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- Python's str() and repr() of a JSON value, for the "chosen" text ---- */

static int printable(uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F) return 0;
    if (cp < 0x7F) return 1;
    if (cp < 0xA1 || cp == 0xAD) return 0;                      /* C1 controls, no-break space, soft hyphen */
    if ((cp >= 0x2000 && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202F) || (cp >= 0x205F && cp <= 0x206F)) return 0;
    if (cp == 0x3000 || cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB)) return 0;
    if (cp >= 0xD800 && cp <= 0xF8FF) return 0;                /* surrogates and private use */
    if (cp >= 0xF0000) return 0;                                /* private use planes */
    return 1;
}

static void repr_str(mj_jout *o, const char *s, size_t n) {
    const unsigned char *u = (const unsigned char *)s;
    int has_sq = memchr(s, '\'', n) != NULL, has_dq = memchr(s, '"', n) != NULL;
    char q = (has_sq && !has_dq) ? '"' : '\'';
    size_t i = 0;
    char b[16];
    mj_jout_raw(o, &q, 1);
    while (i < n) {
        uint32_t cp;
        size_t l = mj_utf8_len(u + i, n - i, &cp);
        if (l == 0) { cp = u[i]; l = 1; }
        if (cp == '\\') mj_jout_raw(o, "\\\\", 2);
        else if (cp == (uint32_t)q) { mj_jout_raw(o, "\\", 1); mj_jout_raw(o, &q, 1); }
        else if (cp == '\t') mj_jout_raw(o, "\\t", 2);
        else if (cp == '\n') mj_jout_raw(o, "\\n", 2);
        else if (cp == '\r') mj_jout_raw(o, "\\r", 2);
        else if (printable(cp)) mj_jout_raw(o, s + i, l);
        else {
            if (cp < 0x100) snprintf(b, sizeof b, "\\x%02x", (unsigned)cp);
            else if (cp < 0x10000) snprintf(b, sizeof b, "\\u%04x", (unsigned)cp);
            else snprintf(b, sizeof b, "\\U%08x", (unsigned)cp);
            mj_jout_cstr(o, b);
        }
        i += l;
    }
    mj_jout_raw(o, &q, 1);
}

static void py_float(mj_jout *o, double f) {
    char b[32];
    size_t k;
    if (isinf(f)) { mj_jout_cstr(o, f > 0 ? "inf" : "-inf"); return; }
    if (isnan(f)) { mj_jout_cstr(o, "nan"); return; }
    k = mj_float_repr(f, b);
    mj_jout_raw(o, b, k);
}

static void py_repr(mj_jout *o, const mj_json *d, uint32_t v);

static void py_str(mj_jout *o, const mj_json *d, uint32_t v) {
    if (v != MJ_JSON_NONE && d->v[v].kind == MJ_J_STR) mj_jout_raw(o, d->t + d->v[v].s_off, d->v[v].s_len);
    else py_repr(o, d, v);
}

static void py_repr(mj_jout *o, const mj_json *d, uint32_t v) {
    const mj_jnode *x;
    uint32_t c;
    int first = 1;
    if (v == MJ_JSON_NONE) { mj_jout_cstr(o, "None"); return; }
    x = &d->v[v];
    switch (x->kind) {
    case MJ_J_NULL: mj_jout_cstr(o, "None"); break;
    case MJ_J_TRUE: mj_jout_cstr(o, "True"); break;
    case MJ_J_FALSE: mj_jout_cstr(o, "False"); break;
    case MJ_J_INT: mj_jout_int(o, x->i); break;
    case MJ_J_BIGINT: mj_jout_raw(o, d->t + x->s_off, x->s_len); break;
    case MJ_J_FLOAT: py_float(o, x->f); break;
    case MJ_J_STR: repr_str(o, d->t + x->s_off, x->s_len); break;
    case MJ_J_ARR:
        mj_jout_raw(o, "[", 1);
        for (c = x->first; c != MJ_JSON_NONE; c = d->v[c].next) {
            if (!first) mj_jout_raw(o, ", ", 2);
            first = 0;
            py_repr(o, d, c);
        }
        mj_jout_raw(o, "]", 1);
        break;
    case MJ_J_OBJ:
        /* a dict keeps the place of a key's first use and the value of its last */
        mj_jout_raw(o, "{", 1);
        for (c = x->first; c != MJ_JSON_NONE; c = d->v[c].next) {
            uint32_t e, last = c;
            int seen = 0;
            for (e = x->first; e != c; e = d->v[e].next)
                if (d->v[e].key_len == d->v[c].key_len && memcmp(d->t + d->v[e].key_off, d->t + d->v[c].key_off, d->v[c].key_len) == 0) seen = 1;
            if (seen) continue;
            for (e = d->v[c].next; e != MJ_JSON_NONE; e = d->v[e].next)
                if (d->v[e].key_len == d->v[c].key_len && memcmp(d->t + d->v[e].key_off, d->t + d->v[c].key_off, d->v[c].key_len) == 0) last = e;
            if (!first) mj_jout_raw(o, ", ", 2);
            first = 0;
            repr_str(o, d->t + d->v[c].key_off, d->v[c].key_len);
            mj_jout_raw(o, ": ", 2);
            py_repr(o, d, last);
        }
        mj_jout_raw(o, "}", 1);
        break;
    default: break;
    }
}

/* ACTION_NAMES.get(v): the name of action 0..5 (1.0 and true count), or NULL. */
static const char *action_name(const mj_json *d, uint32_t v) {
    static const char *names[6] = {"move 0", "move 1", "move 2", "move 3", "action 4", "action 5"};
    double f;
    if (!mj_json_is_number(d, v) || d->v[v].kind == MJ_J_BIGINT) return NULL;
    f = mj_json_to_double(d, v);
    if (f >= 0 && f <= 5 && f == floor(f)) return names[(int)f];
    return NULL;
}

/* Python's round(x, nd) for a float: correctly rounded to nd decimals. */
static double py_round(double x, int nd) {
    char b[400];
    if (!isfinite(x) || fabs(x) > 1e300) return x;
    snprintf(b, sizeof b, "%.*f", nd, x);
    return strtod(b, NULL);
}

/* ---- the session ---- */

static const mj_replay_step *step_of(const mj_lab *s, uint32_t run, int64_t step) {
    const mj_replay_run *r = &s->set->runs[run];
    if (step < 0 || step >= (int64_t)r->n_steps) return NULL;
    return &r->steps[step];
}

typedef struct ord_ent {
    double margin;
    int flag;
    const char *rid;
    uint32_t run, step;
} ord_ent;

static int ord_cmp(const void *pa, const void *pb) {
    const ord_ent *a = (const ord_ent *)pa, *b = (const ord_ent *)pb;
    int c;
    if (a->margin < b->margin) return -1;
    if (a->margin > b->margin) return 1;
    if (a->flag != b->flag) return a->flag < b->flag ? -1 : 1;
    c = strcmp(a->rid, b->rid);
    if (c) return c;
    if (a->step != b->step) return a->step < b->step ? -1 : 1;
    return a->run < b->run ? -1 : (a->run > b->run); /* two runs with one id: keep load order, as a stable sort */
}

static int find_run(const mj_lab *s, const char *rid, size_t n, uint32_t *out) {
    uint32_t i;
    /* Python keeps a dict by id: a later run with the same id replaces an earlier one */
    for (i = s->set->n; i-- > 0;)
        if (strlen(s->set->runs[i].id) == n && memcmp(s->set->runs[i].id, rid, n) == 0) { *out = i; return 1; }
    return 0;
}

static void refresh_flags(mj_lab *s) {
    uint32_t r, k;
    for (r = 0; r < s->set->n; ++r) {
        const mj_replay_run *run = &s->set->runs[r];
        for (k = 0; k < run->n_steps; ++k) {
            uint8_t f = 0;
            if (mj_grades_find(&s->done, run->id, k)) f |= 1;
            s->flag[s->first[r] + k] = f;
        }
    }
    for (k = 0; k < s->n_skipped; ++k)
        if (step_of(s, s->skipped[k].run, s->skipped[k].step))
            s->flag[s->first[s->skipped[k].run] + (uint32_t)s->skipped[k].step] |= 2;
}

mj_status mj_lab_open(mj_lab *s, const mj_replay_set *set, const char *grades_path, const char *grader, uint16_t port,
                      mj_budget *b) {
    uint32_t r, k, total = 0, pos = 0;
    ord_ent *tmp;
    mj_status st;
    memset(s, 0, sizeof *s);
    if (strlen(grades_path) >= sizeof s->grades_path || !mj_id_valid(grader)) return MJ_E_INVAL;
    s->set = set;
    s->b = b;
    s->port = port;
    memcpy(s->grades_path, grades_path, strlen(grades_path) + 1);
    memcpy(s->grader, grader, strlen(grader) + 1);
    st = mj_grades_latest(grades_path, b, &s->done);
    if (st != MJ_OK) return st;
    for (r = 0; r < set->n; ++r) total += set->runs[r].n_steps;
    s->first = (uint32_t *)calloc((size_t)set->n + 1u, sizeof *s->first);
    s->flag = (uint8_t *)calloc((size_t)total + 1u, 1);
    s->order = (mj_lab_key *)calloc((size_t)total + 1u, sizeof *s->order);
    tmp = (ord_ent *)calloc((size_t)total + 1u, sizeof *tmp);
    if (s->first == NULL || s->flag == NULL || s->order == NULL || tmp == NULL) {
        free(tmp);
        mj_lab_close(s);
        return MJ_E_LIMIT;
    }
    for (r = 0; r < set->n; ++r) {
        const mj_replay_run *run = &set->runs[r];
        s->first[r] = pos;
        for (k = 0; k < run->n_steps; ++k, ++pos) {
            tmp[pos].margin = py_round(run->steps[k].margin, 6);
            tmp[pos].flag = run->steps[k].autolab != MJ_AUTO_GOOD ? 0 : 1;
            tmp[pos].rid = run->id;
            tmp[pos].run = r;
            tmp[pos].step = k;
        }
    }
    qsort(tmp, total, sizeof *tmp, ord_cmp);
    for (k = 0; k < total; ++k) {
        uint32_t rr = tmp[k].run;
        /* the order names steps by id, so with two runs of one id both entries point at the later run */
        find_run(s, tmp[k].rid, strlen(tmp[k].rid), &rr);
        s->order[k].run = rr;
        s->order[k].step = tmp[k].step;
    }
    s->n_order = total;
    free(tmp);
    refresh_flags(s);
    return MJ_OK;
}

void mj_lab_close(mj_lab *s) {
    mj_grades_free(&s->done);
    free(s->first);
    free(s->flag);
    free(s->order);
    free(s->skipped);
    free(s->history);
    memset(s, 0, sizeof *s);
}

static uint8_t flag_of(const mj_lab *s, uint32_t run, int64_t step) {
    if (!step_of(s, run, step)) return 0;
    return s->flag[s->first[run] + (uint32_t)step];
}

int mj_lab_next(const mj_lab *s, mj_lab_key *out) {
    uint32_t k;
    for (k = 0; k < s->n_order; ++k)
        if (flag_of(s, s->order[k].run, s->order[k].step) == 0) {
            *out = s->order[k];
            return 1;
        }
    return 0;
}

void mj_lab_stats_json(const mj_lab *s, mj_jout *o) {
    uint32_t i, mine = 0;
    for (i = 0; i < s->done.n; ++i) mine += strcmp(s->done.g[i].grader, s->grader) == 0;
    mj_jout_cstr(o, "{\"total\":");
    mj_jout_int(o, s->n_order);
    mj_jout_cstr(o, ",\"graded\":");
    mj_jout_int(o, s->done.n);
    mj_jout_cstr(o, ",\"by_you\":");
    mj_jout_int(o, mine);
    mj_jout_cstr(o, ",\"skipped\":");
    mj_jout_int(o, s->n_skipped);
    mj_jout_cstr(o, "}");
}

static int int_xy(const mj_json *d, uint32_t v, int64_t *out) {
    if (v == MJ_JSON_NONE) return 0;
    switch (d->v[v].kind) {
    case MJ_J_INT: *out = d->v[v].i; return 1;
    case MJ_J_TRUE: *out = 1; return 1;
    case MJ_J_FALSE: *out = 0; return 1;
    default: return 0; /* floats, text and (unlike Python) integers past int64 are not drawn */
    }
}

/* _map: the path so far, @ here, o visited, * where it went. */
static void draw_map(mj_jout *o, const mj_replay_run *run, uint32_t idx) {
    int64_t x0 = 0, x1 = 0, y0 = 0, y1 = 0, x, y, hx, hy;
    uint32_t i, n = 0;
    int have_next = 0, nx_ok = 0;
    int64_t nx = 0, ny = 0;
    char grid[25][41];
    char line[100];
    const mj_json *d = &run->doc;
    if (idx + 1 < run->n_steps) {
        have_next = 1;
        nx_ok = int_xy(d, run->steps[idx + 1].x, &nx) && int_xy(d, run->steps[idx + 1].y, &ny);
    }
    for (i = 0; i <= idx + (uint32_t)(have_next ? 1 : 0); ++i) {
        if (i == idx + 1) {
            if (!nx_ok) break;
            x = nx;
            y = ny;
        } else if (!int_xy(d, run->steps[i].x, &x) || !int_xy(d, run->steps[i].y, &y)) {
            continue;
        }
        if (n == 0) { x0 = x1 = x; y0 = y1 = y; }
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
        ++n;
    }
    if (n == 0) { mj_jout_string(o, "", 0); return; }
    if (x1 - x0 > 40 || y1 - y0 > 24 || x1 - x0 < 0 || y1 - y0 < 0) {
        mj_jout_cstr(o, "\"(map too large to draw)\"");
        return;
    }
    memset(grid, '.', sizeof grid);
    for (i = 0; i < idx; ++i)
        if (int_xy(d, run->steps[i].x, &x) && int_xy(d, run->steps[i].y, &y)) grid[y - y0][x - x0] = 'o';
    if (have_next && nx_ok) grid[ny - y0][nx - x0] = '*';
    if (int_xy(d, run->steps[idx].x, &hx) && int_xy(d, run->steps[idx].y, &hy)) grid[hy - y0][hx - x0] = '@';
    {
        mj_jout m;
        int64_t r, c;
        mj_jout_init(&m, 8192);
        for (r = 0; r <= y1 - y0; ++r) {
            size_t k = 0;
            for (c = 0; c <= x1 - x0; ++c) {
                if (c) line[k++] = ' ';
                line[k++] = grid[r][c];
            }
            if (r) mj_jout_raw(&m, "\n", 1);
            mj_jout_raw(&m, line, k);
        }
        mj_jout_string(o, m.p ? m.p : "", m.n);
        mj_jout_free(&m);
    }
}

mj_status mj_lab_view(const mj_lab *s, const mj_lab_key *key, mj_jout *o) {
    const mj_replay_run *run;
    const mj_replay_step *st;
    const mj_json *d;
    const mj_grade *g;
    unsigned bits = 0, b;
    uint32_t p, k, first = 1;
    const char *sug, *name;
    if (key == NULL) {
        mj_jout_cstr(o, "{\"finished\":true,\"stats\":");
        mj_lab_stats_json(s, o);
        mj_jout_cstr(o, "}");
        return MJ_OK;
    }
    st = step_of(s, key->run, key->step);
    if (st == NULL) return MJ_E_INVAL;
    run = &s->set->runs[key->run];
    d = &run->doc;
    if (!mj_replay_open_bits(d, st->open, &bits)) return MJ_E_INVAL;
    mj_jout_cstr(o, "{\"finished\":false,\"replay\":");
    mj_jout_string(o, run->id, strlen(run->id));
    mj_jout_cstr(o, ",\"index\":");
    mj_jout_int(o, key->step);
    mj_jout_cstr(o, ",\"count\":");
    mj_jout_int(o, run->n_steps);
    mj_jout_cstr(o, ",\"source\":");
    mj_jout_string(o, run->source, strlen(run->source));
    mj_jout_cstr(o, ",\"x\":");
    mj_jout_value(o, d, st->x);
    mj_jout_cstr(o, ",\"y\":");
    mj_jout_value(o, d, st->y);
    mj_jout_cstr(o, mj_json_truthy(d, st->key) ? ",\"key\":true,\"open\":[" : ",\"key\":false,\"open\":[");
    for (b = 0; b < 4; ++b)
        if ((bits >> b) & 1u) {
            if (!first) mj_jout_raw(o, ",", 1);
            first = 0;
            mj_jout_int(o, b);
        }
    mj_jout_cstr(o, "],\"proposals\":[");
    for (p = 0; p < st->prop_n; ++p) {
        const mj_replay_prop *pr = &run->props[st->prop_first + p];
        uint32_t act = MJ_JSON_NONE;
        int found = 0;
        /* the run's own token-to-action map; a later step wins, as in a dict */
        for (k = 0; k < run->n_steps; ++k)
            if (mj_json_hashable(d, run->steps[k].token) && mj_json_eq(d, run->steps[k].token, d, pr->token)) {
                act = run->steps[k].action;
                found = 1;
            }
        if (p) mj_jout_raw(o, ",", 1);
        mj_jout_raw(o, "[", 1);
        name = found ? action_name(d, act) : NULL;
        if (name) mj_jout_string(o, name, strlen(name));
        else {
            mj_jout m;
            mj_jout_init(&m, 1024);
            mj_jout_cstr(&m, "token ");
            if (d->v[pr->token].kind == MJ_J_TRUE) mj_jout_cstr(&m, "1");
            else if (d->v[pr->token].kind == MJ_J_FALSE) mj_jout_cstr(&m, "0");
            else py_repr(&m, d, pr->token);
            mj_jout_string(o, m.p, m.n);
            mj_jout_free(&m);
        }
        mj_jout_raw(o, ",", 1);
        mj_jout_float(o, py_round(pr->score, 3));
        mj_jout_raw(o, "]", 1);
    }
    mj_jout_cstr(o, "],\"chosen\":");
    name = action_name(d, st->action);
    if (name) mj_jout_string(o, name, strlen(name));
    else {
        mj_jout m;
        mj_jout_init(&m, 1u << 16);
        mj_jout_cstr(&m, "action ");
        py_str(&m, d, st->action);
        mj_jout_string(o, m.p, m.failed ? 0 : m.n);
        mj_jout_free(&m);
    }
    mj_jout_cstr(o, ",\"reward\":");
    mj_jout_float(o, st->reward);
    mj_jout_cstr(o, st->revisit ? ",\"revisit\":true,\"auto\":" : ",\"revisit\":false,\"auto\":");
    mj_jout_string(o, mj_auto_label_name(st->autolab), strlen(mj_auto_label_name(st->autolab)));
    sug = st->autolab == MJ_AUTO_NOT_YET ? "bad" : mj_auto_label_name(st->autolab);
    mj_jout_cstr(o, ",\"suggest\":");
    mj_jout_string(o, sug, strlen(sug));
    mj_jout_cstr(o, ",\"map\":");
    draw_map(o, run, (uint32_t)key->step);
    mj_jout_cstr(o, ",\"graded\":");
    g = mj_grades_find(&s->done, run->id, key->step);
    if (g) mj_jout_string(o, g->label, strlen(g->label));
    else mj_jout_cstr(o, "null");
    mj_jout_cstr(o, ",\"stats\":");
    mj_lab_stats_json(s, o);
    mj_jout_cstr(o, "}");
    return MJ_OK;
}

static mj_status push_key(mj_lab_key **arr, uint32_t *n, uint32_t *cap, uint32_t run, int64_t step) {
    if (*n == *cap) {
        uint32_t c = *cap ? *cap * 2u : 64u;
        mj_lab_key *na;
        if (*n >= (1u << 24)) return MJ_E_LIMIT;
        na = (mj_lab_key *)realloc(*arr, (size_t)c * sizeof *na);
        if (na == NULL) return MJ_E_LIMIT;
        *arr = na;
        *cap = c;
    }
    (*arr)[*n].run = run;
    (*arr)[*n].step = step;
    (*n)++;
    return MJ_OK;
}

mj_status mj_lab_grade(mj_lab *s, uint32_t run, int64_t step, const char *label, char msg[96]) {
    const mj_replay_step *st = step_of(s, run, step);
    mj_status rc;
    mj_grade *g;
    if (st == NULL) { snprintf(msg, 96, "unknown step"); return MJ_E_INVAL; }
    rc = mj_grade_add(s->grades_path, s->b, s->set->runs[run].id, step, label, s->grader,
                      mj_auto_label_name(st->autolab), NULL, msg);
    if (rc != MJ_OK) {
        if (!msg[0]) snprintf(msg, 96, "the grade could not be written (%s)", mj_status_name(rc));
        return rc;
    }
    g = (mj_grade *)mj_grades_find(&s->done, s->set->runs[run].id, step);
    if (g == NULL) {
        if (s->done.n == s->done.cap) {
            uint32_t c = s->done.cap ? s->done.cap * 2u : 64u;
            mj_grade *ng = (mj_grade *)realloc(s->done.g, (size_t)c * sizeof *ng);
            if (ng == NULL) return MJ_E_LIMIT;
            s->done.g = ng;
            s->done.cap = c;
        }
        g = &s->done.g[s->done.n++];
        memset(g, 0, sizeof *g);
        snprintf(g->replay_id, sizeof g->replay_id, "%s", s->set->runs[run].id);
        g->index = step;
    }
    snprintf(g->label, sizeof g->label, "%s", label);
    snprintf(g->grader, sizeof g->grader, "%s", s->grader);
    s->flag[s->first[run] + (uint32_t)step] |= 1;
    return push_key(&s->history, &s->n_history, &s->cap_history, run, step);
}

mj_status mj_lab_skip(mj_lab *s, uint32_t run, int64_t step) {
    uint32_t i;
    mj_status st;
    int have = 0;
    for (i = 0; i < s->n_skipped; ++i)
        if (s->skipped[i].run == run && s->skipped[i].step == step) have = 1;
    if (!have) {
        st = push_key(&s->skipped, &s->n_skipped, &s->cap_skipped, run, step);
        if (st != MJ_OK) return st;
    }
    if (step_of(s, run, step)) s->flag[s->first[run] + (uint32_t)step] |= 2;
    return push_key(&s->history, &s->n_history, &s->cap_history, run, step);
}

int mj_lab_back(mj_lab *s, mj_lab_key *out) {
    uint32_t i;
    if (s->n_history == 0) return 0;
    *out = s->history[--s->n_history];
    for (i = 0; i < s->n_skipped; ++i)
        if (s->skipped[i].run == out->run && s->skipped[i].step == out->step) {
            s->skipped[i] = s->skipped[--s->n_skipped];
            if (step_of(s, out->run, out->step)) s->flag[s->first[out->run] + (uint32_t)out->step] &= (uint8_t)0xFDu;
            break;
        }
    return 1;
}

/* ---- HTTP ---- */

static const char *find_head_end(const char *buf, size_t n, size_t *body_at) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (buf[i] != '\n') continue;
        if (i + 1 < n && buf[i + 1] == '\n') { *body_at = i + 2; return buf + i; }
        if (i + 2 < n && buf[i + 1] == '\r' && buf[i + 2] == '\n') { *body_at = i + 3; return buf + i; }
    }
    return NULL;
}

typedef struct http_req {
    char method[16];
    char path[2048];
    const char *head;
    size_t head_len;
    const char *body;
    size_t body_len;
} http_req;

/* The first header called name (any case), left-trimmed of spaces and tabs
 * and without its line end, as Python's headers.get. */
static int header(const http_req *r, const char *name, char *out, size_t cap) {
    size_t i = 0, nl = strlen(name);
    /* skip the request line */
    while (i < r->head_len && r->head[i] != '\n') ++i;
    ++i;
    while (i < r->head_len) {
        size_t e = i, k;
        while (e < r->head_len && r->head[e] != '\n') ++e;
        if (e - i > nl && r->head[i + nl] == ':') {
            int same = 1;
            for (k = 0; k < nl; ++k) {
                char a = r->head[i + k], b = name[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { same = 0; break; }
            }
            if (same) {
                size_t v = i + nl + 1, ve = e;
                while (v < ve && (r->head[v] == ' ' || r->head[v] == '\t')) ++v;
                while (ve > v && (r->head[ve - 1] == '\r' || r->head[ve - 1] == '\n')) --ve;
                if (ve - v >= cap) return 0;
                memcpy(out, r->head + v, ve - v);
                out[ve - v] = '\0';
                return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

static int py_int_text(const char *s, int64_t *out) {
    size_t a = 0, e = strlen(s);
    int neg = 0, any = 0, us = 0;
    uint64_t v = 0;
    while (a < e && (s[a] == ' ' || (s[a] >= '\t' && s[a] <= '\r'))) ++a;
    while (e > a && (s[e - 1] == ' ' || (s[e - 1] >= '\t' && s[e - 1] <= '\r'))) --e;
    if (a < e && (s[a] == '+' || s[a] == '-')) neg = s[a++] == '-';
    for (; a < e; ++a) {
        if (s[a] == '_') {
            if (!any || us) return 0;
            us = 1;
            continue;
        }
        if (s[a] < '0' || s[a] > '9') return 0;
        if (v > 100000000000000000ull) return 0;
        v = v * 10u + (uint64_t)(s[a] - '0');
        any = 1;
        us = 0;
    }
    if (!any || us) return 0;
    *out = neg ? -(int64_t)v : (int64_t)v;
    return 1;
}

int mj_lab_http_complete(const char *buf, size_t n, size_t *total) {
    size_t body_at;
    http_req r;
    char cl[64];
    int64_t len = 0;
    if (find_head_end(buf, n, &body_at) == NULL) return n > MJ_LAB_MAX_HEAD ? -1 : 0;
    if (body_at > MJ_LAB_MAX_HEAD) return -1;
    memset(&r, 0, sizeof r);
    r.head = buf;
    r.head_len = body_at;
    if (n >= 5 && memcmp(buf, "POST ", 5) == 0 && header(&r, "Content-Length", cl, sizeof cl)) {
        if (!py_int_text(cl, &len) || len < 0 || len > (int64_t)MJ_LAB_MAX_BODY) { *total = body_at; return -1; }
    }
    if (n < body_at + (size_t)len) return 0;
    *total = body_at + (size_t)len;
    return 1;
}

static const char *reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 431: return "Request Header Fields Too Large";
    default: return "Not Implemented";
    }
}

static void respond(mj_jout *resp, int code, const char *ctype, const char *body, size_t n) {
    char h[512];
    char date[64] = "";
    time_t t = time(NULL);
    struct tm g;
    static const char *days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mons[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
#if defined(_WIN32)
    if (gmtime_s(&g, &t) == 0)
#else
    if (gmtime_r(&t, &g) != NULL)
#endif
        snprintf(date, sizeof date, "%s, %02d %s %04d %02d:%02d:%02d GMT", days[g.tm_wday % 7], g.tm_mday,
                 mons[g.tm_mon % 12], g.tm_year + 1900, g.tm_hour, g.tm_min, g.tm_sec);
    snprintf(h, sizeof h,
             "HTTP/1.0 %d %s\r\nServer: Mjolnir\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
             "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
             code, reason(code), date, ctype, (unsigned long)n);
    mj_jout_cstr(resp, h);
    mj_jout_raw(resp, body, n);
}

static void respond_json(mj_jout *resp, int code, mj_jout *body) {
    if (body->failed) {
        static const char e[] = "{\"error\":\"the answer is too large\"}";
        respond(resp, 500, "application/json", e, sizeof e - 1);
        return;
    }
    respond(resp, code, "application/json", body->p ? body->p : "", body->n);
}

static void respond_error(mj_jout *resp, int code, const char *msg, const mj_lab *s) {
    mj_jout b;
    mj_jout_init(&b, 1u << 16);
    mj_jout_cstr(&b, "{\"error\":");
    mj_jout_string(&b, msg, strlen(msg));
    if (s) {
        mj_jout_cstr(&b, ",\"stats\":");
        mj_lab_stats_json(s, &b);
    }
    mj_jout_cstr(&b, "}");
    respond_json(resp, code, &b);
    mj_jout_free(&b);
}

/* urlparse(path).path, after Python's collapsing of leading slashes. */
static void url_path(const char *target, char *out, size_t cap) {
    const char *p = target, *e;
    size_t n, i, semi;
    if ((strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0)) {
        p = strstr(p, "//") + 2;
        while (*p && *p != '/' && *p != '?' && *p != '#') ++p;
    } else if (p[0] == '/' && p[1] == '/') {
        while (p[1] == '/') ++p;
    }
    e = p;
    while (*e && *e != '?' && *e != '#') ++e;
    n = (size_t)(e - p);
    semi = n;
    for (i = n; i-- > 0;) { /* ";params" of the last segment */
        if (p[i] == '/') break;
        if (p[i] == ';') semi = i;
    }
    n = semi;
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static void answer_view(mj_lab *s, mj_jout *resp, const mj_lab_key *key) {
    mj_jout b;
    mj_jout_init(&b, 1u << 22);
    if (mj_lab_view(s, key, &b) != MJ_OK) {
        mj_jout_free(&b);
        respond_error(resp, 400, "unknown step", s);
        return;
    }
    respond_json(resp, 200, &b);
    mj_jout_free(&b);
}

void mj_lab_http(mj_lab *s, const char *req, size_t n, mj_jout *resp) {
    http_req r;
    size_t body_at, i, k;
    char host[300], marker[16], path[2048], allowed1[40], allowed2[40];
    int is_get, is_post, host_ok, marker_ok;
    memset(&r, 0, sizeof r);
    if (find_head_end(req, n, &body_at) == NULL || body_at > MJ_LAB_MAX_HEAD) {
        respond_error(resp, 431, "request too large", NULL);
        return;
    }
    r.head = req;
    r.head_len = body_at;
    /* request line: METHOD SP TARGET SP HTTP/1.x */
    for (i = 0, k = 0; i < body_at && req[i] != ' ' && req[i] != '\r' && req[i] != '\n'; ++i)
        if (k + 1 < sizeof r.method) r.method[k++] = req[i];
        else { respond_error(resp, 400, "bad request line", NULL); return; }
    r.method[k] = '\0';
    if (i >= body_at || req[i] != ' ') { respond_error(resp, 400, "bad request line", NULL); return; }
    ++i;
    for (k = 0; i < body_at && req[i] != ' ' && req[i] != '\r' && req[i] != '\n'; ++i)
        if (k + 1 < sizeof r.path) r.path[k++] = req[i];
        else { respond_error(resp, 400, "bad request line", NULL); return; }
    r.path[k] = '\0';
    if (i + 9 > body_at || req[i] != ' ' || strncmp(req + i + 1, "HTTP/1.", 7) != 0 || req[i + 8] < '0' || req[i + 8] > '9') {
        respond_error(resp, 400, "bad request line", NULL);
        return;
    }
    is_get = strcmp(r.method, "GET") == 0;
    is_post = strcmp(r.method, "POST") == 0;
    if (!is_get && !is_post) { respond_error(resp, 501, "unsupported method", NULL); return; }
    url_path(r.path, path, sizeof path);
    snprintf(allowed1, sizeof allowed1, "127.0.0.1:%u", (unsigned)s->port);
    snprintf(allowed2, sizeof allowed2, "localhost:%u", (unsigned)s->port);
    host_ok = header(&r, "Host", host, sizeof host) && (strcmp(host, allowed1) == 0 || strcmp(host, allowed2) == 0);
    marker_ok = header(&r, "X-Mjolnir", marker, sizeof marker) && strcmp(marker, "1") == 0;

    if (is_get) {
        if (strcmp(path, "/") == 0) {
            mj_jout page;
            if (!host_ok) { respond_error(resp, 403, "this page only answers on this machine", NULL); return; }
            mj_jout_init(&page, 1u << 16);
            mj_lab_page_write(&page);
            respond(resp, 200, "text/html; charset=utf-8", page.p, page.n);
            mj_jout_free(&page);
        } else if (strcmp(path, "/api/next") == 0) {
            mj_lab_key key;
            if (!host_ok) { respond_error(resp, 403, "this page only answers on this machine", NULL); return; }
            if (!marker_ok) { respond_error(resp, 403, "missing request marker", NULL); return; }
            answer_view(s, resp, mj_lab_next(s, &key) ? &key : NULL);
        } else {
            respond_error(resp, 404, "not found", NULL);
        }
        return;
    }

    /* POST */
    if (!host_ok) { respond_error(resp, 403, "this page only answers on this machine", NULL); return; }
    if (!marker_ok) { respond_error(resp, 403, "missing request marker", NULL); return; }
    {
        char cl[64];
        int64_t len = 0;
        mj_json d;
        uint32_t root = MJ_JSON_NONE;
        mj_lab_key key;
        mj_budget jb;
        if (header(&r, "Content-Length", cl, sizeof cl) && !py_int_text(cl, &len)) { respond_error(resp, 400, "bad Content-Length", s); return; }
        if (len < 0 || len > (int64_t)MJ_LAB_MAX_BODY) { respond_error(resp, 400, "request too large", s); return; }
        if ((size_t)len > n - body_at) { respond_error(resp, 400, "request body cut short", s); return; }
        mj_budget_init(&jb, 1u << 20);
        mj_json_init(&d, &jb, 4096, 8192);
        if (len == 0) {
            if (mj_json_parse(&d, "{}", 2, &root) != MJ_OK) root = MJ_JSON_NONE;
        } else if (mj_json_parse(&d, req + body_at, (size_t)len, &root) != MJ_OK) {
            mj_json_free(&d);
            respond_error(resp, 400, "the request is not JSON", s);
            return;
        }
        if (strcmp(path, "/api/grade") == 0 || strcmp(path, "/api/skip") == 0) {
            int grade = path[5] == 'g';
            uint32_t rv = mj_json_get(&d, root, "replay"), iv = mj_json_get(&d, root, "index"), lv = mj_json_get(&d, root, "label");
            size_t rl = 0, ll = 0;
            const char *rid = mj_json_str(&d, rv, &rl), *lab = mj_json_str(&d, lv, &ll);
            uint32_t run;
            int64_t idx;
            double f;
            char label[16], msg[96];
            msg[0] = '\0';
            if (rv == MJ_JSON_NONE || iv == MJ_JSON_NONE || (grade && lv == MJ_JSON_NONE)) {
                mj_json_free(&d);
                respond_error(resp, 400, "missing field", s);
                return;
            }
            /* int(index): whole numbers, floats cut toward zero, or digits as text */
            if (d.v[iv].kind == MJ_J_INT) idx = d.v[iv].i;
            else if (d.v[iv].kind == MJ_J_TRUE || d.v[iv].kind == MJ_J_FALSE) idx = d.v[iv].kind == MJ_J_TRUE;
            else if (d.v[iv].kind == MJ_J_FLOAT && isfinite(f = d.v[iv].f) && fabs(f) < 9e18) idx = (int64_t)f;
            else if (d.v[iv].kind == MJ_J_STR) {
                char tmp[64];
                if (d.v[iv].s_len >= sizeof tmp || memchr(d.t + d.v[iv].s_off, 0, d.v[iv].s_len)) idx = INT64_MIN;
                else {
                    memcpy(tmp, d.t + d.v[iv].s_off, d.v[iv].s_len);
                    tmp[d.v[iv].s_len] = '\0';
                    if (!py_int_text(tmp, &idx)) idx = INT64_MIN;
                }
            } else idx = INT64_MIN;
            if (idx == INT64_MIN) { mj_json_free(&d); respond_error(resp, 400, "bad index", s); return; }
            if (rid == NULL || !find_run(s, rid, rl, &run)) { mj_json_free(&d); respond_error(resp, 400, "unknown step", s); return; }
            if (grade) {
                if (lab == NULL || ll >= sizeof label || memchr(lab, 0, ll)) { mj_json_free(&d); respond_error(resp, 400, "unknown label", s); return; }
                memcpy(label, lab, ll);
                label[ll] = '\0';
                if (!step_of(s, run, idx)) { mj_json_free(&d); respond_error(resp, 400, "unknown step", s); return; }
                if (mj_lab_grade(s, run, idx, label, msg) != MJ_OK) { mj_json_free(&d); respond_error(resp, 400, msg, s); return; }
            } else if (mj_lab_skip(s, run, idx) != MJ_OK) {
                mj_json_free(&d);
                respond_error(resp, 400, "too many skips", s);
                return;
            }
            mj_json_free(&d);
            answer_view(s, resp, mj_lab_next(s, &key) ? &key : NULL);
            return;
        }
        mj_json_free(&d);
        if (strcmp(path, "/api/back") == 0) {
            if (mj_lab_back(s, &key)) answer_view(s, resp, &key);
            else answer_view(s, resp, mj_lab_next(s, &key) ? &key : NULL);
            return;
        }
        respond_error(resp, 404, "not found", NULL);
    }
}
