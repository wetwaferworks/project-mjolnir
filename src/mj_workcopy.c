/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_workcopy.h"
#include "mjolnir/mj_apply.h"
#include "mjolnir/mj_bf16.h"
#include "mjolnir/mj_delta.h"
#include "mjolnir/mj_file.h"
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_safet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK (8u * 1024u * 1024u)
#define STATE_MAX (MJ_WC_TOUCHED_MAX * (MJ_WC_TNAME_MAX + 10u) + 4096u)

/* ---------------- checksums ---------------- */

void mj_sums_free(mj_sums *s) {
    if (s && s->v && s->budget) mj_bfree(s->budget, s->v, s->cap * sizeof *s->v);
    if (s) {
        s->v = NULL;
        s->n = s->cap = 0;
    }
}

static mj_status sums_push(mj_sums *s, const char *name, size_t nl, uint64_t size, const char *sha) {
    if (nl == 0 || nl > MJ_WC_NAME_MAX) return MJ_E_LIMIT;
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        mj_sum *nv;
        if (nc > MJ_WC_FILES_MAX) nc = MJ_WC_FILES_MAX;
        if (s->n >= nc) return MJ_E_LIMIT;
        nv = (mj_sum *)mj_balloc(s->budget, nc * sizeof *nv);
        if (nv == NULL) return MJ_E_LIMIT;
        if (s->v) {
            memcpy(nv, s->v, s->n * sizeof *nv);
            mj_bfree(s->budget, s->v, s->cap * sizeof *s->v);
        }
        s->v = nv;
        s->cap = nc;
    }
    memcpy(s->v[s->n].name, name, nl);
    s->v[s->n].name[nl] = '\0';
    s->v[s->n].size = size;
    snprintf(s->v[s->n].sha, sizeof s->v[s->n].sha, "%s", sha);
    ++s->n;
    return MJ_OK;
}

static int sum_cmp(const void *a, const void *b) { return strcmp(((const mj_sum *)a)->name, ((const mj_sum *)b)->name); }

typedef struct list_ctx {
    mj_sums *s;
    mj_status st;
} list_ctx;

static int list_cb(const char *name, void *u) {
    list_ctx *c = (list_ctx *)u;
    c->st = sums_push(c->s, name, strlen(name), 0, "");
    return c->st != MJ_OK;
}

static int ascii_name(const char *s) {
    for (; *s; ++s)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s >= 0x7F) return 0;
    return 1;
}

mj_status mj_wc_checksums(const char *folder, mj_budget *b, mj_sums *out) {
    list_ctx c;
    mj_status st;
    size_t i;
    memset(out, 0, sizeof *out);
    out->budget = b;
    c.s = out;
    c.st = MJ_OK;
    st = mj_fs_list(folder, MJ_WC_FILES_MAX, list_cb, &c);
    if (st == MJ_OK) st = c.st;
    if (st == MJ_OK) {
        qsort(out->v, out->n, sizeof *out->v, sum_cmp);
        for (i = 0; i < out->n && st == MJ_OK; ++i) {
            char p[MJ_PATH_MAX];
            uint8_t h[MJ_SHA256_LEN];
            if (!ascii_name(out->v[i].name)) { st = MJ_E_INVAL; break; } /* the sums file is ASCII */
            st = mj_path_join(p, sizeof p, folder, out->v[i].name);
            if (st == MJ_OK) st = mj_fs_sha256_file(p, h, &out->v[i].size);
            if (st == MJ_OK) mj_hex_encode(h, MJ_SHA256_LEN, out->v[i].sha);
        }
    }
    if (st != MJ_OK) mj_sums_free(out);
    return st;
}

mj_status mj_wc_format_sums(const mj_sums *s, mj_budget *b, char **out, size_t *n) {
    size_t len = 0, i, o = 0;
    char *buf;
    for (i = 0; i < s->n; ++i)
        len += (size_t)snprintf(NULL, 0, "%s  %llu  %s\n", s->v[i].sha, (unsigned long long)s->v[i].size, s->v[i].name);
    buf = (char *)mj_balloc(b, len + 1);
    if (buf == NULL) return MJ_E_LIMIT;
    for (i = 0; i < s->n; ++i)
        o += (size_t)snprintf(buf + o, len + 1 - o, "%s  %llu  %s\n", s->v[i].sha, (unsigned long long)s->v[i].size,
                              s->v[i].name);
    *out = buf;
    *n = o;
    return MJ_OK;
}

