/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_dream.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_synth.h"

#include <stdio.h>
#include <string.h>

#define DREAM_ID_MAX (MJ_ID_MAX - 5u)

void mj_dream_default_cfg(mj_dream_cfg *c) {
    memset(c, 0, sizeof *c);
    c->trigger = "nightly";
    c->grader = "auto-maze";
    c->dry_run = 1;
    mj_store_default_limits(&c->limits);
    mj_update_default_cfg(&c->update);
    c->mem_cap = 64u * 1024u * 1024u;
}

static int set_text(char *dst, size_t cap, const char *src) {
    size_t l = strlen(src);
    if (l >= cap) l = cap - 1; /* only used for text we produced ourselves */
    memcpy(dst, src, l);
    dst[l] = '\0';
    return 1;
}

static void join_inputs(const mj_dream_material *m, char *out, size_t cap) {
    uint32_t i;
    size_t p = 0;
    mj_sha256_ctx h;
    uint8_t d[MJ_SHA256_LEN];
    char hex[MJ_SHA256_HEX];
    int fits = m->n_new <= MJ_INPUTS_MAX;
    for (i = 0; i < m->n_new && fits; ++i) {
        size_t l = strlen(m->new_runs[i].id);
        if (p + l + 2 > cap || p + l + 2 > MJ_TEXT_MAX) { fits = 0; break; }
        if (i) out[p++] = ',';
        memcpy(out + p, m->new_runs[i].id, l);
        p += l;
    }
    if (fits) {
        out[p] = '\0';
        return;
    }
    /* too many to list: name the set by the hash of its ids */
    mj_sha256_init(&h);
    for (i = 0; i < m->n_new; ++i) mj_sha256_update(&h, m->new_runs[i].id, strlen(m->new_runs[i].id) + 1);
    mj_sha256_final(&h, d);
    mj_hex_encode(d, MJ_SHA256_LEN, hex);
    snprintf(out, cap, "set-%.16s", hex);
}

static mj_status check_cfg(const mj_dream_cfg *c, const mj_dream_material *m) {
    if (c == NULL || m == NULL || c->store_root == NULL || c->ledger_path == NULL) return MJ_E_INVAL;
    if (!c->dry_run) return MJ_E_FORBIDDEN; /* standing gate: no live weight edits in M0 */
    if (!mj_id_valid(c->dream_id) || strlen(c->dream_id) > DREAM_ID_MAX) return MJ_E_INVAL;
    if (c->trigger == NULL || c->grader == NULL || !mj_id_valid(c->grader)) return MJ_E_INVAL;
    if (strcmp(c->trigger, "nightly") != 0 && strcmp(c->trigger, "nap") != 0 && strcmp(c->trigger, "test") != 0)
        return MJ_E_INVAL;
    if (c->mem_cap < 1024u * 1024u) return MJ_E_INVAL;
    return MJ_OK;
}

