#include "aa_internal.h"

/* Implements aptx_aa_swap16. */
void aptx_aa_swap16(const uint8_t *src, uint8_t *dst, size_t n) {
    size_t i = 0;
    while (i + 8 <= n) {
        uint64_t v;
        memcpy(&v, src + i, sizeof(v));
        v = ((v & UINT64_C(0x00FF00FF00FF00FF)) << 8) | ((v & UINT64_C(0xFF00FF00FF00FF00)) >> 8);
        memcpy(dst + i, &v, sizeof(v));
        i += 8;
    }
    while (i + 2 <= n) {
        uint8_t a = src[i], b = src[i + 1];
        dst[i] = b;
        dst[i + 1] = a;
        i += 2;
    }
    if (i < n)
        dst[i] = src[i];
}

/* Implements aptx_aa_strerror. */
const char *aptx_aa_strerror(int status) {
    switch (status) {
    case 0:
        return "success";
    case APTX_AA_INVALID:
        return "invalid configuration or PCM";
    case APTX_AA_NOMEM:
        return "out of memory; reset encoder before retrying";
    case APTX_AA_OUTPUT_TOO_SMALL:
        return "output buffer too small";
    case APTX_AA_QUEUE_FULL:
        return "PCM queue full; drain before appending input";
    case APTX_AA_PACKET_TOO_SMALL:
        return "packet budget too small for this row";
    case APTX_AA_BAD_STREAM:
        return "malformed or discontinuous codec stream";
    case APTX_AA_UNSUPPORTED_STREAM:
        return "unsupported stream geometry or multi-packet carry";
    case APTX_AA_RESOURCE_LIMIT:
        return "fixed instance workspace exhausted; reset or use supported packet geometry";
    default:
        return "internal coding error; reset encoder before retrying";
    }
}