mj_status mj_wc_parse_sums(const char *text, size_t n, mj_budget *b, mj_sums *out) {
    size_t pos = 0;
    mj_status st = MJ_OK;
    memset(out, 0, sizeof *out);
    out->budget = b;
    while (pos < n && st == MJ_OK) {
        size_t e = pos, q, k;
        uint64_t size;
        while (e < n && text[e] != '\n') ++e;
        /* <64 hex>  <size>  <name> */
        if (e - pos < 64 + 2 + 1 + 2 + 1 || text[pos + 64] != ' ' || text[pos + 65] != ' ') { st = MJ_E_CORRUPT; break; }
        for (k = 0; k < 64; ++k) {
            char c = text[pos + k];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) st = MJ_E_CORRUPT;
        }
        q = pos + 66;
        while (q + 1 < e && !(text[q] == ' ' && text[q + 1] == ' ')) ++q;
        if (st != MJ_OK || q + 1 >= e || q + 2 >= e || !mj_parse_u64(text + pos + 66, q - (pos + 66), &size)) {
            st = MJ_E_CORRUPT;
            break;
        }
        {
            char sha[MJ_SHA256_HEX];
            memcpy(sha, text + pos, 64);
            sha[64] = '\0';
            st = sums_push(out, text + q + 2, e - (q + 2), size, sha);
            if (st == MJ_OK && (!ascii_name(out->v[out->n - 1].name))) st = MJ_E_CORRUPT;
        }
        pos = e + 1;
    }
    if (st == MJ_OK) {
        size_t i;
        qsort(out->v, out->n, sizeof *out->v, sum_cmp);
        for (i = 1; i < out->n; ++i)
            if (strcmp(out->v[i - 1].name, out->v[i].name) == 0) st = MJ_E_CORRUPT;
    }
    if (st != MJ_OK) mj_sums_free(out);
    return st;
}

int mj_wc_sums_equal(const mj_sums *a, const mj_sums *b) {
    size_t i;
    if (a->n != b->n) return 0;
    for (i = 0; i < a->n; ++i)
        if (strcmp(a->v[i].name, b->v[i].name) != 0 || a->v[i].size != b->v[i].size ||
            strcmp(a->v[i].sha, b->v[i].sha) != 0)
            return 0;
    return 1;
}

const mj_sum *mj_wc_sums_find(const mj_sums *s, const char *name) {
    size_t i;
    for (i = 0; i < s->n; ++i)
        if (strcmp(s->v[i].name, name) == 0) return &s->v[i];
    return NULL;
}

/* ---------------- name sets ---------------- */

void mj_names_free(mj_names *s) {
    if (s && s->v && s->budget) mj_bfree(s->budget, s->v, s->cap * sizeof *s->v);
    if (s) {
        s->v = NULL;
        s->n = s->cap = 0;
    }
}

static size_t names_lower(const mj_names *s, const char *name) {
    size_t lo = 0, hi = s->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(s->v[mid], name) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int mj_names_has(const mj_names *s, const char *name) {
    size_t i = names_lower(s, name);
    return i < s->n && strcmp(s->v[i], name) == 0;
}

static int tname_ok(const char *s) {
    size_t n = strlen(s), i;
    if (n < 1 || n > MJ_WC_TNAME_MAX) return 0;
    for (i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
            return 0;
    }
    return 1;
}

mj_status mj_names_add(mj_names *s, const char *name) {
    size_t at;
    if (!tname_ok(name)) return MJ_E_INVAL;
    at = names_lower(s, name);
    if (at < s->n && strcmp(s->v[at], name) == 0) return MJ_OK;
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        char(*nv)[MJ_WC_TNAME_MAX + 1];
        if (s->n >= MJ_WC_TOUCHED_MAX) return MJ_E_LIMIT;
        if (nc > MJ_WC_TOUCHED_MAX) nc = MJ_WC_TOUCHED_MAX;
        nv = (char(*)[MJ_WC_TNAME_MAX + 1])mj_balloc(s->budget, nc * sizeof *nv);
        if (nv == NULL) return MJ_E_LIMIT;
        if (s->v) {
            memcpy(nv, s->v, s->n * sizeof *nv);
            mj_bfree(s->budget, s->v, s->cap * sizeof *s->v);
        }
        s->v = nv;
        s->cap = nc;
    }
    memmove(s->v + at + 1, s->v + at, (s->n - at) * sizeof *s->v);
    memset(s->v[at], 0, sizeof s->v[at]);
    memcpy(s->v[at], name, strlen(name));
    ++s->n;
    return MJ_OK;
}

