#include "decoder_internal.h"

/* Implements aa_decode_copy_bits. */
void aa_decode_copy_bits(uint8_t *dst, size_t dst_bit, const uint8_t *src, size_t src_bit,
                         size_t count) {
    if (!count)
        return;
    if ((dst_bit & 7) == 0 && (src_bit & 7) == 0) {
        size_t bytes = count >> 3;
        if (bytes)
            memmove(dst + (dst_bit >> 3), src + (src_bit >> 3), bytes);
        size_t rem = count & 7;
        if (rem) {
            size_t from = src_bit + (bytes << 3), to = dst_bit + (bytes << 3);
            for (size_t i = 0; i < rem; ++i) {
                unsigned bit = (src[(from + i) >> 3] >> (7 - ((from + i) & 7))) & 1;
                uint8_t mask = (uint8_t)(1u << (7 - ((to + i) & 7)));
                dst[(to + i) >> 3] = (uint8_t)((dst[(to + i) >> 3] & ~mask) | (bit ? mask : 0));
            }
        }
        return;
    }
    size_t i;
    for (i = 0; i < count; ++i) {
        size_t from = src_bit + i, to = dst_bit + i;
        unsigned bit = (src[from >> 3] >> (7 - (from & 7))) & 1;
        uint8_t mask = (uint8_t)(1u << (7 - (to & 7)));
        dst[to >> 3] = (uint8_t)((dst[to >> 3] & ~mask) | (bit ? mask : 0));
    }
}

/* Implements bit_at. */
static inline unsigned bit_at(const uint8_t *data, size_t bit) {
    return (data[bit >> 3] >> (7 - (bit & 7))) & 1;
}

/* Implements get. */
static int get(AAReader *r, unsigned n, uint32_t *v) {
    if (n > 32 || n > r->len - r->pos) {
        *v = 0;
        return AA_READ_END;
    }
    if (n == 0) {
        *v = 0;
        return 0;
    }
    size_t pos = r->pos;
    size_t byte_idx = pos >> 3;
    unsigned bit_offset = (unsigned)(pos & 7);
    unsigned space = 8 - bit_offset;
    r->pos = pos + n;
    if (n <= space) {
        *v = (r->bits[byte_idx] >> (space - n)) & ((1u << n) - 1);
        return 0;
    }
    uint32_t res = r->bits[byte_idx] & ((1u << space) - 1);
    n -= space;
    byte_idx++;
    while (n >= 8) {
        res = (res << 8) | r->bits[byte_idx++];
        n -= 8;
    }
    if (n > 0)
        res = (res << n) | (r->bits[byte_idx] >> (8 - n));
    *v = res;
    return 0;
}

/* Implements huffman. */
static int huffman(const aptx_aa_dec *d, AAReader *r, int root, uint32_t *v) {
    int node = root;
    (void)d;
    unsigned depth;
    const uint8_t *bits = r->bits;
    size_t pos = r->pos, len = r->len;
    for (depth = 0; depth <= 24; depth++) {
        if (AA_DECODE_NODES[node].symbol >= 0) {
            *v = (uint32_t)AA_DECODE_NODES[node].symbol;
            r->pos = pos;
            return 0;
        }
        if (pos >= len) {
            r->pos = pos;
            return AA_READ_END;
        }
        unsigned bit = (bits[pos >> 3] >> (7 - (pos & 7))) & 1;
        pos++;
        node = AA_DECODE_NODES[node].child[bit];
        if (node < 0) {
            r->pos = pos;
            return APTX_AA_BAD_STREAM;
        }
    }
    r->pos = pos;
    return APTX_AA_BAD_STREAM;
}

/* Implements only_padding. */
static int only_padding(const AAReader *r) {
    size_t i;
    if (r->pos >= r->len)
        return 1;
    if (r->len - r->pos >= 8)
        return 0;
    for (i = r->pos; i < r->len; i++)
        if (bit_at(r->bits, i))
            return 0;
    return 1;
}

/* Implements header. */
static uint32_t header(AAParams p) {
    if (p.bands == 4)
        return 8 + (p.rate == 48000 ? 2 : 0) + (p.variant == 1);
    return 8 + (p.rate == 44100 ? 0 : (p.rate == 88200 ? 4 : (p.rate == 96000 ? 6 : 2)));
}

