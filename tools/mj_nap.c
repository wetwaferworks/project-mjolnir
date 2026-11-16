/* "Take a nap": a dry-run dream started by new verified experience.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_cli_common.h"

int main(int argc, char **argv) {
    mj_cli o;
    int rc = mj_cli_parse(argc, argv, &o,
                          "usage: mj_nap --dry-run --store DIR --ledger FILE --id DREAM_ID [--grader NAME] [--seed-toy]\n");
    if (rc != 0) return rc;
    return mj_cli_run(&o, 0);
}
