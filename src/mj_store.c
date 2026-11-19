/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_store.h"
#include "mjolnir/mj_fs.h"

#include <stdio.h>
#include <string.h>

#define MANIFEST_MAX 1024u
#define HEAD_MAX 256u

const char *mj_kind_name(mj_kind k) {
    switch (k) {
    case MJ_KIND_SNAPSHOT: return "snapshot";
    case MJ_KIND_NIGHTLY: return "nightly";
    case MJ_KIND_MILESTONE: return "milestone";
    case MJ_KIND_CANDIDATE: return "candidate";
    }
    return NULL;
}

static int kind_parse(const char *s, size_t n, mj_kind *k) {
    int i;
    for (i = 1; i <= 4; ++i) {
        const char *nm = mj_kind_name((mj_kind)i);
        if (strlen(nm) == n && memcmp(nm, s, n) == 0) { *k = (mj_kind)i; return 1; }
    }
    return 0;
}

void mj_store_default_limits(mj_store_limits *l) {
    l->max_store_bytes = 1024ull * 1024ull * 1024ull; /* 1 GiB of toy weights */
    l->max_blob_bytes = MJ_BLOB_MAX_DEFAULT;
    l->min_free_bytes = 4ull * 1024ull * 1024ull * 1024ull; /* keep 4 GiB of disk free */
    l->keep_nightly = 14;
    l->keep_snapshots = 14;
    l->keep_candidates = 14;
    l->max_versions = 4096;
}

int mj_payload_is_model_file(const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    if (n >= 4 && memcmp(p, "GGUF", 4) == 0) return 1;
    if (n >= 4 && memcmp(p, "PK\x03\x04", 4) == 0) return 1; /* PyTorch .pt / .bin zips */
    if (n >= 9) { /* safetensors: little-endian u64 header length, then '{' */
        uint64_t hl = 0;
        int i;
        for (i = 7; i >= 0; --i) hl = (hl << 8) | p[i];
        if (p[8] == '{' && hl > 1 && hl <= n - 8) return 1;
    }
    return 0;
}

/* ---- manifest text ---- */

size_t mj_manifest_format(const mj_version_info *v, char *out, size_t cap) {
    char sha[MJ_SHA256_HEX];
    int n, m;
    const char *kn = mj_kind_name(v->kind);
    if (kn == NULL) return 0;
    mj_hex_encode(v->sha256, MJ_SHA256_LEN, sha);
    n = snprintf(out, cap, "MJVER1\nid=%s\nseq=%llu\nkind=%s\nsha256=%s\nsize=%llu\ncrc32=%08lx\nparent=%s\nlabel=%s\n",
                 v->id, (unsigned long long)v->seq, kn, sha, (unsigned long long)v->size,
                 (unsigned long)v->crc32, v->parent, v->label);
    if (n <= 0 || (size_t)n >= cap) return 0;
    m = snprintf(out + n, cap - (size_t)n, "mcrc=%08lx\n", (unsigned long)mj_crc32(out, (size_t)n));
    if (m <= 0 || (size_t)(n + m) >= cap) return 0;
    return (size_t)(n + m);
}

static int take(const char *t, size_t n, size_t *pos, const char *key, const char **val, size_t *vlen) {
    size_t kl = strlen(key), p = *pos, e;
    if (p + kl > n || memcmp(t + p, key, kl) != 0) return 0;
    p += kl;
    for (e = p; e < n && t[e] != '\n'; ++e) {
        if (t[e] == '\0' || t[e] == '\r') return 0;
    }
    if (e >= n) return 0;
    *val = t + p;
    *vlen = e - p;
    *pos = e + 1;
    return 1;
}

static int copy_field(char *dst, size_t cap, const char *v, size_t vl) {
    if (vl >= cap) return 0;
    memcpy(dst, v, vl);
    dst[vl] = '\0';
    return 1;
}

static int parse_hex32(const char *v, size_t vl, uint32_t *out) {
    uint8_t b[4];
    if (vl != 8 || !mj_hex_decode(v, 8, b)) return 0;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return 1;
}

