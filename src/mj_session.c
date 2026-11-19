/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_session.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_ledger.h"

#include <stdio.h>
#include <string.h>

mj_status mj_real_paths_for(const char *data, mj_real_paths *p) {
    mj_status st = mj_path_join(p->store, sizeof p->store, data, "real-store");
    if (st == MJ_OK) st = mj_path_join(p->ledger, sizeof p->ledger, data, "real-dreams.mjl");
    if (st == MJ_OK) st = mj_path_join(p->reports, sizeof p->reports, data, "reports");
    return st;
}

void mj_real_limits(mj_store_limits *l) {
    mj_store_default_limits(l);
    l->max_store_bytes = 8ull << 30;
    l->max_blob_bytes = 1ull << 30;
}

mj_status mj_utc_stamp(char out[17]) {
    char iso[21];
    size_t i, o = 0;
    mj_status st = mj_utc_now(iso);
    if (st != MJ_OK) return st;
    for (i = 0; i < 20; ++i)
        if (iso[i] != '-' && iso[i] != ':') out[o++] = iso[i];
    out[o] = '\0';
    return o == 16 ? MJ_OK : MJ_E_IO;
}

static void say(char *msg, size_t cap, const char *s) {
    if (msg && cap) snprintf(msg, cap, "%s", s);
}

static mj_status open_store(const char *data, mj_budget *b, mj_store *s, mj_real_paths *p) {
    mj_store_limits l;
    mj_status st = mj_fs_mkdir(data);
    if (st == MJ_OK) st = mj_real_paths_for(data, p);
    mj_real_limits(&l);
    if (st == MJ_OK) st = mj_store_open(s, p->store, &l, b);
    return st;
}

static void copy_field(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }

static int dir_has_files_cb(const char *name, void *u) {
    (void)name;
    *(int *)u = 1;
    return 1;
}

mj_status mj_real_register(const char *original, const char *working, const char *data, mj_budget *b, char *msg,
                           size_t cap) {
    mj_sums sums, wsums;
    mj_store s;
    mj_real_paths p;
    mj_status st;
    int any = 0;
    const mj_sum *model;
    say(msg, cap, "");
    memset(&sums, 0, sizeof sums);
    /* checked first: an "identical" working folder that is the original
     * itself would otherwise pass, and the state file would land in it */
    if ((st = mj_wc_guard(original, working)) != MJ_OK) {
        say(msg, cap, "the working copy must be a separate folder from the original");
        return st;
    }
    (void)mj_fs_list(working, MJ_WC_FILES_MAX, dir_has_files_cb, &any);
    if (any) {
        if ((st = mj_wc_checksums(original, b, &sums)) != MJ_OK) return st;
        st = mj_wc_checksums(working, b, &wsums);
        if (st == MJ_OK && !mj_wc_sums_equal(&sums, &wsums)) {
            say(msg, cap, "a working copy exists and differs from the original; check it out from the store instead");
            st = MJ_E_FORBIDDEN;
        }
        if (st == MJ_OK) say(msg, cap, "working copy already present and identical");
        mj_sums_free(&wsums);
    } else {
        st = mj_wc_make_copy(original, working, b, &sums);
        if (st == MJ_E_FORBIDDEN) say(msg, cap, "the working copy must be a separate folder from the original");
        else if (st == MJ_OK) say(msg, cap, "working copy made and verified");
    }
    if (st != MJ_OK) { mj_sums_free(&sums); return st; }
    model = mj_wc_sums_find(&sums, MJ_WC_MODEL_FILE);
    if (model == NULL) {
        say(msg, cap, "the original folder has no " MJ_WC_MODEL_FILE);
        mj_sums_free(&sums);
        return MJ_E_NOTFOUND;
    }
    if ((st = open_store(data, b, &s, &p)) != MJ_OK) { mj_sums_free(&sums); return st; }
    if (!mj_store_ever_used(&s, MJ_REAL_BASE_ID)) {
        char *text = NULL, *blob = NULL;
        size_t tn = 0, bn = 0;
        mj_names none;
        mj_dream_record r;
        char stamp[17];
        memset(&none, 0, sizeof none);
        st = mj_wc_format_sums(&sums, b, &text, &tn);
        if (st == MJ_OK) {
            bn = 8 + tn;
            blob = (char *)mj_balloc(b, bn);
            if (blob == NULL) st = MJ_E_LIMIT;
            else {
                memcpy(blob, "MJBASE1\n", 8);
                memcpy(blob + 8, text, tn);
            }
        }
        if (st == MJ_OK)
            st = mj_store_put(&s, MJ_REAL_BASE_ID, MJ_KIND_MILESTONE, "-", "original download, untouched", blob, bn, NULL);
        if (st == MJ_OK) st = mj_store_set_head(&s, MJ_REAL_BASE_ID);
        if (st == MJ_OK) st = mj_wc_write_state(working, MJ_REAL_BASE_ID, &none);
        if (st == MJ_OK) {
            mj_ledger_record_init(&r);
            st = mj_utc_stamp(stamp);
            if (st == MJ_OK) st = mj_utc_now(r.when);
            snprintf(r.dream_id, sizeof r.dream_id, "register-%s", stamp);
            copy_field(r.trigger, sizeof r.trigger, "register");
            copy_field(r.grader, sizeof r.grader, "auto-check");
            copy_field(r.weights_before, sizeof r.weights_before, "-");
            copy_field(r.weights_candidate, sizeof r.weights_candidate, MJ_REAL_BASE_ID);
            copy_field(r.edit, sizeof r.edit, "working copy made; original checksummed and marked read-only");
            copy_field(r.gates, sizeof r.gates, "copy=pass");
            copy_field(r.outcome, sizeof r.outcome, "integrated");
            snprintf(r.notes, sizeof r.notes, "model=%.16s", model->sha);
            if (st == MJ_OK) st = mj_ledger_append(p.ledger, b, &r);
        }
        if (text) mj_bfree(b, text, tn + 1);
        if (blob) mj_bfree(b, blob, bn);
    }
    mj_sums_free(&sums);
    return st;
}

