/*
 * PROTOTYPE. Throwaway stub engine behind qc.h.
 *
 * In-process, rand()-based, no shrinking, no worker, no database. It exists
 * so the example properties compile and run, and so a failure prints the
 * drawn values in roughly the shape a real report would use.
 */
#include "qc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DRAWS 256
#define MAX_ALLOCS 64
#define MAX_LIST_DEPTH 8

struct draw {
    char label[32];
    char value[64];
};

struct qc {
    const char *property;
    unsigned case_number;
    int status; /* 0 running, 1 passed, 2 failed, 3 skipped */
    char failure[256];
    char pending_label[32];
    struct draw draws[MAX_DRAWS];
    size_t draw_count;
    void *allocs[MAX_ALLOCS];
    size_t alloc_count;
    char list_labels[MAX_LIST_DEPTH][32];
    size_t list_depth;
};

static unsigned g_seed = 1;
static unsigned g_cases = 100;
static unsigned g_failed_properties = 0;
static unsigned g_checked_properties = 0;

/* ---- recording -------------------------------------------------------- */

static struct draw *next_draw(qc_t *qc, const char *fallback_label)
{
    struct draw *d;
    if (qc->draw_count == MAX_DRAWS) {
        fprintf(stderr, "prototype stub: too many draws\n");
        exit(70);
    }
    d = &qc->draws[qc->draw_count++];
    if (qc->pending_label[0] != '\0') {
        strcpy(d->label, qc->pending_label);
        qc->pending_label[0] = '\0';
    } else {
        snprintf(d->label, sizeof d->label, "%s", fallback_label);
    }
    d->value[0] = '\0';
    return d;
}

static void finish_label(qc_t *qc, char *out, size_t out_size)
{
    if (qc->pending_label[0] != '\0') {
        snprintf(out, out_size, "%s", qc->pending_label);
        qc->pending_label[0] = '\0';
    } else {
        snprintf(out, out_size, "#%u", (unsigned) qc->draw_count);
    }
}

/* ---- public: outcome ---------------------------------------------------- */

void qc_fail(qc_t *qc, const char *file, int line, const char *what)
{
    if (qc->status != 0) {
        return;
    }
    qc->status = 2;
    snprintf(qc->failure, sizeof qc->failure, "%s:%d: %s", file, line, what);
}

void qc_skip(qc_t *qc, const char *what)
{
    if (qc->status != 0) {
        return;
    }
    qc->status = 3;
    snprintf(qc->failure, sizeof qc->failure, "%s", what);
}

/* ---- public: draws ------------------------------------------------------ */

void qc_label(qc_t *qc, const char *name)
{
    snprintf(qc->pending_label, sizeof qc->pending_label, "%s", name);
}

int32_t qc_int32(qc_t *qc, int32_t min, int32_t max)
{
    uint64_t span = (uint64_t) ((int64_t) max - (int64_t) min) + 1;
    uint64_t r = ((uint64_t) rand() << 31) ^ (uint64_t) rand();
    int32_t value = (int32_t) ((int64_t) min + (int64_t) (r % span));
    struct draw *d = next_draw(qc, "int32");
    snprintf(d->value, sizeof d->value, "%ld", (long) value);
    return value;
}

bool qc_bool(qc_t *qc)
{
    bool value = (rand() & 1) != 0;
    struct draw *d = next_draw(qc, "bool");
    snprintf(d->value, sizeof d->value, "%s", value ? "true" : "false");
    return value;
}

qc_span_t qc_bytes(qc_t *qc, size_t min_len, size_t max_len)
{
    qc_span_t span;
    struct draw *d;
    size_t i;
    size_t shown;
    char *p;

    span.len = min_len + (size_t) rand() % (max_len - min_len + 1);
    span.data = malloc(span.len ? span.len : 1);
    if (qc->alloc_count == MAX_ALLOCS) {
        fprintf(stderr, "prototype stub: too many allocations\n");
        exit(70);
    }
    qc->allocs[qc->alloc_count++] = span.data;
    for (i = 0; i < span.len; i++) {
        span.data[i] = (uint8_t) rand();
    }

    d = next_draw(qc, "bytes");
    shown = span.len < 12 ? span.len : 12;
    p = d->value;
    p += sprintf(p, "{");
    for (i = 0; i < shown; i++) {
        p += sprintf(p, "%s%02x", i ? " " : "", span.data[i]);
    }
    sprintf(p, "%s} (%u bytes)", shown < span.len ? " ..." : "",
            (unsigned) span.len);
    return span;
}

