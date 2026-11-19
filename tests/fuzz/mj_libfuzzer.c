/* libFuzzer front end. Build with clang -fsanitize=fuzzer,address and pick the
 * target with -DMJ_FUZZ_TARGET=mj_fuzz_ledger (or _manifest, _toy, _score, _safet, _delta, _json, _http).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mj_fuzz_targets.h"

#ifndef MJ_FUZZ_TARGET
#define MJ_FUZZ_TARGET mj_fuzz_ledger
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)mj_fuzz_ledger; (void)mj_fuzz_manifest; (void)mj_fuzz_toy; (void)mj_fuzz_score; (void)mj_fuzz_safet; (void)mj_fuzz_delta; (void)mj_fuzz_json; (void)mj_fuzz_http;
    return MJ_FUZZ_TARGET(data, size);
}