mj_status mj_manifest_parse(const char *t, size_t n, mj_version_info *out) {
    size_t pos = 0, vl, body_end;
    const char *v;
    mj_version_info r;
    uint32_t mcrc;
    memset(&r, 0, sizeof r);
    if (t == NULL || n == 0 || n > MANIFEST_MAX) return MJ_E_INVAL;
    if (!take(t, n, &pos, "MJVER1", &v, &vl) || vl != 0) return MJ_E_INVAL;
    if (!take(t, n, &pos, "id=", &v, &vl) || !copy_field(r.id, sizeof r.id, v, vl) || !mj_id_valid(r.id)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "seq=", &v, &vl) || !mj_parse_u64(v, vl, &r.seq)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "kind=", &v, &vl) || !kind_parse(v, vl, &r.kind)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "sha256=", &v, &vl) || vl != 64 || !mj_hex_decode(v, 64, r.sha256)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "size=", &v, &vl) || !mj_parse_u64(v, vl, &r.size)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "crc32=", &v, &vl) || !parse_hex32(v, vl, &r.crc32)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "parent=", &v, &vl) || !copy_field(r.parent, sizeof r.parent, v, vl)) return MJ_E_INVAL;
    if (strcmp(r.parent, "-") != 0 && !mj_id_valid(r.parent)) return MJ_E_INVAL;
    if (!take(t, n, &pos, "label=", &v, &vl) || !copy_field(r.label, sizeof r.label, v, vl) || !mj_text_valid(r.label)) return MJ_E_INVAL;
    body_end = pos;
    if (!take(t, n, &pos, "mcrc=", &v, &vl) || !parse_hex32(v, vl, &mcrc)) return MJ_E_INVAL;
    if (pos != n) return MJ_E_INVAL; /* nothing may follow */
    if (mj_crc32(t, body_end) != mcrc) return MJ_E_CORRUPT;
    *out = r;
    return MJ_OK;
}

/* ---- paths ---- */

static mj_status sub(const mj_store *s, const char *dir, const char *name, const char *ext, char *out) {
    char leaf[MJ_PATH_MAX], d[MJ_PATH_MAX];
    int n = snprintf(leaf, sizeof leaf, "%s%s", name, ext);
    mj_status st;
    if (n <= 0 || (size_t)n >= sizeof leaf) return MJ_E_LIMIT;
    if (dir == NULL) return mj_path_join(out, MJ_PATH_MAX, s->root, leaf);
    st = mj_path_join(d, sizeof d, s->root, dir);
    if (st != MJ_OK) return st;
    return mj_path_join(out, MJ_PATH_MAX, d, leaf);
}

static mj_status blob_path(const mj_store *s, const uint8_t sha[MJ_SHA256_LEN], char *out) {
    char hex[MJ_SHA256_HEX];
    mj_hex_encode(sha, MJ_SHA256_LEN, hex);
    return sub(s, "objects", hex, ".blob", out);
}

/* ---- scanning ---- */

typedef struct scan_ctx {
    mj_store *s;
    const char *dir;
    uint64_t max_seq;
    uint64_t bytes;
    mj_status err;
    mj_version_info *list;
    uint32_t count, cap;
} scan_ctx;

static int ends_with(const char *s, const char *suf, size_t *stem) {
    size_t a = strlen(s), b = strlen(suf);
    if (a <= b || strcmp(s + a - b, suf) != 0) return 0;
    *stem = a - b;
    return 1;
}

static mj_status read_manifest(mj_store *s, const char *dir, const char *id, mj_version_info *out) {
    char p[MJ_PATH_MAX];
    uint8_t *buf;
    size_t n;
    mj_status st = sub(s, dir, id, ".ver", p);
    if (st != MJ_OK) return st;
    st = mj_fs_read_all(p, MANIFEST_MAX, s->budget, &buf, &n);
    if (st != MJ_OK) return st;
    st = mj_manifest_parse((const char *)buf, n, out);
    mj_bfree(s->budget, buf, n ? n : 1);
    if (st == MJ_OK && strcmp(out->id, id) != 0) st = MJ_E_CORRUPT; /* file name must match its id */
    return st;
}

static int scan_versions_cb(const char *name, void *user) {
    scan_ctx *c = (scan_ctx *)user;
    char id[MJ_ID_MAX + 1];
    size_t stem;
    mj_version_info v;
    mj_status st;
    if (!ends_with(name, ".ver", &stem) || stem > MJ_ID_MAX) return 0; /* ignore stray .tmp files */
    memcpy(id, name, stem);
    id[stem] = '\0';
    if (!mj_id_valid(id)) return 0;
    st = read_manifest(c->s, c->dir, id, &v);
    if (st != MJ_OK) { c->err = st; return 1; }
    if (v.seq > c->max_seq) c->max_seq = v.seq;
    if (c->list != NULL) {
        if (c->count >= c->cap) { c->err = MJ_E_LIMIT; return 1; }
        c->list[c->count++] = v;
    }
    return 0;
}

