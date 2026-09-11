/*
 * PROTOTYPE example: qc_check from a user-written main.
 *
 * Properties about a run-length encoder. The encoder has a deliberate bug:
 * a run longer than 255 bytes has its count truncated to one byte.
 */
#include "qc.h"

#include <stdio.h>
#include <string.h>

#define MAX_INPUT 1024

/* Encodes as (count, byte) pairs. Bug: count is truncated to a byte. */
static size_t rle_encode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i = 0;
    size_t o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && in[i + run] == in[i]) {
            run++;
        }
        out[o++] = (uint8_t) run;
        out[o++] = in[i];
        i += run;
    }
    return o;
}

static size_t rle_decode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i;
    size_t o = 0;
    for (i = 0; i + 1 < n; i += 2) {
        size_t count = in[i];
        memset(out + o, in[i + 1], count);
        o += count;
    }
    return o;
}

QC_PROPERTY(rle_round_trips)
{
    uint8_t encoded[2 * MAX_INPUT];
    uint8_t decoded[MAX_INPUT];
    qc_span_t input;
    size_t encoded_len;
    size_t decoded_len;

    qc_label(qc, "input");
    input = qc_bytes(qc, 0, MAX_INPUT);

    encoded_len = rle_encode(input.data, input.len, encoded);
    decoded_len = rle_decode(encoded, encoded_len, decoded);

    QC_ASSERT(decoded_len == input.len);
    QC_ASSERT(memcmp(decoded, input.data, input.len) == 0);
}

QC_PROPERTY(rle_round_trips_a_single_run)
{
    uint8_t input[MAX_INPUT];
    uint8_t encoded[2 * MAX_INPUT];
    uint8_t decoded[MAX_INPUT];
    int32_t byte;
    int32_t run;
    size_t encoded_len;
    size_t decoded_len;

    qc_label(qc, "byte");
    byte = qc_int32(qc, 0, 255);
    qc_label(qc, "run");
    run = qc_int32(qc, 1, MAX_INPUT);
    memset(input, byte, (size_t) run);

    encoded_len = rle_encode(input, (size_t) run, encoded);
    decoded_len = rle_decode(encoded, encoded_len, decoded);

    if (decoded_len != (size_t) run) {
        QC_FAIL("decoded length differs from the run length");
    }
    QC_ASSERT(memcmp(decoded, input, (size_t) run) == 0);
}

QC_PROPERTY(rle_never_more_than_doubles)
{
    uint8_t encoded[2 * MAX_INPUT];
    qc_span_t input;

    qc_label(qc, "input");
    input = qc_bytes(qc, 1, MAX_INPUT);

    QC_ASSERT(rle_encode(input.data, input.len, encoded) <= 2 * input.len);
}

int main(int argc, char **argv)
{
    qc_init(argc, argv);

    qc_check(rle_round_trips);
    qc_check(rle_never_more_than_doubles);
    if (!qc_check(rle_round_trips_a_single_run)) {
        printf("(a user main can react to a single property's verdict)\n");
    }

    return qc_finish();
}
