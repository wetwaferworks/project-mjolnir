/* Re-check every version in a weight version store: manifest, size, CRC-32
 * and SHA-256 of each blob, and HEAD. Read-only. Exit 0 when every version
 * checks out, 1 on the first bad one, 2 on usage errors.
 *
 *   mj_store_verify STORE [--max-blob-mb N]
 *
 * The blob ceiling defaults to the M0 limit; real-weight deltas from M1 are
 * a few megabytes, so the default is enough for them as well.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    mj_store_limits lim;
    mj_budget b;
    mj_store s;
    char head[MJ_ID_MAX + 1];
    uint32_t checked = 0;
    mj_status st;
    if (argc != 2 && !(argc == 4 && strcmp(argv[2], "--max-blob-mb") == 0)) {
        fprintf(stderr, "usage: mj_store_verify STORE [--max-blob-mb N]\n");
        return 2;
    }
    mj_store_default_limits(&lim);
    if (argc == 4) {
        unsigned long mb = strtoul(argv[3], NULL, 10);
        if (mb == 0 || mb > 4096) {
            fprintf(stderr, "--max-blob-mb must be 1 to 4096\n");
            return 2;
        }
        lim.max_blob_bytes = (uint64_t)mb * 1024u * 1024u;
        if (lim.max_store_bytes < lim.max_blob_bytes) lim.max_store_bytes = lim.max_blob_bytes;
    }
    mj_budget_init(&b, (size_t)lim.max_blob_bytes + 1024u * 1024u);
    st = mj_store_open(&s, argv[1], &lim, &b);
    if (st == MJ_OK) st = mj_store_verify_all(&s, &checked);
    if (st != MJ_OK) {
        printf("BAD after %u versions: %s\n", checked, mj_status_name(st));
        return 1;
    }
    st = mj_store_head(&s, head);
    if (st == MJ_E_NOTFOUND) memcpy(head, "-", 2);
    else if (st != MJ_OK) {
        printf("BAD HEAD: %s\n", mj_status_name(st));
        return 1;
    }
    printf("OK %u versions, head %s\n", checked, head);
    return 0;
}
