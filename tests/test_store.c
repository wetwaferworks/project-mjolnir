/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_store.h"
#include "mj_test.h"

static mj_budget B;

static void small_limits(mj_store_limits *l) {
    mj_store_default_limits(l);
    l->min_free_bytes = 0;
    l->max_store_bytes = 1u << 20;
    l->max_blob_bytes = 1u << 16;
}

static void payload(uint8_t *p, size_t n, uint32_t seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (uint8_t)((seed * 2654435761u + i * 40503u) >> 13);
    p[0] = 'T'; /* never looks like a model file */
}

static void flip_byte(const char *path, size_t at) {
    uint8_t *buf;
    size_t n;
    if (mj_fs_read_all(path, 1u << 20, &B, &buf, &n) != MJ_OK) return;
    if (at < n) buf[at] ^= 0x40;
    (void)mj_fs_remove(path);
    (void)mj_fs_write_atomic(path, buf, n, 0);
    mj_bfree(&B, buf, n ? n : 1);
}

int main(void) {
    char root[MJ_PATH_MAX], p[MJ_PATH_MAX], head[MJ_ID_MAX + 1], hex[MJ_SHA256_HEX];
    mj_store s;
    mj_store_limits lim;
    mj_version_info v, v2;
    uint8_t data[4096], *got;
    size_t n;
    mj_retain_report rr;
    uint32_t checked, i;

    mj_budget_init(&B, 64u << 20);
    mj_t_dir(root, sizeof root, "store_test");
    mj_t_clean(root);
    small_limits(&lim);
    CHECK_ST(mj_store_open(&s, root, &lim, &B), MJ_OK);

    /* put, get, verify */
    payload(data, sizeof data, 1);
    CHECK_ST(mj_store_put(&s, "v1", MJ_KIND_NIGHTLY, NULL, "first", data, sizeof data, &v), MJ_OK);
    CHECK_EQ(v.seq, 1u);
    CHECK_ST(mj_store_get(&s, "v1", &got, &n), MJ_OK);
    CHECK(n == sizeof data && memcmp(got, data, n) == 0);
    mj_bfree(&B, got, n);

    /* a version is never overwritten */
    payload(data, sizeof data, 2);
    CHECK_ST(mj_store_put(&s, "v1", MJ_KIND_NIGHTLY, NULL, "again", data, sizeof data, NULL), MJ_E_EXISTS);
    CHECK_ST(mj_store_get(&s, "v1", &got, &n), MJ_OK);
    payload(data, sizeof data, 1);
    CHECK(memcmp(got, data, n) == 0);
    mj_bfree(&B, got, n);

    /* bad inputs */
    CHECK_ST(mj_store_put(&s, "../escape", MJ_KIND_NIGHTLY, NULL, "", data, 16, NULL), MJ_E_INVAL);
    CHECK_ST(mj_store_put(&s, "ok", (mj_kind)9, NULL, "", data, 16, NULL), MJ_E_INVAL);
    CHECK_ST(mj_store_put(&s, "ok", MJ_KIND_NIGHTLY, "missing-parent", "", data, 16, NULL), MJ_E_INVAL);
    CHECK_ST(mj_store_put(&s, "ok", MJ_KIND_NIGHTLY, NULL, "bad|label", data, 16, NULL), MJ_E_INVAL);

    /* real model files are refused in M0 */
    memcpy(data, "GGUF", 4);
    CHECK_ST(mj_store_put(&s, "gguf", MJ_KIND_NIGHTLY, NULL, "", data, 64, NULL), MJ_E_FORBIDDEN);
    memcpy(data, "PK\x03\x04", 4);
    CHECK_ST(mj_store_put(&s, "zip", MJ_KIND_NIGHTLY, NULL, "", data, 64, NULL), MJ_E_FORBIDDEN);
    memset(data, 0, 16);
    data[0] = 4; data[8] = '{';
    CHECK_ST(mj_store_put(&s, "safet", MJ_KIND_NIGHTLY, NULL, "", data, 64, NULL), MJ_E_FORBIDDEN);

    /* ceilings */
    CHECK_ST(mj_store_put(&s, "huge", MJ_KIND_NIGHTLY, NULL, "", data, (1u << 16) + 1, NULL), MJ_E_LIMIT);
    {
        mj_store s2;
        mj_store_limits tight;
        char r2[MJ_PATH_MAX];
        mj_t_dir(r2, sizeof r2, "store_full");
        mj_t_clean(r2);
        small_limits(&tight);
        tight.max_store_bytes = 6000;
        tight.max_blob_bytes = 4096;
        CHECK_ST(mj_store_open(&s2, r2, &tight, &B), MJ_OK);
        payload(data, 4096, 10);
        CHECK_ST(mj_store_put(&s2, "a", MJ_KIND_NIGHTLY, NULL, "", data, 4096, NULL), MJ_OK);
        payload(data, 4096, 11);
        CHECK_ST(mj_store_put(&s2, "b", MJ_KIND_NIGHTLY, NULL, "", data, 4096, NULL), MJ_E_FULL);
        {
            char bp[MJ_PATH_MAX];
            snprintf(bp, sizeof bp, "%s/versions/b.ver", r2);
            CHECK(!mj_fs_exists(bp)); /* nothing half-written */
        }
        payload(data, 4096, 10); /* same content dedupes, so it fits */
        CHECK_ST(mj_store_put(&s2, "a-copy", MJ_KIND_NIGHTLY, NULL, "", data, 4096, NULL), MJ_OK);
        tight.min_free_bytes = UINT64_MAX / 2; /* free-disk floor */
        CHECK_ST(mj_store_open(&s2, r2, &tight, &B), MJ_OK);
        payload(data, 100, 12);
        CHECK_ST(mj_store_put(&s2, "c", MJ_KIND_NIGHTLY, NULL, "", data, 100, NULL), MJ_E_FULL);
    }

    /* HEAD and rollback */
    CHECK_ST(mj_store_head(&s, head), MJ_E_NOTFOUND);
    payload(data, 2048, 3);
    CHECK_ST(mj_store_put(&s, "v2", MJ_KIND_NIGHTLY, "v1", "second", data, 2048, NULL), MJ_OK);
    CHECK_ST(mj_store_set_head(&s, "v2"), MJ_OK);
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "v2") == 0);
    CHECK_ST(mj_store_rollback(&s, "v1"), MJ_OK);
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "v1") == 0);
    CHECK_ST(mj_store_rollback(&s, "nope"), MJ_E_NOTFOUND);

    /* corruption is caught: blob byte flip */
    CHECK_ST(mj_store_info(&s, "v2", &v2), MJ_OK);
    mj_hex_encode(v2.sha256, MJ_SHA256_LEN, hex);
    snprintf(p, sizeof p, "%s/objects/%s.blob", root, hex);
    flip_byte(p, 100);
    CHECK_ST(mj_store_get(&s, "v2", &got, &n), MJ_E_CORRUPT);
    CHECK_ST(mj_store_rollback(&s, "v2"), MJ_E_CORRUPT); /* never roll onto bad weights */
    CHECK_ST(mj_store_head(&s, head), MJ_OK);
    CHECK(strcmp(head, "v1") == 0);
    CHECK_ST(mj_store_verify_all(&s, &checked), MJ_E_CORRUPT);
    flip_byte(p, 100); /* restore */
    CHECK_ST(mj_store_verify_all(&s, &checked), MJ_OK);

    /* manifest tampering is caught */
    snprintf(p, sizeof p, "%s/versions/v2.ver", root);
    flip_byte(p, 30);
    CHECK(mj_store_info(&s, "v2", &v2) != MJ_OK);
    flip_byte(p, 30);
    CHECK_ST(mj_store_info(&s, "v2", &v2), MJ_OK);

    /* HEAD tampering is caught */
    snprintf(p, sizeof p, "%s/HEAD", root);
    flip_byte(p, 12);
    CHECK_ST(mj_store_head(&s, head), MJ_E_CORRUPT);
    flip_byte(p, 12);
    CHECK_ST(mj_store_head(&s, head), MJ_OK);

    /* a stale temp file from a crash is ignored */
    snprintf(p, sizeof p, "%s/versions/v9.ver.tmp", root);
    CHECK_ST(mj_fs_write_atomic(p, "junk", 4, 0), MJ_OK);
    CHECK_ST(mj_store_open(&s, root, &lim, &B), MJ_OK);
    CHECK_EQ(s.next_seq, 3u);

    /* retention: 20 nightlies, 3 snapshots, a milestone, one pinned nightly */
    CHECK_ST(mj_store_put(&s, "ms-1", MJ_KIND_MILESTONE, NULL, "graded known good", data, 64, NULL), MJ_OK);
    for (i = 0; i < 20; ++i) {
        char id[16];
        snprintf(id, sizeof id, "n-%02u", i);
        payload(data, 512, 100 + i);
        CHECK_ST(mj_store_put(&s, id, MJ_KIND_NIGHTLY, NULL, "", data, 512, NULL), MJ_OK);
    }
    CHECK_ST(mj_store_pin(&s, "n-00"), MJ_OK);
    CHECK_ST(mj_store_set_head(&s, "n-01"), MJ_OK);
    CHECK_ST(mj_store_retain(&s, &rr), MJ_OK);
    /* nightlies: v1, v2, n-00..n-19 = 22; newest 14 kept + n-00 (pinned) + n-01 (HEAD) = 16; 6 retired */
    CHECK_EQ(rr.retired, 6u);
    CHECK_EQ(rr.kept, 17u); /* 16 nightlies + the milestone */
    CHECK(rr.blobs_removed >= 4u);
    CHECK_ST(mj_store_info(&s, "n-02", &v), MJ_E_NOTFOUND);
    CHECK_ST(mj_store_info(&s, "n-00", &v), MJ_OK);
    CHECK_ST(mj_store_info(&s, "ms-1", &v), MJ_OK);
    CHECK(mj_store_is_pinned(&s, "ms-1"));
    /* a retired id can never be reused */
    payload(data, 512, 999);
    CHECK_ST(mj_store_put(&s, "n-02", MJ_KIND_NIGHTLY, NULL, "", data, 512, NULL), MJ_E_EXISTS);
    snprintf(p, sizeof p, "%s/retired/n-02.ver", root);
    CHECK(mj_fs_exists(p)); /* the record is kept */
    CHECK_ST(mj_store_verify_all(&s, &checked), MJ_OK);
    CHECK_EQ(checked, 17u);
    CHECK_ST(mj_store_retain(&s, &rr), MJ_OK); /* idempotent */
    CHECK_EQ(rr.retired, 0u);

    /* reopen: sequence numbers continue past retired versions */
    CHECK_ST(mj_store_open(&s, root, &lim, &B), MJ_OK);
    CHECK_EQ(s.next_seq, 24u);

    /* manifest parser strictness */
    {
        char t[1024];
        size_t tl;
        mj_version_info x;
        CHECK_ST(mj_store_info(&s, "ms-1", &v), MJ_OK);
        tl = mj_manifest_format(&v, t, sizeof t);
        CHECK(tl > 0);
        CHECK_ST(mj_manifest_parse(t, tl, &x), MJ_OK);
        CHECK_ST(mj_manifest_parse(t, tl - 1, &x), MJ_E_INVAL); /* torn */
        t[tl - 2] = t[tl - 2] == '0' ? '1' : '0';
        CHECK(mj_manifest_parse(t, tl, &x) != MJ_OK);
    }
    CHECK_EQ(B.used, 0u); /* no leaks against the budget */
    return mj_t_done("test_store");
}