/* Implements read_alloc. */
static int read_alloc(aptx_aa_dec *d, AAReader *r, const uint8_t *prev, uint8_t *bits) {
    unsigned i;
    int err, esc = d->p.variant == 3;
    uint32_t v;
    if (!prev) {
        const bool *pair = d->p.bands == 4 ? PAIR_TABLE_4BAND : PAIR_TABLE_8BAND;
        if (get(r, 4, &v))
            return AA_READ_END;
        if (v != header(d->p))
            return APTX_AA_BAD_STREAM;
        for (i = 0; i < d->p.width; i += 2) {
            uint32_t a, b, ext;
            err = huffman(d, r, AA_DECODE_ABS_ROOT[pair[i / 2]], &v);
            if (err)
                return err;
            a = v >> 4;
            b = v & 15;
            if (esc && a == 8) {
                if (get(r, 3, &ext))
                    return AA_READ_END;
                a += ext;
            }
            if (esc && b == 8) {
                if (get(r, 3, &ext))
                    return AA_READ_END;
                b += ext;
            }
            bits[i] = (uint8_t)(a ? a + 1 : 0);
            bits[i + 1] = (uint8_t)(b ? b + 1 : 0);
        }
    } else
        for (i = 0; i < d->p.width; i++) {
            int pv = prev[i] > 1 ? prev[i] - 1 : prev[i], value;
            uint32_t ext;
            err = huffman(d, r, AA_DECODE_DIFF_ROOT[DIFF_INDEX[pv]], &v);
            if (err)
                return err;
            value = (pv + (int)v) & 15;
            if (esc) {
                int lo = DIFF_RANGE[pv][0], hi = DIFF_RANGE[pv][1];
                if (value > hi)
                    value -= 16;
                if (value == lo) {
                    if (get(r, 3, &ext))
                        return AA_READ_END;
                    value = lo - (int)ext;
                } else if (value == hi) {
                    if (get(r, 3, &ext))
                        return AA_READ_END;
                    value = hi + (int)ext;
                }
            }
            if (value < 0 || value > 15)
                return APTX_AA_BAD_STREAM;
            bits[i] = (uint8_t)(value ? value + 1 : 0);
        }
    for (i = 0; i < d->p.width; i++)
        if (bits[i] == 1 || bits[i] > 16 || (!esc && bits[i] > 9))
            return APTX_AA_BAD_STREAM;
    return 0;
}

/* Implements signed_code. */
static int32_t signed_code(uint32_t v, unsigned bits) {
    return audio_math_sign_extend_i32(v, bits);
}

/* Implements read_pair. */
static int read_pair(aptx_aa_dec *d, AAReader *r, const uint8_t *bits, int32_t *q) {
    unsigned i, w = d->p.width;
    memset(q, 0, 2 * w * sizeof(*q));
    for (i = 0; i < w; i++)
        if (bits[i]) {
            unsigned index = bits[i], kind;
            const AACodeword *e;
            uint32_t sym, a, b, h0, h1;
            int err;
            if (d->p.bands == 4 && index == 2 && i % 4 == 3)
                index = 1;
            e = &CODEWORD[index];
            kind = AA_MIN(index, 4);
            err = huffman(d, r, AA_DECODE_CODE_ROOT[kind], &sym);
            if (err)
                return err;
            h0 = sym >> e->pair_shift;
            h1 = sym & ((1u << e->pair_shift) - 1);
            if (get(r, e->raw_lsbs, &a) || get(r, e->raw_lsbs, &b))
                return AA_READ_END;
            q[i] = signed_code((h0 << e->raw_lsbs) | a, bits[i]);
            q[w + i] = signed_code((h1 << e->raw_lsbs) | b, bits[i]);
        }
    return 0;
}

/* Implements output_pair. */
static void output_pair(aptx_aa_dec *d, const uint8_t *bits, const int32_t *q, int32_t *pcm) {
    unsigned block, i, w = d->p.width;
    int32_t *bands = d->scratch_bands, *recon = d->scratch_recon, *pred = d->scratch_pred;
    for (block = 0; block < 2; block++) {
        aa_reconstruct(&d->engine, bits, q + block * w, bands, recon);
#if !APTX_AA_DRIVER_RESOURCES
        if (d->trace) {
            for (i = 0; i < w; i++)
                pred[i] = d->engine.ch[i / d->p.bands].pred[i % d->p.bands].pr;
            d->trace(d->trace_data, bits, q + block * w, recon, pred, bands, w);
        }
#else
        (void)i;
        (void)pred;
#endif
        aa_synthesis_run(&d->synth, bands, pcm + block * w);
    }
}

/* Implements store_pair. */
static void store_pair(unsigned width, const uint8_t *bits, const int32_t *q, int32_t *stored) {
    unsigned i;
    for (i = 0; i < width; ++i) {
        stored[i] = (int32_t)(((uint32_t)q[i] & 0xffffu) | ((uint32_t)bits[i] << 16));
        stored[width + i] = q[width + i];
    }
}

/* Implements render_pairs. */
static void render_pairs(aptx_aa_dec *d, int32_t *pcm, size_t samples) {
    unsigned i, width = d->p.width;
    size_t at;
    uint8_t bits[AA_WIDTH];
    int32_t *q = d->scratch_codes;
    for (at = 0; at < samples; at += 2 * width) {
        for (i = 0; i < width; ++i) {
            uint32_t packed = (uint32_t)pcm[at + i];
            bits[i] = (uint8_t)(packed >> 16);
            q[i] = audio_math_sign_extend_i32(packed, 16);
            q[width + i] = pcm[at + width + i];
        }
        output_pair(d, bits, q, pcm + at);
    }
}

