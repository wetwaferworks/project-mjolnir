/* Recompute a dream ledger's hash chain from the first line to the last.
 * Exit 0 when intact, 1 when broken (the first bad line is printed), 2 on
 * usage or read errors.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_ledger.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    mj_budget b;
    mj_ledger_state st;
    uint64_t bad = 0;
    char hex[MJ_SHA256_HEX];
    mj_status s;
    if (argc != 2) {
        fprintf(stderr, "usage: mj_ledger_verify LEDGER_FILE\n");
        return 2;
    }
    mj_budget_init(&b, MJ_LEDGER_FILE_MAX + 1024u);
    s = mj_ledger_verify_file(argv[1], &b, &st, &bad);
    if (s == MJ_E_CHAIN) {
        printf("BROKEN at line %llu\n", (unsigned long long)bad);
        return 1;
    }
    if (s != MJ_OK) {
        fprintf(stderr, "cannot verify: %s\n", mj_status_name(s));
        return 2;
    }
    mj_hex_encode(st.last_hash, MJ_SHA256_LEN, hex);
    printf("OK %llu records, head %s\n", (unsigned long long)st.count, hex);
    if (st.count > 0) printf("last: %s %s %s\n", st.last.dream_id, st.last.when, st.last.outcome);
    return 0;
}