static int scan_blobs_cb(const char *name, void *user) {
    scan_ctx *c = (scan_ctx *)user;
    char p[MJ_PATH_MAX], d[MJ_PATH_MAX];
    uint64_t sz;
    size_t stem;
    if (!ends_with(name, ".blob", &stem)) return 0;
    if (mj_path_join(d, sizeof d, c->s->root, "objects") != MJ_OK || mj_path_join(p, sizeof p, d, name) != MJ_OK) {
        c->err = MJ_E_LIMIT;
        return 1;
    }
    if (mj_fs_size(p, &sz) != MJ_OK) { c->err = MJ_E_IO; return 1; }
    c->bytes += sz;
    return 0;
}

static mj_status scan_dir(mj_store *s, const char *dir, scan_ctx *c, mj_fs_list_fn fn) {
    char d[MJ_PATH_MAX];
    mj_status st = mj_path_join(d, sizeof d, s->root, dir);
    if (st != MJ_OK) return st;
    c->s = s;
    c->dir = dir;
    st = mj_fs_list(d, s->lim.max_versions, fn, c);
    if (st != MJ_OK) return st;
    return c->err;
}

mj_status mj_store_open(mj_store *s, const char *root, const mj_store_limits *lim, mj_budget *b) {
    static const char *dirs[] = {"objects", "versions", "retired", "pins"};
    size_t i, rl;
    scan_ctx c;
    mj_status st;
    if (s == NULL || root == NULL || lim == NULL || b == NULL) return MJ_E_INVAL;
    if (lim->keep_nightly == 0 || lim->keep_snapshots == 0 || lim->max_versions == 0 ||
        lim->max_blob_bytes == 0 || lim->max_blob_bytes > lim->max_store_bytes)
        return MJ_E_INVAL;
    rl = strlen(root);
    if (rl == 0 || rl + 64 >= MJ_PATH_MAX) return MJ_E_LIMIT;
    memset(s, 0, sizeof *s);
    memcpy(s->root, root, rl + 1);
    s->lim = *lim;
    s->budget = b;
    st = mj_fs_mkdir(root);
    if (st != MJ_OK) return st;
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; ++i) {
        char d[MJ_PATH_MAX];
        st = mj_path_join(d, sizeof d, root, dirs[i]);
        if (st == MJ_OK) st = mj_fs_mkdir(d);
        if (st != MJ_OK) return st;
    }
    memset(&c, 0, sizeof c);
    st = scan_dir(s, "versions", &c, scan_versions_cb);
    if (st == MJ_OK) st = scan_dir(s, "retired", &c, scan_versions_cb);
    if (st == MJ_OK) st = scan_dir(s, "objects", &c, scan_blobs_cb);
    if (st != MJ_OK) return st;
    s->next_seq = c.max_seq + 1;
    s->used_bytes = c.bytes;
    return MJ_OK;
}

static mj_status verify_blob(mj_store *s, const mj_version_info *v, uint8_t **data, size_t *n) {
    char p[MJ_PATH_MAX];
    uint8_t sha[MJ_SHA256_LEN];
    uint8_t *buf;
    size_t len;
    mj_status st = blob_path(s, v->sha256, p);
    if (st != MJ_OK) return st;
    if (v->size > s->lim.max_blob_bytes) return MJ_E_LIMIT;
    st = mj_fs_read_all(p, (size_t)s->lim.max_blob_bytes, s->budget, &buf, &len);
    if (st != MJ_OK) return st == MJ_E_NOTFOUND ? MJ_E_CORRUPT : st;
    mj_sha256(buf, len, sha);
    if ((uint64_t)len != v->size || mj_crc32(buf, len) != v->crc32 || memcmp(sha, v->sha256, MJ_SHA256_LEN) != 0) {
        mj_bfree(s->budget, buf, len ? len : 1);
        return MJ_E_CORRUPT;
    }
    if (data != NULL) {
        *data = buf;
        *n = len;
    } else {
        mj_bfree(s->budget, buf, len ? len : 1);
    }
    return MJ_OK;
}

static int ver_equal(const mj_version_info *a, const mj_version_info *b) {
    return strcmp(a->id, b->id) == 0 && a->seq == b->seq && a->kind == b->kind &&
           memcmp(a->sha256, b->sha256, MJ_SHA256_LEN) == 0 && a->size == b->size && a->crc32 == b->crc32 &&
           strcmp(a->parent, b->parent) == 0 && strcmp(a->label, b->label) == 0;
}