/* Implements aa_decode_parse. */
int aa_decode_parse(aptx_aa_dec *d, const uint8_t *data, size_t n, int final, int32_t *pcm,
                    size_t capacity, size_t *samples) {
    AAReader r = {data, n, 0};
    uint8_t prev[16], bits[16];
    int has_prev = 0;
    size_t count = 0;
    while (!only_padding(&r)) {
        unsigned blocks = 0, max = 64, i;
        int escape = 1, err;
        uint32_t v;
        int32_t *q = d->scratch_codes;
        err = read_alloc(d, &r, has_prev ? prev : NULL, bits);
        if (err) {
            if (err == AA_READ_END && final)
                break;
            return err == AA_READ_END ? APTX_AA_BAD_STREAM : err;
        }
        for (i = 0; i < d->p.width; i++)
            if (bits[i])
                escape = 0;
        if (escape) {
            if (d->p.variant != 3)
                return APTX_AA_BAD_STREAM;
            if (get(&r, 8, &v)) {
                if (final)
                    break;
                return APTX_AA_BAD_STREAM;
            }
            max = v + 1;
            if (max > 64 || (max & 1))
                return APTX_AA_BAD_STREAM;
        }
        while (blocks < max) {
            if (!escape && only_padding(&r))
                break;
            err = read_pair(d, &r, bits, q);
            if (err) {
                if (err == AA_READ_END && final)
                    goto complete;
                return err == AA_READ_END ? APTX_AA_BAD_STREAM : err;
            }
            if (count > 1048576 - 2 * d->p.width)
                return APTX_AA_UNSUPPORTED_STREAM;
            if (pcm && count <= capacity && 2 * d->p.width <= capacity - count)
                store_pair(d->p.width, bits, q, pcm + count);
            count += 2 * d->p.width;
            blocks += 2;
        }
        if (!blocks)
            break;
        memcpy(prev, bits, d->p.width);
        has_prev = 1;
    }
complete:
    *samples = count;
    if (count > capacity || (count && !pcm))
        return APTX_AA_OUTPUT_TOO_SMALL;
    if (count)
        render_pairs(d, pcm, count);
    return 0;
}

/* Implements frame_type. */
static unsigned frame_type(AAParams p) {
    return p.variant == 0 ? 1 : (p.variant == 1 ? 2 : p.variant);
}

/* Implements packet_header. */
static int packet_header(AAParams p, const uint8_t *b, size_t n) {
    return b[0] == 0 && (unsigned)(b[1] & 127) == frame_type(p) &&
           (size_t)(b[2] + 3) * 4 == n;
}

/* Implements aa_decode_split_packet. */
int aa_decode_split_packet(aptx_aa_dec *d, const uint8_t *packet, size_t n, uint8_t *body,
                           size_t *len, size_t *carry, int *padded) {
    uint8_t *raw = d->scratch_raw;
    size_t bytes, i, ff = 0, start, trailer;
    AAReader r;
    uint32_t v;
    unsigned z = 0;
    if (!packet || n < 12 || n > APTX_AA_MAX_PACKET || (n & 3))
        return APTX_AA_INVALID;
    if (packet_header(d->p, packet, n))
        memcpy(raw, packet, n);
    else {
        aptx_aa_swap16(packet, raw, n);
        if (!packet_header(d->p, raw, n))
            return APTX_AA_BAD_STREAM;
    }
    bytes = n - 3;
    *padded = !!(raw[1] & 128);
    if (*padded) {
        while (ff < bytes && raw[n - 1 - ff] == 255)
            ff++;
        if (ff == bytes)
            return APTX_AA_BAD_STREAM;
        trailer = bytes - 1 - ff;
        start = 255 * ff + raw[3 + trailer] + 1;
        if (start > bytes)
            return APTX_AA_BAD_STREAM;
        start = bytes - start;
        if (raw[3 + start] != (start == bytes - 1 ? 0 : 128))
            return APTX_AA_BAD_STREAM;
        for (i = start + 1; i < trailer; i++)
            if (raw[3 + i])
                return APTX_AA_BAD_STREAM;
        bytes = start;
    }
    *len = bytes * 8;
    memcpy(body, raw + 3, bytes);
    r.bits = body;
    r.len = *len;
    r.pos = 0;
    do {
        if (get(&r, 1, &v))
            return APTX_AA_BAD_STREAM;
        if (!v && ++z > 20)
            return APTX_AA_UNSUPPORTED_STREAM;
    } while (!v);
    if (get(&r, z, &v))
        return APTX_AA_BAD_STREAM;
    *carry = ((size_t)1 << z) + v - 1;
    if (*carry > (r.len - r.pos) / 8)
        return APTX_AA_UNSUPPORTED_STREAM;
    *len = r.len - r.pos;
    aa_decode_copy_bits(body, 0, body, r.pos, *len);
    return 0;
}
