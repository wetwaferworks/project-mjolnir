/* The keep-or-roll-back decision for rounds played outside Mjolnir. See
 * mj_hemi_gate.h.
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#include "mjolnir/mj_hemi_gate.h"
#include "mjolnir/mj_classroom.h"

#include <stdio.h>
#include <string.h>

#define WHY(...) (snprintf(out->why, sizeof out->why, __VA_ARGS__), MJ_E_INVAL)

static int same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, MJ_HEMI_FINGERPRINT) == 0; }

/* The most likely move, ties to the lower index; probabilities of illegal
 * moves are stored as 0, so this is the legal pick. */
static uint32_t top(const uint16_t *p, uint32_t na) {
    uint32_t a, best = 0;
    for (a = 1; a < na; ++a)
        if (p[a] > p[best]) best = a;
    return best;
}

static uint32_t play_bp(const mj_hemi_set *s) {
    uint32_t i, cleared = 0;
    for (i = 0; i < s->n; ++i) cleared += s->r[i].grade == MJ_CREDIT_SUCCESS;
    return s->n ? (uint32_t)((uint64_t)cleared * 10000u / s->n) : 0u;
}

/* The holdout set the model played (it is the actor); NULL when there is none. */
static const mj_hemi_set *played_by(const mj_hemi_gate_in *in, const uint8_t *m) {
    uint32_t k;
    for (k = 0; k < in->n_holdout; ++k)
        if (same(in->holdout[k]->h.actor, m)) return in->holdout[k];
    return NULL;
}

static const mj_hemi_set *judged_by(const mj_hemi_gate_in *in, const uint8_t *actor, const uint8_t *watcher) {
    uint32_t k;
    for (k = 0; k < in->n_holdout; ++k)
        if (same(in->holdout[k]->h.actor, actor) && same(in->holdout[k]->h.observer, watcher)) return in->holdout[k];
    return NULL;
}

