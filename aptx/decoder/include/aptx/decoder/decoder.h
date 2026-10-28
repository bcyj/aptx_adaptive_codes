#ifndef APTX_AA_DECODER_H
#define APTX_AA_DECODER_H
#include <aptx/common/memory.h>
#include <aptx/common/types.h>
#ifndef APTX_AA_DECODER_FIXED_POINT
#define APTX_AA_DECODER_FIXED_POINT 1
#endif
#if APTX_AA_DRIVER_RESOURCES && !APTX_AA_DECODER_FIXED_POINT
#error "Driver resources require fixed-point decoder synthesis"
#endif
#if APTX_AA_DECODER_FIXED_POINT != 0 && APTX_AA_DECODER_FIXED_POINT != 1
#error "APTX_AA_DECODER_FIXED_POINT must be 0 or 1"
#endif
typedef struct aptx_aa_dec aptx_aa_dec;
#ifdef __cplusplus
extern "C" {
#endif

/* Creates a decoder for a public configuration using the default allocator. */
aptx_aa_dec *aptx_aa_decoder_create(const aptx_aa_config *);

/* Creates a decoder for an explicit internal variant using the default allocator. */
aptx_aa_dec *aptx_aa_decoder_create_variant(unsigned variant, uint32_t sample_rate);

/* Creates a decoder for a public configuration using a host allocator. */
aptx_aa_dec *aptx_aa_decoder_create_with_allocator(const aptx_aa_config *,
                                                   const aptx_aa_allocator *);

/* Creates a decoder for an explicit internal variant using a host allocator. */
aptx_aa_dec *aptx_aa_decoder_create_variant_with_allocator(unsigned variant, uint32_t sample_rate,
                                                           const aptx_aa_allocator *);

/* Returns the workspace capacity required for in-place decoder initialization. */
size_t aptx_aa_decoder_workspace_bytes(void);

/* Initializes a decoder for a public configuration in caller-owned memory. */
int aptx_aa_decoder_init_in_memory(void *memory, size_t bytes, const aptx_aa_config *,
                                   aptx_aa_dec **out);

/* Initializes an explicit internal variant in caller-owned memory. */
int aptx_aa_decoder_init_variant_in_memory(void *memory, size_t bytes, unsigned variant,
                                           uint32_t sample_rate, aptx_aa_dec **out);

/* Destroys a decoder and releases its owned workspace. */
void aptx_aa_decoder_destroy(aptx_aa_dec *);

/* Clears prediction, synthesis, and cross-packet state for a new stream. */
int aptx_aa_decoder_reset(aptx_aa_dec *);

/* Decodes every complete frame in one transport packet to interleaved PCM. */
int aptx_aa_decoder_push(aptx_aa_dec *, const uint8_t *packet, size_t bytes, int32_t *pcm,
                         size_t capacity, size_t *samples);

/* Drains cross-packet state and the synthesis tail at the end of a stream. */
int aptx_aa_decoder_finish(aptx_aa_dec *, int32_t *pcm, size_t capacity, size_t *samples);

/* Returns the analysis and synthesis delay for the current configuration. */
unsigned aptx_aa_decoder_delay(const aptx_aa_dec *);

/* Reports whether this build uses the fixed-point synthesis backend. */
int aptx_aa_decoder_fixed_point(void);

#ifdef __cplusplus
}
#endif

#endif