static int id_ever_used(mj_store *s, const char *id) {
    char p[MJ_PATH_MAX];
    if (sub(s, "versions", id, ".ver", p) != MJ_OK || mj_fs_exists(p)) return 1;
    if (sub(s, "retired", id, ".ver", p) != MJ_OK || mj_fs_exists(p)) return 1;
    return 0;
}

int mj_store_ever_used(mj_store *s, const char *id) {
    if (s == NULL || id == NULL || !mj_id_valid(id)) return 0;
    return id_ever_used(s, id);
}

mj_status mj_store_put(mj_store *s, const char *id, mj_kind kind, const char *parent, const char *label,
                       const void *data, size_t n, mj_version_info *out) {
    mj_version_info v, back;
    char bp[MJ_PATH_MAX], mp[MJ_PATH_MAX], text[MANIFEST_MAX];
    size_t tl;
    uint64_t freeb = 0;
    int new_blob;
    mj_status st;

    if (s == NULL || !mj_id_valid(id) || mj_kind_name(kind) == NULL || (data == NULL && n != 0)) return MJ_E_INVAL;
    if (parent == NULL) parent = "-";
    if (label == NULL) label = "";
    if ((strcmp(parent, "-") != 0 && !mj_id_valid(parent)) || !mj_text_valid(label)) return MJ_E_INVAL;
    if ((uint64_t)n > s->lim.max_blob_bytes) return MJ_E_LIMIT;
    if (mj_payload_is_model_file(data, n)) return MJ_E_FORBIDDEN; /* M0: toy weights only */
    if (id_ever_used(s, id)) return MJ_E_EXISTS;
    if (strcmp(parent, "-") != 0) {
        mj_version_info pv;
        st = mj_store_info(s, parent, &pv);
        if (st != MJ_OK) return st == MJ_E_NOTFOUND ? MJ_E_INVAL : st;
    }

    memset(&v, 0, sizeof v);
    memcpy(v.id, id, strlen(id) + 1);
    v.seq = s->next_seq;
    v.kind = kind;
    mj_sha256(data, n, v.sha256);
    v.size = (uint64_t)n;
    v.crc32 = mj_crc32(data, n);
    memcpy(v.parent, parent, strlen(parent) + 1);
    memcpy(v.label, label, strlen(label) + 1);

    st = blob_path(s, v.sha256, bp);
    if (st != MJ_OK) return st;
    new_blob = !mj_fs_exists(bp);
    if (new_blob) {
        if (s->used_bytes + (uint64_t)n > s->lim.max_store_bytes) return MJ_E_FULL;
        st = mj_fs_free_bytes(s->root, &freeb);
        if (st != MJ_OK) return st;
        if (freeb < (uint64_t)n || freeb - (uint64_t)n < s->lim.min_free_bytes) return MJ_E_FULL;
        st = mj_fs_write_atomic(bp, data, n, 0);
        if (st != MJ_OK && st != MJ_E_EXISTS) return st;
    }
    /* read back and check the blob, new or shared */
    st = verify_blob(s, &v, NULL, NULL);
    if (st != MJ_OK) {
        if (new_blob) (void)mj_fs_remove(bp);
        return st;
    }
    if (new_blob) s->used_bytes += (uint64_t)n;

    tl = mj_manifest_format(&v, text, sizeof text);
    if (tl == 0) return MJ_E_LIMIT;
    st = sub(s, "versions", id, ".ver", mp);
    if (st != MJ_OK) return st;
    st = mj_fs_write_atomic(mp, text, tl, 0);
    if (st != MJ_OK) return st;
    st = read_manifest(s, "versions", id, &back);
    if (st != MJ_OK || !ver_equal(&back, &v)) {
        (void)mj_fs_remove(mp);
        return st != MJ_OK ? st : MJ_E_CORRUPT;
    }
    s->next_seq++;
    if (kind == MJ_KIND_MILESTONE) {
        st = mj_store_pin(s, id);
        if (st != MJ_OK) return st;
    }
    if (out != NULL) *out = v;
    return MJ_OK;
}

mj_status mj_store_info(mj_store *s, const char *id, mj_version_info *out) {
    char p[MJ_PATH_MAX];
    if (s == NULL || !mj_id_valid(id) || out == NULL) return MJ_E_INVAL;
    if (sub(s, "versions", id, ".ver", p) != MJ_OK) return MJ_E_LIMIT;
    if (!mj_fs_exists(p)) return MJ_E_NOTFOUND;
    return read_manifest(s, "versions", id, out);
}

