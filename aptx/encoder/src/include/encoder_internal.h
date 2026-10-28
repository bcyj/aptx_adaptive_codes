#ifndef AA_ENCODER_INTERNAL_H
#define AA_ENCODER_INTERNAL_H
#include "aa_internal.h"
#include <aptx/encoder/encoder.h>
typedef struct {
    uint8_t *data;
    size_t len, cap;
    unsigned used;
    int error;
} AABits;
typedef struct {
    AAParams p;
    size_t packet_len;
    uint8_t *carry;
    size_t carry_len, carry_cap;
    AABits payload;
    uint8_t prev[16];
    int has_prev, open;
    uint8_t carry_storage[8192], payload_storage[8192];
} AAPacker;
typedef struct {
    uint32_t peak[16];
    int sf[16], need[16], caps[16], out[16];
} AAAllocScratch;
typedef struct {
    uint32_t nominal, target, a, ap, bq, bp;
} AARate;
struct aptx_aa_encoder {
    int owned;
    AAParams p;
    AAEngine engine;
    AAAllocScratch allocation_scratch;
    AAPacker pack;
    int32_t frame[AA_BLOCKS * AA_WIDTH], codes[AA_BLOCKS * AA_WIDTH];
    size_t frame_len;
    int64_t pad_hist, pad1, pad2, pending_pad, ov_hist, ov_prev;
    int has_pending, failed;
    uint32_t p_est;
    AARate rate;
    size_t carry_in, packet_blocks;
};

/* Implements aa_analyse. */
void aa_analyse(AAEngine *, const int32_t *, int32_t *);

/* Implements aa_code. */
void aa_code(AAEngine *, const int32_t *, const uint8_t *, int32_t *, int32_t *);

/* Implements aa_pool. */
uint32_t aa_pool(AAParams p, uint32_t target);

/* Implements aa_rate_range. */
static inline void aa_rate_range(AAParams p, uint32_t *lo, uint32_t *hi) {
    *lo = p.variant == 3 ? 90000 : 200000;
    *hi = p.variant == 3 ? 1000000 : 600000;
}

/* Implements aa_allocate. */
uint32_t aa_allocate(AAParams p, uint32_t target, uint32_t nominal, const int32_t *x, size_t n,
                     uint8_t *bits, AAAllocScratch *scratch);

/* Implements aa_encoder_init. */
int aa_encoder_init(aptx_aa_encoder *, unsigned, uint32_t, uint32_t, size_t);

/* Implements aa_pack_init. */
int aa_pack_init(AAPacker *p, AAParams params, size_t packet_len);

/* Implements aa_pack_reset. */
static inline void aa_pack_reset(AAPacker *p, AAParams params, size_t packet_len) {
    p->p = params;
    p->packet_len = packet_len;
    p->carry_len = 0;
    p->payload.len = 0;
    p->payload.used = 0;
    p->payload.error = 0;
    p->has_prev = 0;
    p->open = 0;
}

/* Implements aa_pack_begin. */
void aa_pack_begin(AAPacker *p);

/* Implements aa_pack_frame. */
int aa_pack_frame(AAPacker *p, const uint8_t *bits, const int32_t *codes, size_t n);

/* Implements aa_pack_finish. */
int aa_pack_finish(AAPacker *p, uint8_t *out, size_t *pad);

/* Implements aa_bit_len. */
static inline size_t aa_bit_len(const AABits *b) {
    return b->len * 8 - (b->used ? 8 - b->used : 0);
}

/* Implements aa_encoder_reset_variant. */
int aa_encoder_reset_variant(aptx_aa_encoder *e, unsigned variant, uint32_t rate, uint32_t bitrate,
                             size_t packet_len);

#endif
