/* The nightly dream: one dry-run dream, then retention and a store check.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_cli_common.h"

int main(int argc, char **argv) {
    mj_cli o;
    int rc = mj_cli_parse(argc, argv, &o,
                          "usage: mj_nightly --dry-run --store DIR --ledger FILE --id DREAM_ID [--grader NAME] [--seed-toy]\n");
    if (rc != 0) return rc;
    return mj_cli_run(&o, 1);
}
