/* The safetensors header reader: what it accepts, and every bound it checks
 * before an offset is trusted.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_test.h"

#include "mjolnir/mj_fs.h"
#include "mjolnir/mj_safet.h"

static mj_budget b;

/* parse json as the header of a file with data_bytes of tensor data */
static mj_status parse(const char *json, uint64_t data_bytes, mj_st_header *h) {
    size_t n = strlen(json);
    return mj_st_parse(json, n, 8u + n + data_bytes, &b, h);
}

static mj_status try_one(const char *json, uint64_t data_bytes) {
    mj_st_header h;
    mj_status st = parse(json, data_bytes, &h);
    if (st == MJ_OK) mj_st_free(&h);
    return st;
}

#define T(name, shape, off) "\"" name "\":{\"dtype\":\"BF16\",\"shape\":" shape ",\"data_offsets\":" off "}"

int main(void) {
    mj_st_header h;
    const mj_st_tensor *t;
    mj_budget_init(&b, 256u << 20);

    /* a good header, keys in any order, metadata skipped, padding allowed */
    {
        const char *j = "{\"__metadata__\":{\"format\":\"pt\",\"n\":{\"x\":[1,2.5e3,true,null]}},"
                        "\"b\":{\"shape\":[],\"data_offsets\":[12,16],\"dtype\":\"F32\"},"
                        T("a.w\\u00e9", "[2,3]", "[0,12]") ",\"z\":{\"dtype\":\"U8\",\"shape\":[0,4],\"data_offsets\":[16,16]}}   ";
        CHECK_ST(parse(j, 16, &h), MJ_OK);
        CHECK_EQ(h.count, 3u);
        t = mj_st_find(&h, "a.w\xc3\xa9");
        CHECK(t != NULL && t->dtype == MJ_ST_BF16 && t->ndim == 2 && t->shape[1] == 3 && t->count == 6 &&
              t->nbytes == 12 && t->abs_offset == 8 + strlen(j));
        t = mj_st_find(&h, "b");
        CHECK(t != NULL && t->ndim == 0 && t->count == 1 && t->begin == 12 && t->nbytes == 4);
        t = mj_st_find(&h, "z");
        CHECK(t != NULL && t->count == 0 && t->nbytes == 0);
        CHECK(mj_st_find(&h, "missing") == NULL);
        CHECK(mj_st_find(&h, "__metadata__") == NULL);
        mj_st_free(&h);
        CHECK_ST(try_one("{}", 0), MJ_OK);
        CHECK_ST(try_one(" { } ", 0), MJ_OK);
    }

    /* bounds */
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,12]") "}", 11), MJ_E_CORRUPT);  /* past the end of the file */
    CHECK_ST(try_one("{" T("a", "[2,3]", "[12,0]") "}", 12), MJ_E_CORRUPT);  /* begin after end */
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,10]") "}", 12), MJ_E_CORRUPT);  /* size does not match the shape */
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,12]") "," T("b", "[2]", "[10,14]") "}", 14), MJ_E_CORRUPT); /* overlap */
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,12]") "," T("b", "[2]", "[12,16]") "}", 16), MJ_OK);
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,12]") "," T("a", "[2]", "[12,16]") "}", 16), MJ_E_CORRUPT); /* twice */
    CHECK_ST(try_one("{" T("a", "[-2,3]", "[0,12]") "}", 12), MJ_E_CORRUPT); /* negative dim */
    CHECK_ST(try_one("{" T("a", "[2.0,3]", "[0,12]") "}", 12), MJ_E_CORRUPT); /* fraction */
    CHECK_ST(try_one("{" T("a", "[2e0,3]", "[0,12]") "}", 12), MJ_E_CORRUPT);
    CHECK_ST(try_one("{" T("a", "[02,3]", "[0,12]") "}", 12), MJ_E_CORRUPT); /* leading zero */
    CHECK_ST(try_one("{" T("a", "[4294967296,4294967296,4294967296]", "[0,12]") "}", 12), MJ_E_CORRUPT); /* overflow */
    CHECK_ST(try_one("{" T("a", "[18446744073709551616]", "[0,12]") "}", 12), MJ_E_CORRUPT);
    CHECK_ST(try_one("{" T("a", "[1,1,1,1,1,1,1,1,1]", "[0,2]") "}", 2), MJ_E_CORRUPT); /* too many dims */
    CHECK_ST(try_one("{" T("a", "[1,1,1,1,1,1,1,1]", "[0,2]") "}", 2), MJ_OK);
    CHECK_ST(try_one("{" T("a", "[2,3]", "[0,12,3]") "}", 12), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"Q4\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1],\"x\":1}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"U8\",\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"U8\",\"shape\":[1]}}", 1), MJ_E_CORRUPT); /* key missing */
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}} x", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]},}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"__metadata__\":{},\"__metadata__\":{}}", 0), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"__metadata__\":[]}", 0), MJ_E_CORRUPT);
    /* strings */
    CHECK_ST(try_one("{\"a\\u0000\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\\ud800\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\\ud83d\\ude00\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_OK);
    CHECK_ST(try_one("{\"a\xc0\xaf\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\x01\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    CHECK_ST(try_one("{\"a\\q\":{\"dtype\":\"U8\",\"shape\":[1],\"data_offsets\":[0,1]}}", 1), MJ_E_CORRUPT);
    /* deep metadata */
    {
        char deep[400];
        size_t i, o = 0;
        o += (size_t)sprintf(deep, "{\"__metadata__\":");
        for (i = 0; i < 100; ++i) deep[o++] = '[';
        for (i = 0; i < 100; ++i) deep[o++] = ']';
        deep[o++] = '}';
        deep[o] = '\0';
        CHECK_ST(try_one(deep, 0), MJ_E_CORRUPT);
    }
    /* header length against the file */
    {
        const char *j = "{}";
        CHECK_ST(mj_st_parse(j, 2, 9, &b, &h), MJ_E_CORRUPT);
        CHECK_ST(mj_st_parse(j, 1, 9, &b, &h), MJ_E_CORRUPT);
        CHECK_ST(mj_st_parse(j, 2, 10, &b, &h), MJ_OK);
        mj_st_free(&h);
    }
    /* from a file */
    {
        char dir[MJ_PATH_MAX], p[MJ_PATH_MAX];
        const char *j = "{" T("w", "[2,2]", "[0,8]") "}";
        uint8_t file[256];
        size_t n = strlen(j), k;
        mj_t_dir(dir, sizeof dir, "safet");
        (void)mj_fs_mkdir(dir);
        mj_path_join(p, sizeof p, dir, "m.safetensors");
        for (k = 0; k < 8; ++k) file[k] = (uint8_t)((uint64_t)n >> (8 * k));
        memcpy(file + 8, j, n);
        memset(file + 8 + n, 0x3f, 8);
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, file, 8 + n + 8, 0), MJ_OK);
        CHECK_ST(mj_st_read_header(p, &b, &h), MJ_OK);
        CHECK(h.count == 1 && h.t[0].abs_offset == 8 + n);
        mj_st_free(&h);
        /* the same file cut one byte short */
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, file, 8 + n + 7, 0), MJ_OK);
        CHECK_ST(mj_st_read_header(p, &b, &h), MJ_E_CORRUPT);
        /* a length that claims more than the file */
        file[0] = (uint8_t)(n + 100);
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, file, 8 + n + 8, 0), MJ_OK);
        CHECK_ST(mj_st_read_header(p, &b, &h), MJ_E_CORRUPT);
        /* a length past the 100 MiB ceiling */
        memset(file, 0, 8);
        file[3] = 0x10;
        (void)mj_fs_remove(p);
        CHECK_ST(mj_fs_write_atomic(p, file, 8 + n + 8, 0), MJ_OK);
        CHECK_ST(mj_st_read_header(p, &b, &h), MJ_E_CORRUPT);
        (void)mj_fs_remove(p);
    }
    CHECK_EQ(b.used, 0u);
    return mj_t_done("test_safet");
}