mj_status mj_dream_run(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r) {
    mj_budget b;
    mj_store s;
    mj_ledger_state ls;
    mj_dream_record rec;
    mj_toy_model live, cand;
    mj_update_cfg ucfg;
    uint8_t *blob = NULL, cblob[MJ_TOY_BLOB_LEN];
    size_t blen = 0;
    uint64_t bad = 0;
    char head_after[MJ_ID_MAX + 1], label[MJ_TEXT_MAX + 1], prior[MJ_TEXT_MAX + 1] = "";
    mj_status st;

    if (r == NULL) return MJ_E_INVAL;
    memset(r, 0, sizeof *r);
    set_text(r->candidate, sizeof r->candidate, "-");
    st = check_cfg(c, m);
    if (st != MJ_OK) return st;

    mj_budget_init(&b, c->mem_cap);
    st = mj_store_open(&s, c->store_root, &c->limits, &b);
    if (st != MJ_OK) return st;
    st = mj_ledger_verify_file(c->ledger_path, &b, &ls, &bad);
    if (st != MJ_OK) return st == MJ_E_CHAIN ? MJ_E_CHAIN : st;

    st = mj_store_head(&s, r->head);
    if (st != MJ_OK) return st;
    st = mj_store_get(&s, r->head, &blob, &blen);
    if (st != MJ_OK) return st;
    st = mj_toy_load(blob, blen, &live);
    mj_bfree(&b, blob, blen ? blen : 1);
    if (st != MJ_OK) return st;

    /* decision 10: the dreamer learns why the last dream failed */
    ucfg = c->update;
    if (ls.count > 0 && strcmp(ls.last.outcome, "rejected") == 0 &&
        (strstr(ls.last.notes, "shift gate") != NULL || strstr(ls.last.notes, "golden gate") != NULL)) {
        ucfg.lr = ucfg.lr / 2 > 0 ? ucfg.lr / 2 : 1;
        ucfg.step_cap = ucfg.step_cap / 2 > 0 ? ucfg.step_cap / 2 : 1;
        ucfg.replay_old_per_new = ucfg.replay_old_per_new + 1 <= 64 ? ucfg.replay_old_per_new + 1 : 64;
        r->adapted = 1;
        snprintf(prior, sizeof prior, "adapted from dream %s: half steps, more old replay", ls.last.dream_id);
    }

    /* the rollback point: a full copy before anything is dreamed */
    snprintf(r->snapshot, sizeof r->snapshot, "%s-pre", c->dream_id);
    snprintf(label, sizeof label, "before dream %s", c->dream_id);
    {
        uint8_t lb[MJ_TOY_BLOB_LEN];
        st = mj_toy_save(&live, lb);
        if (st == MJ_OK) st = mj_store_put(&s, r->snapshot, MJ_KIND_SNAPSHOT, r->head, label, lb, sizeof lb, NULL);
        if (st != MJ_OK) return st;
    }

    r->verdict = mj_update_dream(&live, m->new_runs, m->n_new, m->old_runs, m->n_old, m->golden, m->n_golden,
                                 m->eval, m->n_eval, &ucfg, &cand, &r->rep);
    if (r->verdict == MJ_E_INVAL || r->verdict == MJ_E_LIMIT) return r->verdict; /* bad material: nothing recorded past the snapshot */

    if (r->verdict == MJ_OK || r->verdict == MJ_E_GATE) {
        snprintf(r->candidate, sizeof r->candidate, "%s-cand", c->dream_id);
        snprintf(label, sizeof label, "candidate from dream %s (%s)", c->dream_id,
                 r->verdict == MJ_OK ? "gates passed, dry run" : "gates failed");
        st = mj_toy_save(&cand, cblob);
        if (st == MJ_OK)
            st = mj_store_put(&s, r->candidate, MJ_KIND_CANDIDATE, r->snapshot, label, cblob, sizeof cblob, NULL);
        if (st != MJ_OK) return st;
    }
    set_text(r->outcome, sizeof r->outcome,
             r->verdict == MJ_OK ? "dry-run-pass" : r->verdict == MJ_E_GATE ? "rejected" : "aborted");

    /* safe-state check: a dry run must leave HEAD exactly where it was */
    st = mj_store_head(&s, head_after);
    if (st != MJ_OK) return st;
    if (strcmp(head_after, r->head) != 0) return MJ_E_CORRUPT;

    mj_ledger_record_init(&rec);
    set_text(rec.dream_id, sizeof rec.dream_id, c->dream_id);
    if (c->when != NULL) {
        set_text(rec.when, sizeof rec.when, c->when);
    } else {
        char now[21];
        st = mj_utc_now(now);
        if (st != MJ_OK) return st;
        set_text(rec.when, sizeof rec.when, now);
    }
    set_text(rec.trigger, sizeof rec.trigger, c->trigger);
    set_text(rec.grader, sizeof rec.grader, c->grader);
    join_inputs(m, rec.inputs, sizeof rec.inputs);
    set_text(rec.weights_before, sizeof rec.weights_before, r->head);
    set_text(rec.weights_candidate, sizeof rec.weights_candidate, r->candidate);
    snprintf(rec.edit, sizeof rec.edit,
             "dry run, toy weights: %lu new + %lu old samples, %lu steps, max dw %ld (cap %ld), lr %ld, old per new %lu",
             (unsigned long)r->rep.samples_new, (unsigned long)r->rep.samples_old, (unsigned long)r->rep.steps,
             (long)r->rep.max_abs_delta, (long)ucfg.step_cap, (long)ucfg.lr, (unsigned long)ucfg.replay_old_per_new);
    rec.score_before_bp = r->rep.task_before_bp;
    rec.score_after_bp = r->rep.task_after_bp;
    rec.shift_bp = r->rep.shift_bp;
    set_text(rec.gates, sizeof rec.gates, r->rep.gates[0] ? r->rep.gates : "none");
    set_text(rec.outcome, sizeof rec.outcome, r->outcome);
    if (r->rep.why[0] && prior[0])
        snprintf(rec.notes, sizeof rec.notes, "%.150s; %.100s", r->rep.why, prior);
    else
        set_text(rec.notes, sizeof rec.notes, r->rep.why[0] ? r->rep.why : prior);
    st = mj_ledger_append(c->ledger_path, &b, &rec);
    if (st != MJ_OK) return st;
    r->ledger_seq = rec.seq;
    return MJ_OK;
}

