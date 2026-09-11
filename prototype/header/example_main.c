/*
 * PROTOTYPE example: the QC_MAIN path.
 *
 * Properties about a small sort and a binary search. binary_search has a
 * deliberate bug (it never looks at the last element) so one property fails
 * and the report shape can be judged.
 */
#include "qc.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LEN 32

static void insertion_sort(int32_t *xs, size_t n, bool descending)
{
    size_t i;
    for (i = 1; i < n; i++) {
        int32_t key = xs[i];
        size_t j = i;
        while (j > 0 && (descending ? xs[j - 1] < key : xs[j - 1] > key)) {
            xs[j] = xs[j - 1];
            j--;
        }
        xs[j] = key;
    }
}

/* Deliberately wrong: the initial `hi` excludes the last element. */
static bool binary_search(const int32_t *xs, size_t n, int32_t key)
{
    size_t lo = 0;
    size_t hi = n - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (xs[mid] == key) {
            return true;
        }
        if (xs[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

QC_PROPERTY(sort_is_idempotent)
{
    int32_t xs[MAX_LEN];
    int32_t once[MAX_LEN];
    qc_list_t list;
    bool descending;

    qc_label(qc, "xs");
    list = qc_list(qc, 0, MAX_LEN);
    while (qc_more(&list)) {
        xs[list.len] = qc_int32(qc, -1000, 1000);
    }
    qc_label(qc, "descending");
    descending = qc_bool(qc);

    insertion_sort(xs, list.len, descending);
    memcpy(once, xs, list.len * sizeof xs[0]);
    insertion_sort(xs, list.len, descending);

    QC_ASSERT(memcmp(once, xs, list.len * sizeof xs[0]) == 0);
}

QC_PROPERTY(sort_orders_adjacent_pairs)
{
    int32_t xs[MAX_LEN];
    qc_list_t list;
    size_t i;

    qc_label(qc, "xs");
    list = qc_list(qc, 0, MAX_LEN);
    while (qc_more(&list)) {
        xs[list.len] = qc_int32(qc, INT32_MIN, INT32_MAX);
    }

    insertion_sort(xs, list.len, false);
    for (i = 1; i < list.len; i++) {
        QC_ASSERT(xs[i - 1] <= xs[i]);
    }
}

QC_PROPERTY(binary_search_finds_present_element)
{
    int32_t xs[MAX_LEN];
    qc_list_t list;
    int32_t index;

    qc_label(qc, "xs");
    list = qc_list(qc, 0, MAX_LEN);
    while (qc_more(&list)) {
        xs[list.len] = qc_int32(qc, 0, 100);
    }
    QC_ASSUME(list.len > 0);
    insertion_sort(xs, list.len, false);

    /* A dependent draw: the index range depends on the length drawn above. */
    qc_label(qc, "index");
    index = qc_int32(qc, 0, (int32_t) list.len - 1);

    QC_ASSERT(binary_search(xs, list.len, xs[index]));
}

QC_PROPERTY(abs_is_non_negative)
{
    int32_t x = qc_int32(qc, INT32_MIN, INT32_MAX);
    QC_ASSUME(x != INT32_MIN);
    QC_ASSERT(abs(x) >= 0);
}

QC_MAIN(sort_is_idempotent,
        sort_orders_adjacent_pairs,
        binary_search_finds_present_element,
        abs_is_non_negative)