mj_status mj_store_get(mj_store *s, const char *id, uint8_t **data, size_t *n) {
    mj_version_info v;
    mj_status st;
    if (data == NULL || n == NULL) return MJ_E_INVAL;
    *data = NULL;
    *n = 0;
    st = mj_store_info(s, id, &v);
    if (st != MJ_OK) return st;
    return verify_blob(s, &v, data, n);
}

mj_status mj_store_head(mj_store *s, char id_out[MJ_ID_MAX + 1]) {
    char p[MJ_PATH_MAX];
    uint8_t *buf;
    size_t n, pos = 0, vl, body;
    const char *v;
    uint32_t crc;
    mj_status st;
    if (s == NULL || id_out == NULL) return MJ_E_INVAL;
    st = mj_path_join(p, sizeof p, s->root, "HEAD");
    if (st != MJ_OK) return st;
    st = mj_fs_read_all(p, HEAD_MAX, s->budget, &buf, &n);
    if (st != MJ_OK) return st;
    st = MJ_E_CORRUPT;
    if (take((const char *)buf, n, &pos, "MJHEAD1", &v, &vl) && vl == 0 &&
        take((const char *)buf, n, &pos, "id=", &v, &vl) && vl <= MJ_ID_MAX) {
        char id[MJ_ID_MAX + 1];
        const char *cv;
        size_t cl;
        memcpy(id, v, vl);
        id[vl] = '\0';
        body = pos;
        if (mj_id_valid(id) && take((const char *)buf, n, &pos, "crc=", &cv, &cl) && parse_hex32(cv, cl, &crc) &&
            pos == n && mj_crc32(buf, body) == crc) {
            memcpy(id_out, id, vl + 1);
            st = MJ_OK;
        }
    }
    mj_bfree(s->budget, buf, n ? n : 1);
    return st;
}

mj_status mj_store_set_head(mj_store *s, const char *id) {
    char p[MJ_PATH_MAX], text[HEAD_MAX], back[MJ_ID_MAX + 1];
    mj_version_info v;
    int n, m;
    mj_status st = mj_store_info(s, id, &v);
    if (st != MJ_OK) return st;
    st = verify_blob(s, &v, NULL, NULL);
    if (st != MJ_OK) return st;
    n = snprintf(text, sizeof text, "MJHEAD1\nid=%s\n", id);
    if (n <= 0 || (size_t)n >= sizeof text) return MJ_E_LIMIT;
    m = snprintf(text + n, sizeof text - (size_t)n, "crc=%08lx\n", (unsigned long)mj_crc32(text, (size_t)n));
    if (m <= 0) return MJ_E_LIMIT;
    st = mj_path_join(p, sizeof p, s->root, "HEAD");
    if (st != MJ_OK) return st;
    st = mj_fs_write_atomic(p, text, (size_t)(n + m), 1); /* HEAD is a pointer, the one file that is replaced */
    if (st != MJ_OK) return st;
    st = mj_store_head(s, back);
    if (st != MJ_OK) return st;
    return strcmp(back, id) == 0 ? MJ_OK : MJ_E_CORRUPT;
}

mj_status mj_store_rollback(mj_store *s, const char *id) { return mj_store_set_head(s, id); }

mj_status mj_store_pin(mj_store *s, const char *id) {
    char p[MJ_PATH_MAX];
    mj_version_info v;
    mj_status st = mj_store_info(s, id, &v);
    if (st != MJ_OK) return st;
    st = sub(s, "pins", id, ".pin", p);
    if (st != MJ_OK) return st;
    st = mj_fs_write_atomic(p, "pinned\n", 7, 0);
    return st == MJ_E_EXISTS ? MJ_OK : st;
}

int mj_store_is_pinned(mj_store *s, const char *id) {
    char p[MJ_PATH_MAX];
    if (!mj_id_valid(id) || sub(s, "pins", id, ".pin", p) != MJ_OK) return 0;
    return mj_fs_exists(p);
}

static mj_status load_all(mj_store *s, mj_version_info **list, uint32_t *count) {
    scan_ctx c;
    size_t bytes = (size_t)s->lim.max_versions * sizeof(mj_version_info);
    mj_status st;
    memset(&c, 0, sizeof c);
    c.list = (mj_version_info *)mj_balloc(s->budget, bytes);
    if (c.list == NULL) return MJ_E_LIMIT;
    c.cap = s->lim.max_versions;
    st = scan_dir(s, "versions", &c, scan_versions_cb);
    if (st != MJ_OK) {
        mj_bfree(s->budget, c.list, bytes);
        return st;
    }
    *list = c.list;
    *count = c.count;
    return MJ_OK;
}

