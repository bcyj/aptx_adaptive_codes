#include "encoder_internal.h"

/* Implements reserve. */
static int reserve(size_t capacity, size_t need) {
    return need <= capacity ? 0 : APTX_AA_RESOURCE_LIMIT;
}

/* Implements put. */
static void put(AABits *b, uint32_t v, unsigned n) {
    if (b->error || !n)
        return;
    if (n > 32 || b->len > SIZE_MAX - 8) {
        b->error = APTX_AA_INTERNAL;
        return;
    }
    if (b->len + 5 > b->cap) {
        b->error = reserve(b->cap, b->len + (n + 7) / 8 + 1);
        if (b->error)
            return;
    }
    v &= (n == 32) ? UINT32_MAX : ((1u << n) - 1);
    if (b->used > 0) {
        unsigned space = 8 - b->used;
        if (n <= space) {
            b->data[b->len - 1] |= (uint8_t)(v << (space - n));
            b->used = (b->used + n) & 7;
            return;
        }
        b->data[b->len - 1] |= (uint8_t)(v >> (n - space));
        n -= space;
        v &= (1u << n) - 1;
        b->used = 0;
    }
    while (n >= 8) {
        b->data[b->len++] = (uint8_t)(v >> (n - 8));
        n -= 8;
        v &= (n == 0) ? 0 : ((1u << n) - 1);
    }
    if (n > 0) {
        b->data[b->len++] = (uint8_t)(v << (8 - n));
        b->used = n;
    }
}

/* Implements code. */
static void code(AABits *b, AACode c) {
    if (!c.len)
        b->error = APTX_AA_INTERNAL;
    else
        put(b, c.code, c.len);
}

/* Implements exp_golomb. */
static void exp_golomb(AABits *b, uint32_t n) {
    uint32_t v = n + 1, t = v;
    unsigned len = 0;
    if (!v) {
        b->error = APTX_AA_INTERNAL;
        return;
    }
    while (t) {
        len++;
        t >>= 1;
    }
    put(b, 0, len - 1);
    put(b, v, len);
}

/* Implements reduced. */
static unsigned reduced(unsigned b) {
    return b > 1 ? b - 1 : b;
}

/* Implements alloc_header. */
static uint32_t alloc_header(AAParams p) {
    if (p.bands == 4)
        return 8 + (p.rate == 48000 ? 2 : 0) + (p.variant == 1);
    return 8 + (p.rate == 44100 ? 0 : (p.rate == 88200 ? 4 : (p.rate == 96000 ? 6 : 2)));
}

/* Implements zero_escape. */
static void zero_escape(AABits *b, const uint8_t *bits, unsigned w, size_t blocks) {
    unsigned i;
    for (i = 0; i < w; i++)
        if (bits[i])
            return;
    put(b, (uint32_t)blocks - 1, 8);
}

/* Implements absolute. */
static void absolute(AABits *b, AAParams p, const uint8_t *bits, size_t blocks) {
    unsigned i;
    int escapes = p.variant == 3;
    const bool *pair = p.bands == 8 ? PAIR_TABLE_8BAND : PAIR_TABLE_4BAND;
    put(b, alloc_header(p), 4);
    for (i = 0; i < p.width; i += 2) {
        const AACode *tab = pair[i / 2] ? ABS_B : ABS_A;
        unsigned a = reduced(bits[i]), c = reduced(bits[i + 1]);
        if (escapes) {
            code(b, tab[(AA_MIN(a, 8) << 4) | AA_MIN(c, 8)]);
            if (a > 7)
                put(b, a - 8, 3);
            if (c > 7)
                put(b, c - 8, 3);
        } else {
            if (a > 8 || c > 8) {
                b->error = APTX_AA_INTERNAL;
                return;
            }
            code(b, tab[(a << 4) | c]);
        }
    }
    if (escapes)
        zero_escape(b, bits, p.width, blocks);
}

/* Implements differential. */
static void differential(AABits *b, AAParams p, const uint8_t *prev, const uint8_t *bits,
                         size_t blocks) {
    unsigned i;
    int escapes = p.variant == 3;
    for (i = 0; i < p.width; i++) {
        int v = (int)reduced(bits[i]), pv = (int)reduced(prev[i]);
        const AACode *tab = DIFF[DIFF_INDEX[pv]];
        if (escapes) {
            int lo = DIFF_RANGE[pv][0], hi = DIFF_RANGE[pv][1], cl = aa_clamp(v, lo, hi);
            code(b, tab[(cl - pv) & 15]);
            if (v <= lo)
                put(b, (uint32_t)(lo - v), 3);
            else if (v >= hi)
                put(b, (uint32_t)(v - hi), 3);
        } else
            code(b, tab[(v - pv) & 15]);
    }
    if (escapes)
        zero_escape(b, bits, p.width, blocks);
}

