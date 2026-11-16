/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_common.h"
#include "mjolnir/mj_hash.h"
#include "mj_test.h"

static void hex_of(const char *msg, size_t n, char out[65]) { mj_sha256_hex(msg, n, out); }

int main(void) {
    char h[65];
    static char million[1000000];
    uint8_t d1[32], d2[32];
    mj_sha256_ctx c;
    size_t i;
    uint64_t v;
    uint8_t raw[4];

    hex_of("", 0, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    hex_of("abc", 3, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    memset(million, 'a', sizeof million);
    hex_of(million, sizeof million, h);
    CHECK(strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);

    /* streaming in odd pieces equals one shot */
    mj_sha256(million, 100003, d1);
    mj_sha256_init(&c);
    for (i = 0; i < 100003;) {
        size_t step = (i % 7) * 13 + 1;
        if (i + step > 100003) step = 100003 - i;
        mj_sha256_update(&c, million + i, step);
        i += step;
    }
    mj_sha256_final(&c, d2);
    CHECK(memcmp(d1, d2, 32) == 0);

    CHECK_EQ(mj_crc32("123456789", 9), 0xCBF43926u);
    CHECK_EQ(mj_crc32("", 0), 0u);
    CHECK_EQ(mj_crc32_update(mj_crc32("1234", 4), "56789", 5), 0xCBF43926u);

    /* id, text and number rules */
    CHECK(mj_id_valid("toy-genesis"));
    CHECK(!mj_id_valid(""));
    CHECK(!mj_id_valid(".hidden"));
    CHECK(!mj_id_valid("-flag"));
    CHECK(!mj_id_valid("a/b"));
    CHECK(!mj_id_valid("a\\b"));
    CHECK(!mj_id_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")); /* 65 */
    CHECK(mj_text_valid("plain note, ok."));
    CHECK(!mj_text_valid("pipe|inside"));
    CHECK(!mj_text_valid("new\nline"));
    CHECK(mj_parse_u64("18446744073709551615", 20, &v) && v == UINT64_MAX);
    CHECK(!mj_parse_u64("18446744073709551616", 20, &v));
    CHECK(!mj_parse_u64("007", 3, &v));
    CHECK(mj_parse_u64("0", 1, &v) && v == 0);
    CHECK(!mj_hex_decode("ABCD", 4, raw)); /* lowercase only */
    CHECK(mj_hex_decode("abcd", 4, raw) && raw[0] == 0xab && raw[1] == 0xcd);
    {
        mj_budget b;
        void *p, *q;
        mj_budget_init(&b, 100);
        p = mj_balloc(&b, 60);
        q = mj_balloc(&b, 60);
        CHECK(p != NULL && q == NULL); /* the ceiling holds */
        mj_bfree(&b, p, 60);
        CHECK_EQ(b.used, 0u);
        CHECK_EQ(b.peak, 60u);
    }
    return mj_t_done("test_hash");
}
