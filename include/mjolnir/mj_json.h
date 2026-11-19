/* Mjolnir: a small JSON reader for PCHIE run logs.
 *
 * It reads one JSON text into a flat tree and answers the questions the
 * replay code asks the way Python does: object lookups take the last value
 * for a repeated key, values compare equal as Python's == would (1, 1.0 and
 * true are equal), and texts are written back as Python's
 * json.dumps(..., sort_keys=True, separators=(",", ":")) writes them.
 *
 * It is stricter than Python's json module in a few documented ways: UTF-8
 * only (no byte order mark, no UTF-16 or UTF-32 detection), no NaN or
 * Infinity literals, and nesting deeper than MJ_JSON_MAX_DEPTH is refused
 * (Python stops with a recursion error at about 1000).
 * MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works. */
#ifndef MJ_JSON_H
#define MJ_JSON_H
#include "mjolnir/mj_common.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MJ_JSON_MAX_DEPTH 512u
#define MJ_JSON_NONE 0xFFFFFFFFu

typedef enum mj_jkind {
    MJ_J_NULL = 0,
    MJ_J_FALSE,
    MJ_J_TRUE,
    MJ_J_INT,    /* an integer literal that fits int64 */
    MJ_J_BIGINT, /* an integer literal outside int64, kept as its digits */
    MJ_J_FLOAT,  /* a literal with a fraction or exponent */
    MJ_J_STR,
    MJ_J_ARR,
    MJ_J_OBJ
} mj_jkind;

typedef struct mj_jnode {
    uint32_t kind;
    uint32_t n;      /* children of an array or object (repeats counted) */
    uint32_t first;  /* first child, MJ_JSON_NONE if empty */
    uint32_t next;   /* next sibling, MJ_JSON_NONE at the end */
    uint32_t key_off, key_len; /* member name, for children of an object */
    uint32_t s_off, s_len;     /* string bytes (UTF-8), or the digits of a BIGINT */
    int64_t i;
    double f;
} mj_jnode;

typedef struct mj_json {
    mj_jnode *v;
    uint32_t nv, cap_v;
    char *t;
    uint32_t nt, cap_t;
    uint32_t max_nodes, max_text;
    mj_budget *b;
} mj_json;

/* A document that may hold up to max_nodes values and max_text bytes of
 * strings, charging its memory to b. */
void mj_json_init(mj_json *d, mj_budget *b, uint32_t max_nodes, uint32_t max_text);
void mj_json_free(mj_json *d);
void mj_json_reset(mj_json *d); /* forget the contents, keep the memory */

/* Parse one JSON text into d (appending to what is already there). *root is
 * the index of the new top value. MJ_E_INVAL for anything Python's json.loads
 * would refuse (and the stricter cases above), MJ_E_LIMIT past the ceilings. */
mj_status mj_json_parse(mj_json *d, const char *s, size_t n, uint32_t *root);

/* The last member called key, or MJ_JSON_NONE (also when obj is not an object). */
uint32_t mj_json_get(const mj_json *d, uint32_t obj, const char *key);
const char *mj_json_str(const mj_json *d, uint32_t v, size_t *len); /* NULL unless a string */
int mj_json_str_is(const mj_json *d, uint32_t v, const char *s);    /* a string equal to s */

/* Python semantics on values (v may be MJ_JSON_NONE for Python's None from a
 * missing key, which equals a JSON null). */
int mj_json_truthy(const mj_json *d, uint32_t v);
int mj_json_eq(const mj_json *da, uint32_t a, const mj_json *db, uint32_t b);
int mj_json_is_number(const mj_json *d, uint32_t v);   /* int, float or bool, as Python's numbers */
int mj_json_is_integer(const mj_json *d, uint32_t v);  /* isinstance(v, int): ints and bools */
int mj_json_hashable(const mj_json *d, uint32_t v);    /* not an array or object */
uint64_t mj_json_hash(const mj_json *d, uint32_t v);   /* equal values hash equal */
double mj_json_to_double(const mj_json *d, uint32_t v); /* a number as Python's float(v) */

/* One UTF-8 sequence at s (encoded surrogates allowed, as Python's
 * "surrogatepass"): its length, or 0 when it is not valid UTF-8. */
size_t mj_utf8_len(const unsigned char *s, size_t n, uint32_t *cp_out);

/* Copy the value at src_v of src into dst; *out is its index there. */
mj_status mj_json_copy(mj_json *dst, const mj_json *src, uint32_t src_v, uint32_t *out);

/* Text writer used by the dumps below. */
typedef struct mj_jout {
    char *p;
    size_t n, cap, max;
    int failed;
} mj_jout;
void mj_jout_init(mj_jout *o, size_t max);
void mj_jout_free(mj_jout *o);
void mj_jout_raw(mj_jout *o, const char *s, size_t n);
void mj_jout_cstr(mj_jout *o, const char *s);
void mj_jout_string(mj_jout *o, const char *s, size_t n); /* quoted, ensure_ascii */
void mj_jout_int(mj_jout *o, int64_t v);
void mj_jout_float(mj_jout *o, double v);                 /* Python's repr, Infinity, -Infinity */
/* A value as json.dumps(sort_keys=True, separators=(",", ":")) writes it;
 * MJ_JSON_NONE writes null. */
void mj_jout_value(mj_jout *o, const mj_json *d, uint32_t v);

#ifdef __cplusplus
}
#endif
#endif