/* ---------------- folders ---------------- */

static int inside(const char *outer, const char *inner) {
    size_t l = strlen(outer);
    if (l > 0 && outer[l - 1] == '/') --l; /* a root such as "/" or "c:/" */
    return strncmp(inner, outer, l) == 0 && inner[l] == '/';
}

mj_status mj_wc_guard(const char *original, const char *working) {
    char o[MJ_PATH_MAX], w[MJ_PATH_MAX];
    mj_status st;
    if ((st = mj_fs_realpath(original, o, sizeof o)) != MJ_OK) return st;
    if ((st = mj_fs_realpath(working, w, sizeof w)) != MJ_OK) return st;
    if (strcmp(o, w) == 0 || inside(o, w) || inside(w, o)) return MJ_E_FORBIDDEN;
    return MJ_OK;
}

static int any_cb(const char *name, void *u) {
    (void)name;
    *(int *)u = 1;
    return 1;
}

static int folder_has_files(const char *dir) {
    int any = 0;
    (void)mj_fs_list(dir, MJ_WC_FILES_MAX, any_cb, &any);
    return any;
}

mj_status mj_wc_make_copy(const char *original, const char *working, mj_budget *b, mj_sums *out) {
    mj_sums after;
    mj_status st;
    size_t i;
    memset(out, 0, sizeof *out);
    if ((st = mj_wc_guard(original, working)) != MJ_OK) return st;
    if (mj_fs_is_dir(working) && folder_has_files(working)) return MJ_E_EXISTS;
    if ((st = mj_fs_mkdir(working)) != MJ_OK) return st;
    if ((st = mj_wc_checksums(original, b, out)) != MJ_OK) return st;
    for (i = 0; i < out->n && st == MJ_OK; ++i) {
        char s[MJ_PATH_MAX], d[MJ_PATH_MAX];
        st = mj_path_join(s, sizeof s, original, out->v[i].name);
        if (st == MJ_OK) st = mj_path_join(d, sizeof d, working, out->v[i].name);
        if (st == MJ_OK) st = mj_fs_copy_new(s, d);
    }
    if (st == MJ_OK) st = mj_wc_checksums(working, b, &after);
    else memset(&after, 0, sizeof after);
    if (st == MJ_OK && !mj_wc_sums_equal(out, &after)) st = MJ_E_CORRUPT;
    mj_sums_free(&after);
    for (i = 0; i < out->n && st == MJ_OK; ++i) {
        char s[MJ_PATH_MAX];
        st = mj_path_join(s, sizeof s, original, out->v[i].name);
        if (st == MJ_OK) st = mj_fs_set_readonly(s);
    }
    if (st != MJ_OK) mj_sums_free(out);
    return st;
}

/* ---------------- state ---------------- */

void mj_wc_state_free(mj_wc_state *st) {
    mj_names_free(&st->touched);
    mj_names_free(&st->dirty_touched);
}

/* MJWORK1 / MJDIRTY1 files: a first line, then key=value lines. */
static mj_status read_kv_file(const char *path, mj_budget *b, const char *magic, char applied[MJ_ID_MAX + 1],
                              mj_names *names) {
    uint8_t *buf;
    size_t n, pos = 0, line = 0;
    mj_status st = mj_fs_read_all(path, STATE_MAX, b, &buf, &n);
    if (st != MJ_OK) return st;
    while (pos < n && st == MJ_OK) {
        size_t e = pos, l;
        const char *s = (const char *)buf + pos;
        while (e < n && buf[e] != '\n') {
            if (buf[e] >= 0x80) st = MJ_E_CORRUPT;
            ++e;
        }
        l = e - pos;
        if (st != MJ_OK) break;
        if (line == 0) {
            if (l != strlen(magic) || memcmp(s, magic, l) != 0) st = MJ_E_CORRUPT;
        } else if (line == 1) {
            if (l < 8 || memcmp(s, "applied=", 8) != 0 || l - 8 > MJ_ID_MAX) st = MJ_E_CORRUPT;
            else {
                memcpy(applied, s + 8, l - 8);
                applied[l - 8] = '\0';
                if (!(strcmp(applied, "-") == 0 || mj_id_valid(applied))) st = MJ_E_CORRUPT;
            }
        } else if (l >= 8 && memcmp(s, "touched=", 8) == 0) {
            char name[MJ_WC_TNAME_MAX + 1];
            if (l - 8 > MJ_WC_TNAME_MAX) st = MJ_E_CORRUPT;
            else {
                memcpy(name, s + 8, l - 8);
                name[l - 8] = '\0';
                st = mj_names_add(names, name);
                if (st == MJ_E_INVAL) st = MJ_E_CORRUPT;
            }
        }
        ++line;
        pos = e + 1;
    }
    if (st == MJ_OK && line < 2) st = MJ_E_CORRUPT;
    mj_bfree(b, buf, n ? n : 1);
    return st;
}