mj_status mj_real_base_sha(const char *data, mj_budget *b, char sha[MJ_SHA256_HEX]) {
    mj_store s;
    mj_real_paths p;
    mj_sums sums;
    uint8_t *blob;
    size_t n;
    const mj_sum *m;
    mj_status st = open_store(data, b, &s, &p);
    if (st != MJ_OK) return st;
    if ((st = mj_store_get(&s, MJ_REAL_BASE_ID, &blob, &n)) != MJ_OK) return st;
    if (n < 8 || memcmp(blob, "MJBASE1\n", 8) != 0) st = MJ_E_CORRUPT;
    else st = mj_wc_parse_sums((const char *)blob + 8, n - 8, b, &sums);
    mj_bfree(b, blob, n ? n : 1);
    if (st != MJ_OK) return st;
    m = mj_wc_sums_find(&sums, MJ_WC_MODEL_FILE);
    if (m == NULL) st = MJ_E_CORRUPT;
    else memcpy(sha, m->sha, MJ_SHA256_HEX);
    mj_sums_free(&sums);
    return st;
}

mj_status mj_real_apply(const char *original, const char *working, const char *data, const char *version_id,
                        const mj_wc_options *opt, mj_budget *b, mj_wc_result *res) {
    mj_store s;
    mj_real_paths p;
    mj_wc_state state;
    char before[MJ_ID_MAX + 1], sha[MJ_SHA256_HEX], stamp[17];
    mj_dream_record r;
    mj_status st;
    memset(res, 0, sizeof *res);
    if (version_id == NULL || !mj_id_valid(version_id)) return MJ_E_INVAL;
    if ((st = open_store(data, b, &s, &p)) != MJ_OK) return st;
    if ((st = mj_wc_read_state(working, b, &state)) != MJ_OK) {
        snprintf(res->reason, sizeof res->reason, "working copy state file");
        return st;
    }
    copy_field(before, sizeof before, state.applied);
    mj_wc_state_free(&state);
    if ((st = mj_real_base_sha(data, b, sha)) != MJ_OK) {
        snprintf(res->reason, sizeof res->reason, "no base record; register first");
        return st;
    }
    if ((st = mj_wc_checkout(&s, version_id, original, working, sha, opt, b, res)) != MJ_OK) return st;
    if ((st = mj_store_set_head(&s, version_id)) != MJ_OK) return st;
    mj_ledger_record_init(&r);
    if ((st = mj_utc_stamp(stamp)) != MJ_OK || (st = mj_utc_now(r.when)) != MJ_OK) return st;
    snprintf(r.dream_id, sizeof r.dream_id, "apply-%s", stamp);
    copy_field(r.trigger, sizeof r.trigger, "apply");
    copy_field(r.grader, sizeof r.grader, "auto-check");
    copy_field(r.weights_before, sizeof r.weights_before, before);
    copy_field(r.weights_candidate, sizeof r.weights_candidate, version_id);
    snprintf(r.edit, sizeof r.edit, "wrote %lu tensors into the working copy", (unsigned long)res->touched);
    copy_field(r.gates, sizeof r.gates, "readback=pass");
    copy_field(r.outcome, sizeof r.outcome, strcmp(version_id, MJ_REAL_BASE_ID) == 0 ? "rolled-back" : "integrated");
    copy_field(r.notes, sizeof r.notes, "working copy only; the original is untouched");
    return mj_ledger_append(p.ledger, b, &r);
}