mj_status mj_take_a_nap(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r) {
    mj_dream_cfg n;
    if (c == NULL || m == NULL || r == NULL) return MJ_E_INVAL;
    if (m->n_new == 0) return MJ_E_INVAL; /* a nap needs new verified experience */
    n = *c;
    n.trigger = "nap";
    return mj_dream_run(&n, m, r);
}

mj_status mj_nightly(const mj_dream_cfg *c, const mj_dream_material *m, mj_dream_result *r,
                     mj_retain_report *retain) {
    mj_dream_cfg n;
    mj_budget b;
    mj_store s;
    uint32_t checked = 0;
    mj_status st;
    if (c == NULL || m == NULL || r == NULL) return MJ_E_INVAL;
    n = *c;
    n.trigger = "nightly";
    st = mj_dream_run(&n, m, r);
    if (st != MJ_OK) return st;
    mj_budget_init(&b, c->mem_cap);
    st = mj_store_open(&s, c->store_root, &c->limits, &b);
    if (st == MJ_OK) st = mj_store_retain(&s, retain);
    if (st == MJ_OK) st = mj_store_verify_all(&s, &checked);
    return st;
}

mj_status mj_dream_seed_toy(const char *store_root, const mj_store_limits *lim, char id_out[MJ_ID_MAX + 1]) {
    mj_budget b;
    mj_store s;
    mj_toy_model g;
    uint8_t blob[MJ_TOY_BLOB_LEN];
    char head[MJ_ID_MAX + 1];
    mj_status st;
    mj_budget_init(&b, 16u * 1024u * 1024u);
    st = mj_store_open(&s, store_root, lim, &b);
    if (st != MJ_OK) return st;
    if (mj_store_head(&s, head) == MJ_OK) return MJ_E_EXISTS;
    mj_synth_genesis(&g);
    st = mj_toy_save(&g, blob);
    if (st == MJ_OK)
        st = mj_store_put(&s, "toy-genesis", MJ_KIND_MILESTONE, "-", "synthetic genesis toy weights (M0, not a real model)",
                          blob, sizeof blob, NULL);
    if (st == MJ_OK) st = mj_store_set_head(&s, "toy-genesis");
    if (st == MJ_OK && id_out != NULL) memcpy(id_out, "toy-genesis", 12);
    return st;
}