mj_status mj_wc_read_state(const char *working, mj_budget *b, mj_wc_state *out) {
    char p[MJ_PATH_MAX], dummy[MJ_ID_MAX + 1];
    mj_status st;
    memset(out, 0, sizeof *out);
    out->touched.budget = b;
    out->dirty_touched.budget = b;
    memcpy(out->applied, "-", 2);
    if ((st = mj_path_join(p, sizeof p, working, MJ_WC_STATE_FILE)) != MJ_OK) return st;
    if (mj_fs_exists(p)) {
        st = read_kv_file(p, b, "MJWORK1", out->applied, &out->touched);
        if (st != MJ_OK) { mj_wc_state_free(out); return st; }
    }
    if ((st = mj_path_join(p, sizeof p, working, MJ_WC_DIRTY_FILE)) != MJ_OK) { mj_wc_state_free(out); return st; }
    if (mj_fs_exists(p)) {
        out->dirty = 1;
        st = read_kv_file(p, b, "MJDIRTY1", dummy, &out->dirty_touched);
        if (st != MJ_OK) { mj_wc_state_free(out); return st; }
    }
    return MJ_OK;
}

static mj_status write_kv_file(const char *working, const char *file, const char *magic, const char *applied,
                               const mj_names *touched) {
    char p[MJ_PATH_MAX];
    size_t cap = 64 + MJ_ID_MAX + touched->n * (MJ_WC_TNAME_MAX + 10), o = 0, i;
    char *buf;
    mj_status st;
    if (!(strcmp(applied, "-") == 0 || mj_id_valid(applied))) return MJ_E_INVAL;
    if ((st = mj_path_join(p, sizeof p, working, file)) != MJ_OK) return st;
    buf = (char *)malloc(cap);
    if (buf == NULL) return MJ_E_LIMIT;
    o += (size_t)snprintf(buf + o, cap - o, "%s\napplied=%s\n", magic, applied);
    for (i = 0; i < touched->n; ++i) o += (size_t)snprintf(buf + o, cap - o, "touched=%s\n", touched->v[i]);
    st = mj_fs_write_atomic(p, buf, o, 1);
    free(buf);
    return st;
}

mj_status mj_wc_write_state(const char *working, const char *applied, const mj_names *touched) {
    return write_kv_file(working, MJ_WC_STATE_FILE, "MJWORK1", applied, touched);
}

/* ---------------- checkout ---------------- */

typedef struct chain_item {
    mj_delta d;
} chain_item;

static void reason(mj_wc_result *r, const char *a, const char *b) {
    snprintf(r->reason, sizeof r->reason, "%s%s", a, b ? b : "");
}

/* The raw 8 + n header bytes of a safetensors file (for comparing two). */
static mj_status raw_header(const char *path, mj_budget *b, uint8_t **out, size_t *n) {
    mj_file f;
    uint8_t len8[8];
    uint64_t size, hl = 0;
    int k;
    mj_status st = mj_file_open(&f, path, 0);
    *out = NULL;
    if (st != MJ_OK) return st;
    st = mj_file_size(&f, &size);
    if (st == MJ_OK && size < 8) st = MJ_E_CORRUPT;
    if (st == MJ_OK) st = mj_file_pread(&f, 0, len8, 8);
    if (st == MJ_OK) {
        for (k = 7; k >= 0; --k) hl = (hl << 8) | len8[k];
        if (hl > MJ_ST_HEADER_MAX || hl > size - 8) st = MJ_E_CORRUPT;
    }
    if (st == MJ_OK) {
        *n = (size_t)(8 + hl);
        *out = (uint8_t *)mj_balloc(b, *n);
        if (*out == NULL) st = MJ_E_LIMIT;
        else st = mj_file_pread(&f, 0, *out, *n);
        if (st != MJ_OK && *out) { mj_bfree(b, *out, *n); *out = NULL; }
    }
    mj_file_close(&f);
    return st;
}

