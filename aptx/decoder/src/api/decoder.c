#include "decoder_internal.h"

/* Implements aptx_aa_decoder_create_variant. */
aptx_aa_dec *aptx_aa_decoder_create_variant(unsigned variant, uint32_t rate) {
    return aptx_aa_decoder_create_variant_with_allocator(variant, rate, NULL);
}

/* Implements aptx_aa_decoder_create_variant_with_allocator. */
aptx_aa_dec *aptx_aa_decoder_create_variant_with_allocator(unsigned variant, uint32_t rate,
                                                           const aptx_aa_allocator *allocator) {
    AAParams p;
    aptx_aa_dec *d = NULL;
    void *memory;
    size_t bytes;
    if (aa_params(variant, rate, &p))
        return NULL;
    bytes = aptx_aa_decoder_workspace_bytes();
    memory = aa_memory_alloc(allocator, bytes);
    if (!memory)
        return NULL;
    if (aptx_aa_decoder_init_variant_in_memory(memory, bytes, variant, rate, &d)) {
        aa_memory_free(memory);
        return NULL;
    }
    d->owned = 1;
    return d;
}

/* Implements aptx_aa_decoder_create. */
aptx_aa_dec *aptx_aa_decoder_create(const aptx_aa_config *c) {
    return aptx_aa_decoder_create_with_allocator(c, NULL);
}

/* Implements aptx_aa_decoder_create_with_allocator. */
aptx_aa_dec *aptx_aa_decoder_create_with_allocator(const aptx_aa_config *c,
                                                   const aptx_aa_allocator *allocator) {
    AAParams p;
    if (aa_config_params(c, &p))
        return NULL;
    return aptx_aa_decoder_create_variant_with_allocator(p.variant, p.rate, allocator);
}

/* Implements aptx_aa_decoder_destroy. */
void aptx_aa_decoder_destroy(aptx_aa_dec *d) {
    if (d) {
        int owned = d->owned;
        d->owned = 0;
        d->finished = 1;
        if (owned)
            aa_memory_free(d);
    }
}

/* Implements aptx_aa_decoder_workspace_bytes. */
size_t aptx_aa_decoder_workspace_bytes(void) {
    return sizeof(aptx_aa_dec);
}

/* Implements aptx_aa_decoder_init_variant_in_memory. */
int aptx_aa_decoder_init_variant_in_memory(void *memory, size_t bytes, unsigned variant,
                                           uint32_t rate, aptx_aa_dec **out) {
    AAParams p;
    aptx_aa_dec *d;
    size_t need = aptx_aa_decoder_workspace_bytes();
    if (!out)
        return APTX_AA_INVALID;
    *out = NULL;
    if (!memory || (uintptr_t)memory % APTX_AA_WORKSPACE_ALIGNMENT || bytes < need ||
        aa_params(variant, rate, &p))
        return APTX_AA_INVALID;
    d = memory;
    memset(d, 0, sizeof(*d));
    d->p = p;
    aptx_aa_decoder_reset(d);
    *out = d;
    return 0;
}

/* Implements aptx_aa_decoder_init_in_memory. */
int aptx_aa_decoder_init_in_memory(void *memory, size_t bytes, const aptx_aa_config *c,
                                   aptx_aa_dec **out) {
    AAParams p;
    if (!out)
        return APTX_AA_INVALID;
    *out = NULL;
    if (aa_config_params(c, &p))
        return APTX_AA_INVALID;
    return aptx_aa_decoder_init_variant_in_memory(memory, bytes, p.variant, p.rate, out);
}

/* Implements aptx_aa_decoder_reset. */
int aptx_aa_decoder_reset(aptx_aa_dec *d) {
    if (!d)
        return APTX_AA_INVALID;
    aa_engine_init(&d->engine, d->p);
    aa_synthesis_init(&d->synth, d->p);
    d->pending_len = 0;
    d->has_pending = 0;
    d->padded = 0;
    d->finished = 0;
    return 0;
}

