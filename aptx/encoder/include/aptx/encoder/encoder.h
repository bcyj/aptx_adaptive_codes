#ifndef APTX_AA_ENCODER_H
#define APTX_AA_ENCODER_H
#include <aptx/common/memory.h>
#include <aptx/common/types.h>
typedef struct aptx_aa_encoder aptx_aa_encoder;
typedef struct aptx_aa_stream aptx_aa_stream;
#ifdef __cplusplus
extern "C" {
#endif

/* Creates an encoder for a public configuration using the default allocator. */
aptx_aa_encoder *aptx_aa_create(const aptx_aa_config *, uint32_t bitrate, size_t packet_bytes);

/* Creates an encoder for an explicit internal variant using the default allocator. */
aptx_aa_encoder *aptx_aa_create_variant(unsigned variant, uint32_t sample_rate, uint32_t bitrate,
                                        size_t packet_bytes);

/* Creates an encoder for a public configuration using a host allocator. */
aptx_aa_encoder *aptx_aa_create_with_allocator(const aptx_aa_config *, uint32_t bitrate,
                                               size_t packet_bytes, const aptx_aa_allocator *);

/* Creates an explicit internal variant using a host allocator. */
aptx_aa_encoder *aptx_aa_create_variant_with_allocator(unsigned variant, uint32_t sample_rate,
                                                       uint32_t bitrate, size_t packet_bytes,
                                                       const aptx_aa_allocator *);

/* Returns the workspace capacity required for in-place encoder initialization. */
size_t aptx_aa_encoder_workspace_bytes(void);

/* Initializes an encoder for a public configuration in caller-owned memory. */
int aptx_aa_encoder_init_in_memory(void *, size_t bytes, const aptx_aa_config *, uint32_t bitrate,
                                   size_t packet_bytes, aptx_aa_encoder **out);

/* Initializes an explicit internal variant in caller-owned memory. */
int aptx_aa_encoder_init_variant_in_memory(void *, size_t bytes, unsigned variant, uint32_t rate,
                                           uint32_t bitrate, size_t packet_bytes,
                                           aptx_aa_encoder **out);

/* Destroys an encoder and releases its owned workspace. */
void aptx_aa_destroy(aptx_aa_encoder *);

/* Consumes two optional contiguous input regions and emits a completed packet. */
int aptx_aa_encode(aptx_aa_encoder *, const int32_t *first, size_t first_samples,
                   const int32_t *second, size_t second_samples, uint8_t *out, size_t capacity,
                   aptx_aa_progress *);

/* Closes an unfinished packet without synthesizing additional tail samples. */
int aptx_aa_flush(aptx_aa_encoder *, uint8_t *out, size_t capacity, size_t *written);

/* Clamps and applies the target bitrate for subsequent input. */
int aptx_aa_set_bitrate(aptx_aa_encoder *, uint32_t bitrate);

/* Switches the public configuration and resets encoder state. */
int aptx_aa_reset(aptx_aa_encoder *, const aptx_aa_config *, uint32_t bitrate, size_t packet_bytes);

/* Returns the minimum bitrate allowed by a public configuration. */
uint32_t aptx_aa_min_bitrate(const aptx_aa_config *);

/* Computes the codec payload budget from a link MTU. */
size_t aptx_aa_packet_budget(size_t link_mtu);

/* Selects a negotiated mode row for configuration, link, budget, and quality. */
int aptx_aa_table_row(const aptx_aa_config *, const aptx_aa_link *, size_t packet_budget,
                      unsigned quality, aptx_aa_row *);

/* Creates a queued streaming encoder using the default allocator. */
aptx_aa_stream *aptx_aa_stream_create(const aptx_aa_config *, size_t packet_budget);

/* Creates a queued streaming encoder using a host allocator. */
aptx_aa_stream *aptx_aa_stream_create_with_allocator(const aptx_aa_config *, size_t packet_budget,
                                                     const aptx_aa_allocator *);

/* Returns the workspace capacity for a requested sample-queue capacity. */
size_t aptx_aa_stream_workspace_bytes(size_t queue_samples);

/* Initializes stream control, core encoder, and ring queue in caller memory. */
int aptx_aa_stream_init_in_memory(void *, size_t bytes, const aptx_aa_config *,
                                  size_t packet_budget, size_t queue_samples, aptx_aa_stream **out);

/* Destroys a streaming encoder and its owned core and workspace. */
void aptx_aa_stream_destroy(aptx_aa_stream *);

/* Queues a possibly discontinuous sample batch and emits a packet when due. */
int aptx_aa_stream_push(aptx_aa_stream *, const int32_t *pcm, size_t samples, uint8_t *out,
                        size_t capacity, size_t *written);

/* Requests a quality-level transition at the next packet boundary. */
int aptx_aa_stream_request_level(aptx_aa_stream *, unsigned quality);

/* Controls whether a pending quality transition is held. */
void aptx_aa_stream_hold_level(aptx_aa_stream *, int hold);

/* Applies a pending quality level immediately when no packet is active. */
int aptx_aa_stream_apply_level_now(aptx_aa_stream *);

/* Resets stream state and clears the queue using a new mode row. */
int aptx_aa_stream_reset(aptx_aa_stream *, const aptx_aa_config *, const aptx_aa_row *);

/* Returns the currently active negotiated mode row. */
int aptx_aa_stream_get_row(const aptx_aa_stream *, aptx_aa_row *);

/* Returns the currently active quality level. */
unsigned aptx_aa_stream_level(const aptx_aa_stream *);

/* Returns accumulated consumed-sample deficit relative to the target period. */
int64_t aptx_aa_stream_deficit(const aptx_aa_stream *);

/* Controls whether quality level one is mapped to level two. */
void aptx_aa_stream_map_level_one(aptx_aa_stream *, int enabled);

#ifdef __cplusplus
}
#endif

#endif