mj_status mj_hemi_gate_decide(const mj_hemi_gate_in *in, mj_hemi_gate_out *out) {
    const mj_hemi_set *h0, *pp, *pc, *po = NULL;
    uint32_t k, i, a, na, n;
    uint8_t *grades;
    size_t gbytes;
    if (out == NULL) return MJ_E_INVAL;
    memset(out, 0, sizeof *out);
    if (in == NULL || in->budget == NULL) return WHY("no input");
    if (in->kind < MJ_HEMI_GATE_ACTOR || in->kind > MJ_HEMI_GATE_SWAP) return WHY("unknown kind");
    if (in->n_lessons == 0 || in->n_lessons > MJ_HEMI_GATE_MAX_SETS) return WHY("needs 1 to %u lesson sets", MJ_HEMI_GATE_MAX_SETS);
    if (in->n_holdout == 0 || in->n_holdout > MJ_HEMI_GATE_MAX_SETS) return WHY("needs 1 to %u holdout sets", MJ_HEMI_GATE_MAX_SETS);
    if (same(in->parent, in->candidate)) return WHY("the candidate is the parent");
    if (in->kind == MJ_HEMI_GATE_SWAP && (same(in->other, in->parent) || same(in->other, in->candidate)))
        return WHY("the second parent must differ from the parent and the candidate");
    h0 = in->holdout[0];
    if (h0 == NULL || mj_hemi_header_check(&h0->h) != MJ_OK) return WHY("holdout set 1 is not valid");
    na = h0->h.n_actions;
    n = h0->n;
    if (n == 0) return WHY("the holdout sets are empty");

    /* every set: valid, the right purpose, the same world */
    for (k = 0; k < in->n_lessons + in->n_holdout; ++k) {
        const int lesson = k < in->n_lessons;
        const mj_hemi_set *s = lesson ? in->lessons[k] : in->holdout[k - in->n_lessons];
        if (s == NULL || mj_hemi_header_check(&s->h) != MJ_OK) return WHY("set %u is not valid", k + 1);
        if (s->h.purpose != (lesson ? MJ_HEMI_LESSON : MJ_HEMI_HOLDOUT))
            return WHY("%s set %u is marked as a %s set", lesson ? "lesson" : "holdout",
                       lesson ? k + 1 : k + 1 - in->n_lessons, lesson ? "holdout" : "lesson");
    }
    for (k = 0; k < in->n_lessons + in->n_holdout; ++k) {
        const int lesson = k < in->n_lessons;
        const mj_hemi_set *s = lesson ? in->lessons[k] : in->holdout[k - in->n_lessons];
        if (s->h.world != h0->h.world || s->h.n_features != h0->h.n_features || s->h.n_actions != na)
            return WHY("the sets are not from the same world");
        if (lesson && s->h.seed == h0->h.seed) return WHY("a lesson set uses the holdout seed");
    }

    /* the lessons are bound to the models in the decision */
    for (k = 0; k < in->n_lessons; ++k) {
        const mj_hemi_header *h = &in->lessons[k]->h;
        if (in->kind == MJ_HEMI_GATE_ACTOR && !same(h->actor, in->parent))
            return WHY("lesson set %u was not played by the parent", k + 1);
        if (in->kind == MJ_HEMI_GATE_WATCHER &&
            (!same(h->observer, in->parent) || !same(h->actor, in->lessons[0]->h.actor)))
            return WHY("lesson set %u was not watched by the parent, or has another actor", k + 1);
        if (in->kind == MJ_HEMI_GATE_SWAP &&
            !((same(h->actor, in->parent) && same(h->observer, in->other)) ||
              (same(h->actor, in->other) && same(h->observer, in->parent))))
            return WHY("lesson set %u is not between the two parents", k + 1);
    }
    if (in->kind == MJ_HEMI_GATE_SWAP) {
        int first = 0, second = 0;
        for (k = 0; k < in->n_lessons; ++k) {
            first |= same(in->lessons[k]->h.actor, in->parent);
            second |= same(in->lessons[k]->h.actor, in->other);
        }
        if (!first || !second) return WHY("a swap needs a pass played by each parent");
    }

    /* the holdout sets: the same states, moves that match their distributions */
    gbytes = (size_t)n * MJ_HEMI_MAX_ACTIONS;
    grades = (uint8_t *)mj_balloc(in->budget, gbytes);
    if (grades == NULL) return MJ_E_LIMIT;
    for (k = 0; k < in->n_holdout; ++k) {
        const mj_hemi_set *s = in->holdout[k];
        if (s->n != n || s->h.seed != h0->h.seed) { mj_bfree(in->budget, grades, gbytes); return WHY("holdout set %u is not on the same states", k + 1); }
        for (i = 0; i < n; ++i) {
            const mj_hemi_record *r = &s->r[i], *r0 = &h0->r[i];
            uint8_t *g = &grades[(size_t)i * MJ_HEMI_MAX_ACTIONS + r->actor_move];
            if (r->episode != r0->episode || r->step != r0->step || memcmp(r->features, r0->features, MJ_HEMI_MAX_FEATURES) != 0) {
                mj_bfree(in->budget, grades, gbytes);
                return WHY("holdout set %u is not on the same states (step %u)", k + 1, i);
            }
            if (r->grade != MJ_CREDIT_SUCCESS && r->grade != MJ_CREDIT_NOT_YET) {
                mj_bfree(in->budget, grades, gbytes);
                return WHY("holdout set %u has an ungraded move (step %u)", k + 1, i);
            }
            if (r->actor_move != top(r->actor_probs, na) || r->verdict.choice != top(r->watcher_probs, na) ||
                (r->verdict.reason == MJ_HEMI_AGREE) != (r->verdict.choice == r->actor_move)) {
                mj_bfree(in->budget, grades, gbytes);
                return WHY("holdout set %u: a recorded move is not the one its distribution picks (step %u)", k + 1, i);
            }
            if (*g == 0) *g = r->grade;
            else if (*g != r->grade) {
                mj_bfree(in->budget, grades, gbytes);
                return WHY("holdout set %u: the answer key graded the same move two ways (step %u)", k + 1, i);
            }
        }
    }
    mj_bfree(in->budget, grades, gbytes);
    /* a model's own play is a function of the model: two sets played by it must agree */
    for (k = 0; k < in->n_holdout; ++k)
        for (i = k + 1; i < in->n_holdout; ++i)
            if (same(in->holdout[k]->h.actor, in->holdout[i]->h.actor))
                for (a = 0; a < n; ++a)
                    if (memcmp(in->holdout[k]->r[a].actor_probs, in->holdout[i]->r[a].actor_probs,
                               sizeof in->holdout[k]->r[a].actor_probs) != 0)
                        return WHY("holdout sets %u and %u disagree on the same model's play", k + 1, i + 1);

    /* the scores, worked out here */
    pp = played_by(in, in->parent);
    pc = played_by(in, in->candidate);
    if (pp == NULL) return WHY("no holdout set played by the parent");
    if (pc == NULL) return WHY("no holdout set played by the candidate");
    out->states = n;
    out->before_bp = play_bp(pp);
    out->after_bp = play_bp(pc);
    if (in->kind == MJ_HEMI_GATE_ACTOR) {
        out->keep = out->after_bp > out->before_bp;
    } else if (in->kind == MJ_HEMI_GATE_WATCHER) {
        const uint8_t *actor = in->lessons[0]->h.actor;
        const mj_hemi_set *jp = judged_by(in, actor, in->parent), *jc = judged_by(in, actor, in->candidate);
        mj_hemi_tally t;
        if (jp == NULL || jc == NULL) return WHY("no holdout set where the parent and the candidate watch the lesson's actor");
        mj_hemi_tally_set(jp, &t);
        out->judge_before_bp = mj_classroom_judge_bp(&t);
        mj_hemi_tally_set(jc, &t);
        out->judge_after_bp = mj_classroom_judge_bp(&t);
        out->keep = out->judge_after_bp > out->judge_before_bp && out->after_bp >= out->before_bp;
    } else {
        po = played_by(in, in->other);
        if (po == NULL) return WHY("no holdout set played by the second parent");
        out->other_bp = play_bp(po);
        out->keep = out->after_bp > out->before_bp && out->after_bp > out->other_bp;
    }
    return MJ_OK;
}
