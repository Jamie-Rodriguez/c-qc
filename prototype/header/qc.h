/*
 * PROTOTYPE. Throwaway. Not the real qc.h.
 *
 * This header exists to judge the ergonomics of the draw-based API before the
 * engine exists. The engine behind it (qc_stub.c) runs in-process, uses rand(),
 * and does not shrink, isolate, replay, or persist anything.
 *
 * Strict ISO C99. No _Generic, no VLAs, no compiler extensions.
 */
#ifndef QC_H
#define QC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- The test case handle ------------------------------------------------ */

/* One executing test case. A property receives it and draws values from it. */
typedef struct qc qc_t;

/* ---- Defining a property ------------------------------------------------- */

/*
 * A property is a function `void body(qc_t *qc)`. QC_PROPERTY defines one and
 * fixes the handle's name to `qc`, which is what QC_ASSERT and QC_ASSUME use.
 * The identifier is the property's name in reports and in the failure database.
 *
 *     QC_PROPERTY(addition_commutes) {
 *         int32_t a = qc_int32(qc, INT32_MIN, INT32_MAX);
 *         int32_t b = qc_int32(qc, INT32_MIN, INT32_MAX);
 *         QC_ASSERT(a + b == b + a);
 *     }
 *
 * The identifier names a one-element array so it decays to a pointer wherever
 * it is used: `qc_check(addition_commutes)` and `QC_MAIN(addition_commutes)`
 * both work without `&`.
 */
typedef struct qc_property {
    const char *name;
    void (*body)(qc_t *qc);
} qc_property_t;

#define QC_PROPERTY(id)                                                    \
    static void qc_body_##id(qc_t *qc);                                    \
    static const qc_property_t id[1] = { { #id, qc_body_##id } };          \
    static void qc_body_##id(qc_t *qc)

/* Fails the test case and leaves the property. */
#define QC_ASSERT(cond)                                                    \
    do {                                                                   \
        if (!(cond)) {                                                     \
            qc_fail(qc, __FILE__, __LINE__, "QC_ASSERT(" #cond ")");       \
            return;                                                        \
        }                                                                  \
    } while (0)

/* Fails the test case with a message and leaves the property. */
#define QC_FAIL(message)                                                   \
    do {                                                                   \
        qc_fail(qc, __FILE__, __LINE__, (message));                        \
        return;                                                            \
    } while (0)

/* Discards the test case (it counts as neither pass nor fail) and leaves. */
#define QC_ASSUME(cond)                                                    \
    do {                                                                   \
        if (!(cond)) {                                                     \
            qc_skip(qc, "QC_ASSUME(" #cond ")");                           \
            return;                                                        \
        }                                                                  \
    } while (0)

/* The functions behind the macros. Callable directly from helper functions
 * that do not have the `qc` name in scope, though they cannot `return` for
 * the caller: after qc_fail the property must still leave on its own. */
void qc_fail(qc_t *qc, const char *file, int line, const char *what);
void qc_skip(qc_t *qc, const char *what);

/* ---- Drawing values ------------------------------------------------------ */

/* Names the next draw. Reports print `name = value` instead of `#3 = value`.
 * Applies to the next draw only; for a collection, to the whole collection. */
void qc_label(qc_t *qc, const char *name);

/* An integer in [min, max], both inclusive. Shrinks towards 0 (or towards
 * the bound nearest 0 when 0 is outside the range). */
int32_t qc_int32(qc_t *qc, int32_t min, int32_t max);

/* A boolean. Shrinks towards false. */
bool qc_bool(qc_t *qc);

/* A byte string with a length in [min_len, max_len]. The buffer belongs to
 * the test case and is freed when the property returns; the property may
 * modify it in place. Shrinks towards shorter, then towards zero bytes. */
typedef struct qc_span {
    uint8_t *data;
    size_t len;
} qc_span_t;

qc_span_t qc_bytes(qc_t *qc, size_t min_len, size_t max_len);

/* ---- Drawing collections ------------------------------------------------- */

/*
 * A collection is a loop. qc_list opens one with a length in [min, max];
 * qc_more says whether to draw another element. `len` is the number of
 * elements drawn so far, so inside the loop it is the index of the element
 * being drawn (the usual `xs[len++] = ...` append idiom), and after the loop
 * it is the count.
 *
 *     int32_t xs[32];
 *     qc_list_t list = qc_list(qc, 0, 32);
 *     while (qc_more(&list)) {
 *         xs[list.len] = qc_int32(qc, -100, 100);
 *     }
 *     sort(xs, list.len);
 *
 * Every element is preceded by one recorded "more?" choice, forced to true
 * below min and forced to false at max, so the shrinker can delete elements
 * and the sequence stays aligned. Lists nest freely.
 */
typedef struct qc_list {
    qc_t *qc;
    size_t min;
    size_t max;
    size_t len;
    bool started;
} qc_list_t;

qc_list_t qc_list(qc_t *qc, size_t min, size_t max);
bool qc_more(qc_list_t *list);

/* ---- Running ------------------------------------------------------------- */

/*
 * From a user-written main:
 *
 *     int main(int argc, char **argv) {
 *         qc_init(argc, argv);
 *         qc_check(addition_commutes);
 *         qc_check(sort_is_idempotent);
 *         return qc_finish();
 *     }
 *
 * qc_init must see argc and argv because the worker is this same binary
 * re-executed in worker mode (ADR-0001); it also reads --qc-* options.
 * qc_check runs one property and returns whether it passed. qc_finish prints
 * the summary and returns the process exit status.
 */
void qc_init(int argc, char **argv);
bool qc_check(const qc_property_t *property);
int qc_finish(void);

/* The zero-boilerplate path. Lists the properties to run, in order. */
#define QC_MAIN(...)                                                       \
    int main(int argc, char **argv)                                        \
    {                                                                      \
        static const qc_property_t *const qc_properties[] = { __VA_ARGS__ }; \
        size_t qc_i;                                                       \
        qc_init(argc, argv);                                               \
        for (qc_i = 0; qc_i < sizeof qc_properties / sizeof qc_properties[0]; qc_i++) { \
            qc_check(qc_properties[qc_i]);                                 \
        }                                                                  \
        return qc_finish();                                                \
    }

#endif /* QC_H */