static mj_status model_paths(const char *original, const char *working, char *o, char *w, mj_wc_result *res,
                             mj_file_id *io_out, mj_file_id *iw_out) {
    mj_file_id io, iw;
    mj_status st;
    if ((st = mj_wc_guard(original, working)) != MJ_OK) {
        reason(res, "the working copy must be a separate folder from the original", NULL);
        return st;
    }
    if ((st = mj_path_join(o, MJ_PATH_MAX, original, MJ_WC_MODEL_FILE)) != MJ_OK) return st;
    if ((st = mj_path_join(w, MJ_PATH_MAX, working, MJ_WC_MODEL_FILE)) != MJ_OK) return st;
    if ((st = mj_file_identity(o, &io)) != MJ_OK || (st = mj_file_identity(w, &iw)) != MJ_OK) {
        reason(res, "model file missing", NULL);
        return st;
    }
    if (io.dev == iw.dev && io.ino == iw.ino) {
        reason(res, "the working model file is the original file", NULL);
        return MJ_E_FORBIDDEN;
    }
    if (iw.nlink != 1) {
        reason(res, "the working model file is hard-linked; refusing to write through it", NULL);
        return MJ_E_FORBIDDEN;
    }
    if (io_out) *io_out = io;
    if (iw_out) *iw_out = iw;
    return MJ_OK;
}

/* Copy the original bytes of one tensor into the working file, hashing what
 * was written. */
static mj_status restore_tensor(mj_file *fo, mj_file *fw, const mj_st_tensor *t, uint8_t *buf,
                                uint8_t hash[MJ_SHA256_LEN]) {
    uint64_t done = 0;
    mj_sha256_ctx c;
    mj_status st = MJ_OK;
    mj_sha256_init(&c);
    while (done < t->nbytes && st == MJ_OK) {
        size_t n = (t->nbytes - done) > CHUNK ? CHUNK : (size_t)(t->nbytes - done);
        st = mj_file_pread(fo, t->abs_offset + done, buf, n);
        if (st == MJ_OK) st = mj_file_pwrite(fw, t->abs_offset + done, buf, n);
        if (st == MJ_OK) mj_sha256_update(&c, buf, n);
        done += n;
    }
    mj_sha256_final(&c, hash);
    return st;
}

static mj_status readback(mj_file *fw, const mj_st_tensor *t, uint8_t *buf, const uint8_t want[MJ_SHA256_LEN]) {
    uint64_t done = 0;
    mj_sha256_ctx c;
    uint8_t got[MJ_SHA256_LEN];
    mj_status st = MJ_OK;
    mj_sha256_init(&c);
    while (done < t->nbytes && st == MJ_OK) {
        size_t n = (t->nbytes - done) > CHUNK ? CHUNK : (size_t)(t->nbytes - done);
        st = mj_file_pread(fw, t->abs_offset + done, buf, n);
        if (st == MJ_OK) mj_sha256_update(&c, buf, n);
        done += n;
    }
    if (st != MJ_OK) return st;
    mj_sha256_final(&c, got);
    return memcmp(got, want, MJ_SHA256_LEN) == 0 ? MJ_OK : MJ_E_CORRUPT;
}

static void put_le16(uint8_t *p, const uint16_t *v, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        p[2 * i] = (uint8_t)v[i];
        p[2 * i + 1] = (uint8_t)(v[i] >> 8);
    }
}

static void get_le16(uint16_t *v, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) v[i] = (uint16_t)(p[2 * i] | (p[2 * i + 1] << 8));
}