qc_list_t qc_list(qc_t *qc, size_t min, size_t max)
{
    qc_list_t list;
    list.qc = qc;
    list.min = min;
    list.max = max;
    list.len = 0;
    list.started = false;
    return list;
}

bool qc_more(qc_list_t *list)
{
    qc_t *qc = list->qc;
    bool more;

    if (!list->started) {
        /* The label given before the loop names the whole collection;
         * elements are reported as name[i]. */
        if (qc->list_depth == MAX_LIST_DEPTH) {
            fprintf(stderr, "prototype stub: lists nested too deep\n");
            exit(70);
        }
        finish_label(qc, qc->list_labels[qc->list_depth],
                     sizeof qc->list_labels[0]);
        qc->list_depth++;
        list->started = true;
        list->len = 0;
    } else {
        list->len++;
    }

    if (list->len < list->min) {
        more = true;
    } else if (list->len >= list->max) {
        more = false;
    } else {
        /* Geometric: keep going with probability 7/8. */
        more = (rand() % 8) != 0;
    }

    if (more) {
        snprintf(qc->pending_label, sizeof qc->pending_label, "%s[%u]",
                 qc->list_labels[qc->list_depth - 1], (unsigned) list->len);
    } else {
        qc->list_depth--;
        if (list->len == 0) {
            struct draw *d = next_draw(qc, qc->list_labels[qc->list_depth]);
            snprintf(d->value, sizeof d->value, "{} (0 elements)");
        }
    }
    return more;
}

/* ---- public: running ---------------------------------------------------- */

void qc_init(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--qc-seed=", 10) == 0) {
            g_seed = (unsigned) strtoul(argv[i] + 10, NULL, 10);
        } else if (strncmp(argv[i], "--qc-cases=", 11) == 0) {
            g_cases = (unsigned) strtoul(argv[i] + 11, NULL, 10);
        }
    }
    srand(g_seed);
}

static void reset_case(qc_t *qc, const char *property, unsigned n)
{
    size_t i;
    for (i = 0; i < qc->alloc_count; i++) {
        free(qc->allocs[i]);
    }
    memset(qc, 0, sizeof *qc);
    qc->property = property;
    qc->case_number = n;
}

static void print_report(const qc_t *qc, unsigned skipped)
{
    size_t i;
    printf("FAIL %s (case %u of %u, %u skipped)\n", qc->property,
           qc->case_number, g_cases, skipped);
    printf("  %s\n", qc->failure);
    for (i = 0; i < qc->draw_count; i++) {
        const struct draw *d = &qc->draws[i];
        printf("  %s = %s\n", d->label, d->value);
    }
}

bool qc_check(const qc_property_t *property)
{
    static qc_t qc;
    unsigned run = 0;
    unsigned skipped = 0;
    unsigned attempts = 0;

    g_checked_properties++;
    while (run < g_cases && attempts < g_cases * 10) {
        attempts++;
        reset_case(&qc, property->name, run + 1);
        property->body(&qc);
        if (qc.status == 3) {
            skipped++;
            continue;
        }
        if (qc.status == 2) {
            print_report(&qc, skipped);
            g_failed_properties++;
            reset_case(&qc, NULL, 0);
            return false;
        }
        run++;
    }
    reset_case(&qc, NULL, 0);
    if (run < g_cases) {
        printf("GAVE UP %s (%u of %u cases passed, %u skipped)\n",
               property->name, run, g_cases, skipped);
        g_failed_properties++;
        return false;
    }
    printf("ok   %s (%u cases, %u skipped)\n", property->name, run, skipped);
    return true;
}

int qc_finish(void)
{
    printf("%u properties, %u failed, seed %u\n", g_checked_properties,
           g_failed_properties, g_seed);
    return g_failed_properties == 0 ? 0 : 1;
}