static void free_all(mj_store *s, mj_version_info *list) {
    mj_bfree(s->budget, list, (size_t)s->lim.max_versions * sizeof(mj_version_info));
}

/* rank of v among versions of the same kind, newest first (0 = newest) */
static uint32_t newer_count(const mj_version_info *list, uint32_t n, const mj_version_info *v) {
    uint32_t i, r = 0;
    for (i = 0; i < n; ++i)
        if (list[i].kind == v->kind && list[i].seq > v->seq) ++r;
    return r;
}

mj_status mj_store_retain(mj_store *s, mj_retain_report *r) {
    mj_version_info *list;
    uint32_t n, i, j;
    char head[MJ_ID_MAX + 1];
    int have_head;
    mj_status st;
    mj_retain_report rep;
    memset(&rep, 0, sizeof rep);
    if (s == NULL) return MJ_E_INVAL;
    st = load_all(s, &list, &n);
    if (st != MJ_OK) return st;
    have_head = (mj_store_head(s, head) == MJ_OK);
    rep.scanned = n;
    for (i = 0; i < n; ++i) {
        const mj_version_info *v = &list[i];
        uint32_t keep_n = 0, rank;
        int keep;
        switch (v->kind) {
        case MJ_KIND_NIGHTLY: keep_n = s->lim.keep_nightly; break;
        case MJ_KIND_SNAPSHOT: keep_n = s->lim.keep_snapshots; break;
        case MJ_KIND_CANDIDATE: keep_n = s->lim.keep_candidates; break;
        case MJ_KIND_MILESTONE: keep_n = UINT32_MAX; break;
        }
        rank = newer_count(list, n, v);
        keep = rank < keep_n || mj_store_is_pinned(s, v->id) || (have_head && strcmp(head, v->id) == 0);
        if (keep) {
            ++rep.kept;
            continue;
        }
        {
            char from[MJ_PATH_MAX], to[MJ_PATH_MAX], text[MANIFEST_MAX];
            size_t tl = mj_manifest_format(v, text, sizeof text);
            if (tl == 0) { st = MJ_E_LIMIT; break; }
            if ((st = sub(s, "versions", v->id, ".ver", from)) != MJ_OK) break;
            if ((st = sub(s, "retired", v->id, ".ver", to)) != MJ_OK) break;
            st = mj_fs_write_atomic(to, text, tl, 0); /* the record is kept, never deleted */
            if (st != MJ_OK && st != MJ_E_EXISTS) break;
            if ((st = mj_fs_remove(from)) != MJ_OK) break;
            list[i].kind = (mj_kind)0; /* mark retired in the working list */
            ++rep.retired;
        }
    }
    if (st == MJ_OK) {
        /* collect blobs no live version references */
        for (i = 0; i < n; ++i) {
            int referenced = 0;
            char bp[MJ_PATH_MAX];
            uint64_t sz = 0;
            if (list[i].kind != (mj_kind)0) continue;
            for (j = 0; j < n; ++j)
                if (list[j].kind != (mj_kind)0 && memcmp(list[j].sha256, list[i].sha256, MJ_SHA256_LEN) == 0) referenced = 1;
            for (j = 0; j < i; ++j) /* already removed by an earlier retired twin */
                if (list[j].kind == (mj_kind)0 && memcmp(list[j].sha256, list[i].sha256, MJ_SHA256_LEN) == 0) referenced = 1;
            if (referenced) continue;
            if ((st = blob_path(s, list[i].sha256, bp)) != MJ_OK) break;
            if (mj_fs_size(bp, &sz) != MJ_OK) continue;
            if ((st = mj_fs_remove(bp)) != MJ_OK) break;
            s->used_bytes = sz <= s->used_bytes ? s->used_bytes - sz : 0;
            rep.bytes_freed += sz;
            ++rep.blobs_removed;
        }
    }
    free_all(s, list);
    if (r != NULL) *r = rep;
    return st;
}

mj_status mj_store_verify_all(mj_store *s, uint32_t *checked) {
    mj_version_info *list;
    uint32_t n, i;
    mj_status st = load_all(s, &list, &n);
    if (st != MJ_OK) return st;
    for (i = 0; i < n && st == MJ_OK; ++i) st = verify_blob(s, &list[i], NULL, NULL);
    if (checked != NULL) *checked = i;
    free_all(s, list);
    return st;
}
