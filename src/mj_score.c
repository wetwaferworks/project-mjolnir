/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_score.h"

#include <string.h>

static uint32_t bp(uint64_t num, uint64_t den) {
    if (den == 0) return 0u;
    if (num > den) num = den;
    while (den > (UINT64_MAX / MJ_BP_FULL)) { /* keep num * 10000 inside 64 bits */
        num >>= 1;
        den >>= 1;
    }
    return (uint32_t)((num * MJ_BP_FULL) / den);
}

mj_status mj_score_maze(const mj_maze_segment *seg, uint32_t n, mj_credit *labels, mj_maze_score *out) {
    mj_maze_score s;
    uint32_t i;
    int failed = 0, stopped = 0;
    if (seg == NULL || out == NULL || n == 0 || n > MJ_SCORE_MAX_PARTS) return MJ_E_INVAL;
    memset(&s, 0, sizeof s);
    /* validate the whole run before writing anything */
    for (i = 0; i < n; ++i) {
        if (seg[i].type < MJ_SEG_PIT || seg[i].type > MJ_SEG_GOAL) return MJ_E_INVAL;
        switch (seg[i].result) {
        case MJ_RUN_CLEARED:
            if (failed || stopped) return MJ_E_INVAL;
            break;
        case MJ_RUN_FAILED:
            if (failed || stopped) return MJ_E_INVAL;
            failed = 1;
            break;
        case MJ_RUN_NOT_REACHED:
            stopped = 1;
            break;
        default: return MJ_E_INVAL;
        }
    }
    for (i = 0; i < n; ++i) {
        mj_credit c = seg[i].result == MJ_RUN_CLEARED  ? MJ_CREDIT_SUCCESS
                      : seg[i].result == MJ_RUN_FAILED ? MJ_CREDIT_NOT_YET
                                                       : MJ_CREDIT_UNGRADED;
        if (c == MJ_CREDIT_SUCCESS) ++s.success;
        else if (c == MJ_CREDIT_NOT_YET) ++s.not_yet;
        else ++s.ungraded;
        if (labels != NULL) labels[i] = c;
    }
    s.total = n;
    s.score_bp = bp(s.success, n);
    s.escaped = (s.success == n && seg[n - 1].type == MJ_SEG_GOAL);
    *out = s;
    return MJ_OK;
}

mj_status mj_score_circuit(const mj_circuit_element *el, uint32_t n, mj_credit *labels, mj_circuit_score *out) {
    uint8_t seen[65536u / 8u];
    mj_circuit_score s;
    uint32_t i;
    if (el == NULL || out == NULL || n == 0 || n > MJ_SCORE_MAX_PARTS) return MJ_E_INVAL;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < n; ++i) {
        uint16_t id = el[i].id;
        if (el[i].kind < MJ_ELEM_PIXEL || el[i].kind > MJ_ELEM_MEMORY_TX || el[i].pass > 1) return MJ_E_INVAL;
        if (seen[id >> 3] & (1u << (id & 7u))) return MJ_E_INVAL; /* duplicate element */
        seen[id >> 3] = (uint8_t)(seen[id >> 3] | (1u << (id & 7u)));
    }
    memset(&s, 0, sizeof s);
    for (i = 0; i < n; ++i) {
        if (el[i].pass) ++s.good;
        else ++s.not_yet;
        if (labels != NULL) labels[i] = el[i].pass ? MJ_CREDIT_SUCCESS : MJ_CREDIT_NOT_YET;
    }
    s.total = n;
    s.score_bp = bp(s.good, n);
    s.all_pass = (s.good == n);
    *out = s;
    return MJ_OK;
}

void mj_score_accum_add(mj_score_accum *a, uint32_t parts, uint32_t success) {
    if (success > parts) success = parts;
    a->parts += parts;
    a->success += success;
}

uint32_t mj_score_accum_bp(const mj_score_accum *a) { return bp(a->success, a->parts); }