/* Implements aptx_aa_decoder_push. */
int aptx_aa_decoder_push(aptx_aa_dec *d, const uint8_t *packet, size_t bytes, int32_t *pcm,
                         size_t capacity, size_t *samples) {
    uint8_t *body, *joined;
    size_t len, carry, nbits, count = 0;
    int padded, err;
    if (samples)
        *samples = 0;
    if (!d || !samples || (!pcm && capacity) || d->finished)
        return APTX_AA_INVALID;
    body = d->scratch_body;
    joined = d->scratch_joined;
    err = aa_decode_split_packet(d, packet, bytes, body, &len, &carry, &padded);
    if (err)
        return err;
    if (carry && (!d->has_pending || d->padded))
        return APTX_AA_BAD_STREAM;
    nbits = d->pending_len + carry * 8;
    if (nbits > AA_DECODE_BITS)
        return APTX_AA_UNSUPPORTED_STREAM;
    if (d->has_pending) {
        aa_decode_copy_bits(joined, 0, d->pending, 0, d->pending_len);
        aa_decode_copy_bits(joined, d->pending_len, body, 0, carry * 8);
        err = aa_decode_parse(d, joined, nbits, 0, pcm, capacity, &count);
        if (err) {
            if (err == APTX_AA_OUTPUT_TOO_SMALL)
                *samples = count;
            return err;
        }
    }
    d->pending_len = len - carry * 8;
    aa_decode_copy_bits(d->pending, 0, body, carry * 8, d->pending_len);
    d->has_pending = 1;
    d->padded = padded;
    *samples = count;
    return 0;
}

/* Implements aptx_aa_decoder_finish. */
int aptx_aa_decoder_finish(aptx_aa_dec *d, int32_t *pcm, size_t capacity, size_t *samples) {
    size_t count = 0, tail, at, decode_capacity;
    int err;
    int32_t *zero, *out;
    if (samples)
        *samples = 0;
    if (!d || !samples || (!pcm && capacity))
        return APTX_AA_INVALID;
    if (d->finished || !d->has_pending)
        return 0;
    zero = d->scratch_bands;
    out = d->scratch_recon;
    tail = 2 * (d->synth.taps - d->synth.bands);
    decode_capacity = capacity >= tail ? capacity - tail : 0;
    err = aa_decode_parse(d, d->pending, d->pending_len, !d->padded, pcm, decode_capacity,
                          &count);
    if (err) {
        if (err == APTX_AA_OUTPUT_TOO_SMALL)
            *samples = count + tail;
        return err;
    }
    if (tail > capacity || count > capacity - tail || !pcm) {
        *samples = count + tail;
        return APTX_AA_OUTPUT_TOO_SMALL;
    }
    memset(zero, 0, 16 * sizeof(*zero));
    for (at = 0; at < tail; at += d->p.width) {
        size_t take = AA_MIN(d->p.width, tail - at);
        aa_synthesis_run(&d->synth, zero, out);
        memcpy(pcm + count + at, out, take * sizeof(*out));
    }
    d->finished = 1;
    d->has_pending = 0;
    d->pending_len = 0;
    *samples = count + tail;
    return 0;
}

/* Implements aptx_aa_decoder_delay. */
unsigned aptx_aa_decoder_delay(const aptx_aa_dec *d) {
    return d ? d->synth.taps - d->synth.bands : 0;
}

/* Implements aptx_aa_decoder_fixed_point. */
int aptx_aa_decoder_fixed_point(void) {
    return APTX_AA_DECODER_FIXED_POINT;
}

/* Implements aa_decoder_set_trace. */
void aa_decoder_set_trace(aptx_aa_dec *d, AADecodeTrace trace, void *data) {
#if APTX_AA_DRIVER_RESOURCES
    (void)d;
    (void)trace;
    (void)data;
#else
    if (d) {
        d->trace = trace;
        d->trace_data = data;
    }
#endif
}
