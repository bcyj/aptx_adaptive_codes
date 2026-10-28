#include <aptx/decoder/decoder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../tools/cli/host_allocator.h"
#define CHECK(v)                                                                                   \
    do {                                                                                           \
        if (!(v)) {                                                                                \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static uint32_t rng = 1;

/* Implements random32. */
static uint32_t random32(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

/* Implements main. */
int main(void) {
    unsigned variant, run;
    size_t examined = 0;
    int32_t *pcm = (int32_t *)malloc(1048576 * sizeof(*pcm));
    CHECK(pcm);
    CHECK(!aptx_aa_decoder_create_variant(2, 48000));
    CHECK(!aptx_aa_decoder_create_variant(4, 96000));
    for (variant = 0; variant <= 4; variant++) {
        aptx_aa_dec *d;
        unsigned type;
        unsigned char b[1032];
        if (variant == 2)
            continue;
        d = aptx_aa_decoder_create_variant_with_allocator(variant, 48000, &example_allocator);
        CHECK(d);
        type = variant == 0 ? 1 : (variant == 1 ? 2 : variant);
        for (run = 0; run < 1000; run++) {
            size_t bytes = 12 + 4 * (random32() % 256), i, n, needed;
            int err;
            CHECK(aptx_aa_decoder_reset(d) == 0);
            for (i = 0; i < bytes; i++)
                b[i] = (unsigned char)(random32() >> 24);
            b[0] = 0;
            b[1] = (unsigned char)(type | ((run & 1) ? 128 : 0));
            b[2] = (unsigned char)(bytes / 4 - 3);

            if (run % 4)
                b[3] = (unsigned char)((b[3] & 7) | 128 | ((variant == 1 ? 11 : 10) << 3));
            err = aptx_aa_decoder_push(d, b, bytes, pcm, 1048576, &n);
            CHECK(err == 0 || err == APTX_AA_BAD_STREAM || err == APTX_AA_UNSUPPORTED_STREAM);
            if (err)
                continue;
            CHECK(n == 0);
            err = aptx_aa_decoder_finish(d, NULL, 0, &needed);
            CHECK(err == APTX_AA_OUTPUT_TOO_SMALL || err == APTX_AA_BAD_STREAM ||
                  err == APTX_AA_UNSUPPORTED_STREAM);
            if (err != APTX_AA_OUTPUT_TOO_SMALL)
                continue;
            CHECK(needed <= 1048576);
            err = aptx_aa_decoder_finish(d, pcm, 1048576, &n);
            CHECK(err == 0 && n == needed);
            for (i = 0; i < n; i++)
                CHECK(pcm[i] >= -8388608 && pcm[i] <= 8388607);
            CHECK(aptx_aa_decoder_finish(d, pcm, 1048576, &n) == 0 && n == 0);
            examined++;
        }
        aptx_aa_decoder_destroy(d);
    }
    free(pcm);
    printf("PASS 4000 malformed cases; %u bounded decodes\n", (unsigned)examined);
    return 0;
}
