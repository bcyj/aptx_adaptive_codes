#ifndef AA_DECODER_INTERNAL_H
#define AA_DECODER_INTERNAL_H
#include "aa_decoder_trace.h"
#include "aa_synthesis.h"
#define AA_DECODE_BITS (APTX_AA_MAX_PACKET * 16)
#define AA_DECODE_BYTES ((AA_DECODE_BITS + 7) / 8)
#define AA_READ_END (-100)
typedef struct {
    int16_t child[2], symbol;
} AAHuffNode;
extern const AAHuffNode AA_DECODE_NODES[];
extern const unsigned AA_DECODE_NODE_COUNT;
extern const int AA_DECODE_ABS_ROOT[2], AA_DECODE_DIFF_ROOT[9], AA_DECODE_CODE_ROOT[5];

/* Implements aa_decode_verify_tables. */
int aa_decode_verify_tables(void);
typedef struct {
    const uint8_t *bits;
    size_t len, pos;
} AAReader;
struct aptx_aa_dec {
    int owned;
    AAParams p;
    AAEngine engine;
    AASynthesis synth;

    uint8_t pending[AA_DECODE_BYTES];
    uint8_t scratch_body[APTX_AA_MAX_PACKET], scratch_joined[AA_DECODE_BYTES];
    uint8_t scratch_raw[APTX_AA_MAX_PACKET];
    int32_t scratch_codes[32], scratch_bands[16], scratch_recon[16], scratch_pred[16];
    size_t pending_len;
    int has_pending, padded, finished;
    AADecodeTrace trace;
    void *trace_data;
};

/* Implements aa_decode_copy_bits. */
void aa_decode_copy_bits(uint8_t *, size_t, const uint8_t *, size_t, size_t);

/* Implements aa_decode_parse. */
int aa_decode_parse(aptx_aa_dec *, const uint8_t *, size_t, int, int32_t *, size_t, size_t *);

/* Implements aa_decode_split_packet. */
int aa_decode_split_packet(aptx_aa_dec *, const uint8_t *, size_t, uint8_t *, size_t *, size_t *,
                           int *);

/* Implements aa_reconstruct. */
void aa_reconstruct(AAEngine *, const uint8_t *, const int32_t *, int32_t *, int32_t *);

#endif
