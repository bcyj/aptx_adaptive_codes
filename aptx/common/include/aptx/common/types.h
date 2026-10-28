#ifndef APTX_AA_COMMON_TYPES_H
#define APTX_AA_COMMON_TYPES_H
#include <stddef.h>
#include <stdint.h>
#define APTX_AA_MAX_PACKET 1032
#define APTX_AA_MAX_QUEUED_SAMPLES 30720
typedef enum {
    APTX_AA_OK = 0,
    APTX_AA_INVALID = -1,
    APTX_AA_NOMEM = -2,
    APTX_AA_OUTPUT_TOO_SMALL = -3,
    APTX_AA_QUEUE_FULL = -4,
    APTX_AA_INTERNAL = -5,
    APTX_AA_PACKET_TOO_SMALL = -6,
    APTX_AA_BAD_STREAM = -7,
    APTX_AA_UNSUPPORTED_STREAM = -8,
    APTX_AA_RESOURCE_LIMIT = -9
} aptx_aa_status;
typedef enum { APTX_AA_HQ = 0, APTX_AA_LL = 1 } aptx_aa_profile;
typedef enum { APTX_AA_R1 = 0, APTX_AA_R2 = 1 } aptx_aa_decoder;
typedef struct {
    uint32_t sample_rate;
    unsigned channels;
    aptx_aa_profile profile;
    aptx_aa_decoder decoder;
} aptx_aa_config;
typedef struct {
    uint32_t bitrate, period_ms, samples_per_channel;
    size_t packet_bytes;
    uint8_t cfg;
} aptx_aa_row;
typedef struct {
    int qhs, twm, enhanced_ll;
} aptx_aa_link;
typedef struct {
    size_t consumed, written;
} aptx_aa_progress;

#ifdef __cplusplus
extern "C" {
#endif

/* Implements aptx_aa_strerror. */
const char *aptx_aa_strerror(int status);

/* Implements aptx_aa_swap16. */
void aptx_aa_swap16(const uint8_t *, uint8_t *, size_t bytes);
#ifdef __cplusplus
}
#endif

#endif