mj_status mj_wc_checkout(mj_store *store, const char *version_id, const char *original, const char *working,
                         const char *base_sha, const mj_wc_options *opt, mj_budget *b, mj_wc_result *res) {
    char o_file[MJ_PATH_MAX], w_file[MJ_PATH_MAX], dirty_path[MJ_PATH_MAX];
    mj_st_header hdr;
    mj_version_info *chain = NULL;
    chain_item *items = NULL;
    size_t nchain = 0, nitems = 0, i, k, chain_cap = 0;
    mj_names sums, touched;
    mj_wc_state state;
    mj_file fo, fw;
    uint8_t *hashes = NULL, *buf = NULL, *ho = NULL, *hw = NULL;
    size_t nho = 0, nhw = 0;
    mj_status st;
    char cur[MJ_ID_MAX + 1];
    int have_state = 0, files_open = 0;
    mj_file_id id_o, id_w, now_id;

    memset(res, 0, sizeof *res);
    memset(&hdr, 0, sizeof hdr);
    memset(&sums, 0, sizeof sums);
    memset(&touched, 0, sizeof touched);
    sums.budget = touched.budget = b;
    fo.h = fw.h = -1;
    if (store == NULL || version_id == NULL || base_sha == NULL || opt == NULL || b == NULL) return MJ_E_INVAL;
    if ((st = model_paths(original, working, o_file, w_file, res, &id_o, &id_w)) != MJ_OK) return st;
    if ((st = mj_st_read_header(o_file, b, &hdr)) != MJ_OK) {
        reason(res, "the original model file's header does not check out", NULL);
        return st;
    }
    /* the working file must have the very same header, so the offsets hold */
    st = raw_header(o_file, b, &ho, &nho);
    if (st == MJ_OK) st = raw_header(w_file, b, &hw, &nhw);
    if (st == MJ_OK && (nho != nhw || memcmp(ho, hw, nho) != 0)) {
        reason(res, "the working model file's header differs from the original's", NULL);
        st = MJ_E_FORBIDDEN;
    }
    if (ho) mj_bfree(b, ho, nho);
    if (hw) mj_bfree(b, hw, nhw);
    if (st != MJ_OK) goto out;

    /* the chain: the version and its parents, oldest first */
    chain_cap = store->lim.max_versions ? store->lim.max_versions : 4096;
    chain = (mj_version_info *)mj_balloc(b, chain_cap * sizeof *chain);
    if (chain == NULL) { st = MJ_E_LIMIT; goto out; }
    snprintf(cur, sizeof cur, "%s", version_id);
    while (strcmp(cur, "-") != 0) {
        for (i = 0; i < nchain; ++i)
            if (strcmp(chain[i].id, cur) == 0) { reason(res, "parent loop at ", cur); st = MJ_E_CORRUPT; goto out; }
        if (nchain == chain_cap) { st = MJ_E_LIMIT; goto out; }
        if ((st = mj_store_info(store, cur, &chain[nchain])) != MJ_OK) { reason(res, "no such version: ", cur); goto out; }
        snprintf(cur, sizeof cur, "%s", chain[nchain].parent);
        ++nchain;
    }
    items = (chain_item *)mj_balloc(b, (nchain ? nchain : 1) * sizeof *items);
    if (items == NULL) { st = MJ_E_LIMIT; goto out; }
    for (i = nchain; i-- > 0;) {
        uint8_t *blob;
        size_t n;
        if ((st = mj_store_get(store, chain[i].id, &blob, &n)) != MJ_OK) { reason(res, "cannot read version ", chain[i].id); goto out; }
        if (chain[i].kind == MJ_KIND_MILESTONE && n >= 7 && memcmp(blob, "MJBASE1", 7) == 0) {
            mj_bfree(b, blob, n ? n : 1);
            continue; /* the base record: nothing to add */
        }
        st = mj_delta_unpack(blob, n, b, &items[nitems].d);
        mj_bfree(b, blob, n ? n : 1);
        if (st != MJ_OK) { reason(res, "not a delta: ", chain[i].id); goto out; }
        ++nitems;
        if (strcmp(items[nitems - 1].d.base, base_sha) != 0) {
            reason(res, "version was computed on a different model file: ", chain[i].id);
            st = MJ_E_FORBIDDEN;
            goto out;
        }
        for (k = 0; k < items[nitems - 1].d.count; ++k) {
            const mj_delta_entry *e = &items[nitems - 1].d.e[k];
            const mj_st_tensor *t = mj_st_find(&hdr, e->name);
            if (t == NULL || t->dtype != MJ_ST_BF16) {
                reason(res, "delta names a tensor the model does not have: ", e->name);
                st = MJ_E_FORBIDDEN;
                goto out;
            }
            if (t->ndim != 2 || t->shape[0] != e->rows || t->shape[1] != e->cols) {
                reason(res, "delta shape does not match ", e->name);
                st = MJ_E_FORBIDDEN;
                goto out;
            }
            if ((st = mj_names_add(&sums, e->name)) != MJ_OK) goto out;
        }
    }

    /* everything touched before (and anything a cut-short write named) gets restored */
    if ((st = mj_wc_read_state(working, b, &state)) != MJ_OK) { reason(res, "working copy state file", NULL); goto out; }
    have_state = 1;
    for (i = 0; i < state.touched.n && st == MJ_OK; ++i) st = mj_names_add(&touched, state.touched.v[i]);
    for (i = 0; i < state.dirty_touched.n && st == MJ_OK; ++i) st = mj_names_add(&touched, state.dirty_touched.v[i]);
    for (i = 0; i < sums.n && st == MJ_OK; ++i) st = mj_names_add(&touched, sums.v[i]);
    if (st != MJ_OK) goto out;
    for (i = 0; i < touched.n; ++i) {
        const mj_st_tensor *t = mj_st_find(&hdr, touched.v[i]);
        if (t == NULL || t->dtype != MJ_ST_BF16) {
            reason(res, "the state file names a tensor the model does not have: ", touched.v[i]);
            st = MJ_E_CORRUPT;
            goto out;
        }
    }

    /* Every tensor is restored from the original, so the original must still be byte for byte the file the base
     * record checksummed. Hash it through the very handle the restore reads from, and check that handle is the
     * file checked by name above, so nothing can swap or change it between the check and the reads. */
    files_open = 1;
    if ((st = mj_file_open(&fo, o_file, 0)) != MJ_OK) { reason(res, "cannot open the original model file", NULL); goto out; }
    if ((st = mj_file_handle_identity(&fo, &now_id)) != MJ_OK) goto out;
    if (!mj_file_same(&now_id, &id_o)) {
        reason(res, "the original model file changed between the check and the open", NULL);
        st = MJ_E_FORBIDDEN;
        goto out;
    }
    {
        uint8_t h[MJ_SHA256_LEN];
        char hex[MJ_SHA256_HEX];
        if ((st = mj_file_sha256(&fo, h, NULL)) != MJ_OK) { reason(res, "cannot checksum the original model file", NULL); goto out; }
        mj_hex_encode(h, MJ_SHA256_LEN, hex);
        if (strcmp(hex, base_sha) != 0) {
            reason(res, "the original model file no longer matches the base record; nothing was written", NULL);
            st = MJ_E_CORRUPT;
            goto out;
        }
    }

    /* the dirty marker goes down before the first byte changes */
    if ((st = mj_path_join(dirty_path, sizeof dirty_path, working, MJ_WC_DIRTY_FILE)) != MJ_OK) goto out;
    if ((st = write_kv_file(working, MJ_WC_DIRTY_FILE, "MJDIRTY1", state.applied, &touched)) != MJ_OK) {
        reason(res, "cannot write the dirty marker", NULL);
        goto out;
    }

    hashes = (uint8_t *)mj_balloc(b, (touched.n ? touched.n : 1) * MJ_SHA256_LEN);
    buf = (uint8_t *)mj_balloc(b, CHUNK);
    if (hashes == NULL || buf == NULL) { st = MJ_E_LIMIT; goto out; }
    if ((st = mj_file_open(&fw, w_file, 1)) != MJ_OK) { reason(res, "cannot open the working model file for writing", NULL); goto out; }
    /* the same checks as by name, now on the handle that writes */
    if ((st = mj_file_handle_identity(&fw, &now_id)) != MJ_OK) goto out;
    if (!mj_file_same(&now_id, &id_w) || mj_file_same(&now_id, &id_o) || now_id.nlink != 1) {
        reason(res, "the working model file changed, is the original, or is hard-linked; refusing to write", NULL);
        st = MJ_E_FORBIDDEN;
        goto out;
    }
    for (i = 0; i < touched.n && st == MJ_OK; ++i) {
        const mj_st_tensor *t = mj_st_find(&hdr, touched.v[i]);
        if (!mj_names_has(&sums, touched.v[i])) {
            st = restore_tensor(&fo, &fw, t, buf, hashes + i * MJ_SHA256_LEN);
        } else {
            /* gather this tensor's terms in chain order */
            size_t nt = 0, ti = 0, j;
            mj_apply_term *terms;
            uint8_t *raw;
            uint16_t *orig, *outv;
            for (j = 0; j < nitems; ++j)
                for (k = 0; k < items[j].d.count; ++k)
                    if (strcmp(items[j].d.e[k].name, t->name) == 0) ++nt;
            terms = (mj_apply_term *)mj_balloc(b, nt * sizeof *terms);
            raw = (uint8_t *)mj_balloc(b, (size_t)t->nbytes);
            orig = (uint16_t *)mj_balloc(b, (size_t)t->nbytes);
            outv = (uint16_t *)mj_balloc(b, (size_t)t->nbytes);
            if (terms == NULL || raw == NULL || orig == NULL || outv == NULL) st = MJ_E_LIMIT;
            for (j = 0; j < nitems && st == MJ_OK; ++j)
                for (k = 0; k < items[j].d.count; ++k) {
                    const mj_delta_entry *e = &items[j].d.e[k];
                    if (strcmp(e->name, t->name) != 0) continue;
                    terms[ti].U = e->U;
                    terms[ti].V = e->V;
                    terms[ti].rank = e->rank;
                    terms[ti].scale = (float)e->scale;
                    ++ti;
                }
            if (st == MJ_OK) st = mj_file_pread(&fo, t->abs_offset, raw, (size_t)t->nbytes);
            if (st == MJ_OK) {
                get_le16(orig, raw, (size_t)t->count);
                st = MJ_E_NOTFOUND;
                if (opt->use_cuda) {
                    uint64_t peak = 0;
                    st = mj_apply_cuda(orig, outv, (uint32_t)t->shape[0], (uint32_t)t->shape[1], terms, nt, &peak);
                    if (st == MJ_OK) {
                        res->used_cuda = 1;
                        if (peak > res->device_peak) res->device_peak = peak;
                    }
                }
                if (st == MJ_E_NOTFOUND)
                    st = mj_apply_cpu(orig, outv, (uint32_t)t->shape[0], (uint32_t)t->shape[1], terms, nt, opt->threads);
            }
            if (st == MJ_OK) {
                put_le16(raw, outv, (size_t)t->count);
                mj_sha256(raw, (size_t)t->nbytes, hashes + i * MJ_SHA256_LEN);
                st = mj_file_pwrite(&fw, t->abs_offset, raw, (size_t)t->nbytes);
            }
            if (terms) mj_bfree(b, terms, nt * sizeof *terms);
            if (raw) mj_bfree(b, raw, (size_t)t->nbytes);
            if (orig) mj_bfree(b, orig, (size_t)t->nbytes);
            if (outv) mj_bfree(b, outv, (size_t)t->nbytes);
            if (st == MJ_OK) ++res->edited;
        }
    }
    if (st == MJ_OK) st = mj_file_sync(&fw);
    mj_file_close(&fw);
    if (st != MJ_OK) { reason(res, "writing the working copy failed; the dirty marker stays", NULL); goto out; }
    /* read back through a fresh handle */
    if ((st = mj_file_open(&fw, w_file, 0)) == MJ_OK) {
        for (i = 0; i < touched.n && st == MJ_OK; ++i)
            st = readback(&fw, mj_st_find(&hdr, touched.v[i]), buf, hashes + i * MJ_SHA256_LEN);
        mj_file_close(&fw);
    }
    if (st != MJ_OK) { reason(res, "read-back mismatch; the dirty marker stays", NULL); goto out; }
    /* the state lists what now differs from the original */
    if ((st = mj_wc_write_state(working, version_id, &sums)) != MJ_OK) goto out;
    if ((st = mj_fs_remove(dirty_path)) != MJ_OK) goto out;
    res->touched = touched.n;

out:
    if (files_open) {
        mj_file_close(&fo);
        mj_file_close(&fw);
    }
    if (buf) mj_bfree(b, buf, CHUNK);
    if (hashes) mj_bfree(b, hashes, (touched.n ? touched.n : 1) * MJ_SHA256_LEN);
    if (have_state) mj_wc_state_free(&state);
    for (i = 0; i < nitems; ++i) mj_delta_free(&items[i].d);
    if (items) mj_bfree(b, items, (nchain ? nchain : 1) * sizeof *items);
    if (chain) mj_bfree(b, chain, chain_cap * sizeof *chain);
    mj_names_free(&sums);
    mj_names_free(&touched);
    mj_st_free(&hdr);
    return st;
}
