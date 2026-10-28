#ifndef AA_DECODER_TRACE_H
#define AA_DECODER_TRACE_H
#include <aptx/decoder/decoder.h>
/* Implements void. */
typedef void (*AADecodeTrace)(void *, const uint8_t *bits, const int32_t *codes,
                              const int32_t *recon, const int32_t *pred, const int32_t *subbands,
                              unsigned width);

/* Implements aa_decoder_set_trace. */
void aa_decoder_set_trace(aptx_aa_dec *, AADecodeTrace, void *);
#endif