/* Implements codewords. */
static void codewords(AABits *b, AAParams p, const uint8_t *bits, const int32_t *q, size_t n) {
    size_t at;
    unsigned i;
    for (at = 0; at + 2 * p.width <= n; at += 2 * p.width)
        for (i = 0; i < p.width; i++) {
            const AACodeword *e;
            int32_t q0, q1, h0, h1;
            unsigned index = bits[i];
            if (!index)
                continue;
            if (p.bands == 4 && index == 2 && i % 4 == 3)
                index = 1;
            if (index > 16) {
                b->error = APTX_AA_INTERNAL;
                return;
            }
            e = &CODEWORD[index];
            q0 = q[at + i];
            q1 = q[at + p.width + i];
            h0 = (q0 >> e->raw_lsbs) & e->msb_mask;
            h1 = (q1 >> e->raw_lsbs) & e->msb_mask;
            code(b, e->table[((uint32_t)h0 << e->pair_shift) | (uint32_t)h1]);
            if (e->raw_lsbs) {
                uint32_t mask = (UINT32_C(1) << e->raw_lsbs) - 1;
                put(b, (uint32_t)q0 & mask, e->raw_lsbs);
                put(b, (uint32_t)q1 & mask, e->raw_lsbs);
            }
        }
}

/* Implements aa_pack_init. */
int aa_pack_init(AAPacker *p, AAParams params, size_t packet_len) {
    memset(p, 0, offsetof(AAPacker, carry_storage));
    p->p = params;
    p->packet_len = packet_len;
    p->carry = p->carry_storage;
    p->carry_cap = sizeof(p->carry_storage);
    p->payload.data = p->payload_storage;
    p->payload.cap = sizeof(p->payload_storage);
    return 0;
}

/* Implements aa_pack_begin. */
void aa_pack_begin(AAPacker *p) {
    size_t i;
    p->payload.len = 0;
    p->payload.used = 0;
    if (p->carry_len > UINT32_MAX - 1) {
        p->payload.error = APTX_AA_INTERNAL;
        return;
    }
    exp_golomb(&p->payload, (uint32_t)p->carry_len);
    for (i = 0; i < p->carry_len; i++)
        put(&p->payload, p->carry[i], 8);
    p->has_prev = 0;
    p->open = 1;
}

/* Implements aa_pack_frame. */
int aa_pack_frame(AAPacker *p, const uint8_t *bits, const int32_t *q, size_t n) {
    if (!p->open)
        aa_pack_begin(p);
    if (p->has_prev)
        differential(&p->payload, p->p, p->prev, bits, n / p->p.width);
    else
        absolute(&p->payload, p->p, bits, n / p->p.width);
    codewords(&p->payload, p->p, bits, q, n);
    memcpy(p->prev, bits, p->p.width);
    p->has_prev = 1;
    return p->payload.error;
}

/* Implements aa_pack_finish. */
int aa_pack_finish(AAPacker *p, uint8_t *out, size_t *pad) {
    size_t cap = p->packet_len - 3, n, tail, t, ff, at;
    if (!p->open)
        aa_pack_begin(p);
    if (p->payload.error)
        return p->payload.error;
    n = AA_MIN(p->payload.len, cap);
    tail = p->payload.len - n;
    {
        int error = reserve(p->carry_cap, tail);
        if (error)
            return error;
    }
    out[0] = 0;
    out[1] = (uint8_t)(p->p.variant == 0 ? 1 : (p->p.variant == 1 ? 2 : p->p.variant));
    out[2] = (uint8_t)(p->packet_len / 4 - 3);
    memcpy(out + 3, p->payload.data, n);
    memcpy(p->carry, p->payload.data + n, tail);
    p->carry_len = tail;
    *pad = cap - n;
    if (*pad) {
        uint8_t *padding = out + 3 + n;
        out[1] |= 0x80;
        memset(padding, 0, *pad);
        padding[0] = 0x80;
        t = *pad - 1;
        ff = t / 255;
        at = *pad - 1 - ff;
        memset(padding + at + 1, 0xff, ff);
        padding[at] = (uint8_t)(t % 255);
    }
    p->open = 0;
    return 0;
}
