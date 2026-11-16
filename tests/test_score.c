/* MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_score.h"
#include "mj_test.h"

int main(void) {
    mj_maze_segment s[8];
    mj_credit lab[8];
    mj_maze_score m;
    mj_circuit_element e[8];
    mj_circuit_score c;
    mj_score_accum acc = {0, 0};
    int i;

    /* JD's example: clears one pit, falls into the next. */
    s[0].type = MJ_SEG_PIT; s[0].result = MJ_RUN_CLEARED;
    s[1].type = MJ_SEG_PIT; s[1].result = MJ_RUN_FAILED;
    s[2].type = MJ_SEG_OBSTACLE; s[2].result = MJ_RUN_NOT_REACHED;
    s[3].type = MJ_SEG_GOAL; s[3].result = MJ_RUN_NOT_REACHED;
    CHECK_ST(mj_score_maze(s, 4, lab, &m), MJ_OK);
    CHECK_EQ(m.success, 1u);
    CHECK_EQ(m.not_yet, 1u);
    CHECK_EQ(m.ungraded, 2u);
    CHECK_EQ(m.score_bp, 2500u);
    CHECK(!m.escaped);
    CHECK(lab[0] == MJ_CREDIT_SUCCESS && lab[1] == MJ_CREDIT_NOT_YET && lab[2] == MJ_CREDIT_UNGRADED);

    /* a clean escape */
    for (i = 0; i < 4; ++i) s[i].result = MJ_RUN_CLEARED;
    CHECK_ST(mj_score_maze(s, 4, lab, &m), MJ_OK);
    CHECK_EQ(m.score_bp, 10000u);
    CHECK(m.escaped);

    /* all cleared but the corridor ended before the goal (time ran out) */
    s[3].result = MJ_RUN_NOT_REACHED;
    CHECK_ST(mj_score_maze(s, 4, lab, &m), MJ_OK);
    CHECK_EQ(m.score_bp, 7500u);
    CHECK_EQ(m.not_yet, 0u);
    CHECK(!m.escaped);

    /* failing at the very first segment: no credit, one "not yet", no penalty */
    s[0].result = MJ_RUN_FAILED;
    s[1].result = s[2].result = s[3].result = MJ_RUN_NOT_REACHED;
    CHECK_ST(mj_score_maze(s, 4, lab, &m), MJ_OK);
    CHECK_EQ(m.score_bp, 0u);
    CHECK_EQ(m.not_yet, 1u);

    /* malformed runs are rejected, not guessed at */
    s[0].result = MJ_RUN_FAILED; s[1].result = MJ_RUN_CLEARED;
    CHECK_ST(mj_score_maze(s, 2, lab, &m), MJ_E_INVAL);
    s[0].result = MJ_RUN_FAILED; s[1].result = MJ_RUN_FAILED;
    CHECK_ST(mj_score_maze(s, 2, lab, &m), MJ_E_INVAL);
    s[0].result = MJ_RUN_NOT_REACHED; s[1].result = MJ_RUN_CLEARED;
    CHECK_ST(mj_score_maze(s, 2, lab, &m), MJ_E_INVAL);
    s[0].result = 7;
    CHECK_ST(mj_score_maze(s, 1, lab, &m), MJ_E_INVAL);
    s[0].result = MJ_RUN_CLEARED; s[0].type = 0;
    CHECK_ST(mj_score_maze(s, 1, lab, &m), MJ_E_INVAL);
    CHECK_ST(mj_score_maze(s, 0, lab, &m), MJ_E_INVAL);
    CHECK_ST(mj_score_maze(NULL, 1, lab, &m), MJ_E_INVAL);
    CHECK_ST(mj_score_maze(s, MJ_SCORE_MAX_PARTS + 1, lab, &m), MJ_E_INVAL);

    /* JD's circuit example: a pixel screen, three pixels right, the fourth wrong */
    for (i = 0; i < 4; ++i) { e[i].id = (uint16_t)i; e[i].kind = MJ_ELEM_PIXEL; e[i].pass = 1; }
    e[3].pass = 0;
    CHECK_ST(mj_score_circuit(e, 4, lab, &c), MJ_OK);
    CHECK_EQ(c.good, 3u);
    CHECK_EQ(c.not_yet, 1u);
    CHECK_EQ(c.score_bp, 7500u);
    CHECK(!c.all_pass);
    CHECK(lab[3] == MJ_CREDIT_NOT_YET && lab[0] == MJ_CREDIT_SUCCESS);
    /* ... or the fault is in the memory transmission */
    e[3].pass = 1;
    e[4].id = 4; e[4].kind = MJ_ELEM_MEMORY_TX; e[4].pass = 0;
    CHECK_ST(mj_score_circuit(e, 5, lab, &c), MJ_OK);
    CHECK_EQ(c.score_bp, 8000u);
    CHECK(lab[4] == MJ_CREDIT_NOT_YET);
    e[4].pass = 1;
    CHECK_ST(mj_score_circuit(e, 5, lab, &c), MJ_OK);
    CHECK(c.all_pass);
    e[4].id = 2; /* duplicate element id */
    CHECK_ST(mj_score_circuit(e, 5, lab, &c), MJ_E_INVAL);
    e[4].id = 4; e[4].pass = 2;
    CHECK_ST(mj_score_circuit(e, 5, lab, &c), MJ_E_INVAL);
    e[4].pass = 1; e[4].kind = 9;
    CHECK_ST(mj_score_circuit(e, 5, lab, &c), MJ_E_INVAL);

    /* totals across runs, integer math, deterministic */
    mj_score_accum_add(&acc, 4, 1);
    mj_score_accum_add(&acc, 4, 3);
    mj_score_accum_add(&acc, 3, 3);
    CHECK_EQ(mj_score_accum_bp(&acc), 6363u); /* 7/11, rounded down */
    mj_score_accum_add(&acc, 2, 9); /* success above parts is clamped */
    CHECK_EQ(acc.success, 9u);
    {
        mj_score_accum big = {UINT64_MAX / 2, UINT64_MAX / 4};
        CHECK(mj_score_accum_bp(&big) >= 4999u && mj_score_accum_bp(&big) <= 5000u);
    }
    return mj_t_done("test_score");
}
