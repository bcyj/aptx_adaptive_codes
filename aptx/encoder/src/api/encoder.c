#include "encoder_internal.h"

/* Implements packet_valid. */
static int packet_valid(size_t n) {
    return n >= 12 && n <= APTX_AA_MAX_PACKET && !(n & 3);
}

/* Implements rate_new. */
static AARate rate_new(uint32_t b) {
    AARate r = {b, b, b, b, b, b};
    return r;
}

/* Implements aa_encoder_reset_variant. */
int aa_encoder_reset_variant(aptx_aa_encoder *e, unsigned variant, uint32_t fs, uint32_t bitrate,
                             size_t n) {
    AAParams p;
    uint32_t lo, hi;
    if (!e || !bitrate || !packet_valid(n) || aa_params(variant, fs, &p))
        return APTX_AA_INVALID;
    aa_rate_range(p, &lo, &hi);
    bitrate = aa_clampu(bitrate, lo, hi);
    e->p = p;
    aa_engine_init(&e->engine, p);
    aa_pack_reset(&e->pack, p, n);
    e->frame_len = 0;
    e->pad_hist = 0;
    e->pad1 = 0;
    e->pad2 = 0;
    e->pending_pad = 0;
    e->has_pending = 0;
    e->ov_hist = 0;
    e->ov_prev = 0;
    e->p_est = aa_pool(p, bitrate);
    e->rate = rate_new(bitrate);
    e->carry_in = 0;
    e->packet_blocks = 0;
    e->failed = 0;
    return 0;
}

/* Implements aptx_aa_create_variant. */
aptx_aa_encoder *aptx_aa_create_variant(unsigned variant, uint32_t rate, uint32_t bitrate,
                                        size_t n) {
    return aptx_aa_create_variant_with_allocator(variant, rate, bitrate, n, NULL);
}

/* Implements aptx_aa_create_variant_with_allocator. */
aptx_aa_encoder *aptx_aa_create_variant_with_allocator(unsigned variant, uint32_t rate,
                                                       uint32_t bitrate, size_t n,
                                                       const aptx_aa_allocator *allocator) {
    AAParams p;
    aptx_aa_encoder *e = NULL;
    void *memory;
    size_t bytes;
    if (!bitrate || !packet_valid(n) || aa_params(variant, rate, &p))
        return NULL;
    bytes = aptx_aa_encoder_workspace_bytes();
    memory = aa_memory_alloc(allocator, bytes);
    if (!memory)
        return NULL;
    if (aptx_aa_encoder_init_variant_in_memory(memory, bytes, variant, rate, bitrate, n, &e)) {
        aa_memory_free(memory);
        return NULL;
    }
    e->owned = 1;
    return e;
}

/* Implements aptx_aa_create. */
aptx_aa_encoder *aptx_aa_create(const aptx_aa_config *c, uint32_t bitrate, size_t n) {
    return aptx_aa_create_with_allocator(c, bitrate, n, NULL);
}

/* Implements aptx_aa_create_with_allocator. */
aptx_aa_encoder *aptx_aa_create_with_allocator(const aptx_aa_config *c, uint32_t bitrate, size_t n,
                                               const aptx_aa_allocator *allocator) {
    AAParams p;
    if (aa_config_params(c, &p))
        return NULL;
    return aptx_aa_create_variant_with_allocator(p.variant, p.rate, bitrate, n, allocator);
}

/* Implements aptx_aa_destroy. */
void aptx_aa_destroy(aptx_aa_encoder *e) {
    if (e) {
        int owned = e->owned;
        e->owned = 0;
        if (owned)
            aa_memory_free(e);
    }
}

/* Implements aptx_aa_encoder_workspace_bytes. */
size_t aptx_aa_encoder_workspace_bytes(void) {
    return sizeof(aptx_aa_encoder);
}

/* Implements aa_encoder_init. */
int aa_encoder_init(aptx_aa_encoder *e, unsigned variant, uint32_t rate, uint32_t bitrate,
                    size_t n) {
    AAParams p;
    if (!e || !bitrate || !packet_valid(n) || aa_params(variant, rate, &p))
        return APTX_AA_INVALID;

    memset(e, 0, sizeof(*e));
    aa_pack_init(&e->pack, p, n);
    return aa_encoder_reset_variant(e, variant, rate, bitrate, n);
}

/* Implements aptx_aa_encoder_init_variant_in_memory. */
int aptx_aa_encoder_init_variant_in_memory(void *memory, size_t bytes, unsigned variant,
                                           uint32_t rate, uint32_t bitrate, size_t n,
                                           aptx_aa_encoder **out) {
    AAParams p;
    int err;
    size_t need = aptx_aa_encoder_workspace_bytes();
    if (!out)
        return APTX_AA_INVALID;
    *out = NULL;
    if (!memory || (uintptr_t)memory % APTX_AA_WORKSPACE_ALIGNMENT || bytes < need || !bitrate ||
        !packet_valid(n) || aa_params(variant, rate, &p))
        return APTX_AA_INVALID;
    err = aa_encoder_init(memory, variant, rate, bitrate, n);
    if (!err)
        *out = memory;
    return err;
}

/* Implements aptx_aa_encoder_init_in_memory. */
int aptx_aa_encoder_init_in_memory(void *memory, size_t bytes, const aptx_aa_config *c,
                                   uint32_t bitrate, size_t n, aptx_aa_encoder **out) {
    AAParams p;
    if (!out)
        return APTX_AA_INVALID;
    *out = NULL;
    if (aa_config_params(c, &p))
        return APTX_AA_INVALID;
    return aptx_aa_encoder_init_variant_in_memory(memory, bytes, p.variant, p.rate, bitrate, n,
                                                  out);
}

/* Implements aptx_aa_reset. */
int aptx_aa_reset(aptx_aa_encoder *e, const aptx_aa_config *c, uint32_t bitrate, size_t n) {
    AAParams p;
    if (aa_config_params(c, &p))
        return APTX_AA_INVALID;
    return aa_encoder_reset_variant(e, p.variant, p.rate, bitrate, n);
}

/* Implements aptx_aa_set_bitrate. */
int aptx_aa_set_bitrate(aptx_aa_encoder *e, uint32_t bitrate) {
    uint32_t lo, hi;
    if (!e || !bitrate)
        return APTX_AA_INVALID;
    if (e->failed)
        return e->failed;
    aa_rate_range(e->p, &lo, &hi);
    e->rate = rate_new(aa_clampu(bitrate, lo, hi));
    return 0;
}

/* Implements aptx_aa_min_bitrate. */
uint32_t aptx_aa_min_bitrate(const aptx_aa_config *c) {
    AAParams p;
    uint32_t lo, hi;
    if (aa_config_params(c, &p))
        return 0;
    aa_rate_range(p, &lo, &hi);
    return lo;
}

/* Implements frame_size. */
static size_t frame_size(const aptx_aa_encoder *e, int *close) {
    int64_t cap = (int64_t)e->pack.packet_len - 3, space, hold, v;
    uint32_t x, z;
    if (e->pack.open) {
        space = cap - (int64_t)(aa_bit_len(&e->pack.payload) / 8);
        hold = e->ov_hist;
    } else {
        space = cap - (int64_t)e->pack.carry_len;
        hold = (int64_t)e->pack.carry_len + e->ov_hist;
    }
    v = space - hold + AA_MIN(e->pad_hist, 30);
    *close = 1;
    if (v < 0)
        return 8;

    x = 8u * (uint32_t)v;
    if (e->rate.target > e->rate.nominal) {
        uint32_t k = e->rate.nominal / (e->rate.target - e->rate.nominal);
        if (k)
            x += x / k;
    }
    z = x / e->p_est;
    if (z >= 48) {
        *close = 0;
        return 64;
    }
    if (z <= 7)
        return 8;
    if (z <= 15)
        return 16;
    if (z <= 31)
        return 32;
    return 48;
}

/* Implements code_frame. */
static int code_frame(aptx_aa_encoder *e) {
    uint8_t bits[16];
    int32_t recon[16];
    size_t i, blocks = e->frame_len / e->p.width;
    int first = !e->pack.open, err;
    e->p_est = aa_allocate(e->p, e->rate.target, e->rate.nominal, e->frame, e->frame_len, bits,
                           &e->allocation_scratch);
    for (i = 0; i < e->frame_len; i += e->p.width)
        aa_code(&e->engine, e->frame + i, bits, e->codes + i, recon);
    if (first) {
        e->carry_in = e->pack.carry_len;
        aa_pack_begin(&e->pack);
    }
    err = aa_pack_frame(&e->pack, bits, e->codes, e->frame_len);
    if (err)
        return e->failed = err;
    e->frame_len = 0;
    e->packet_blocks += blocks;
    if (first && e->has_pending) {
        e->pad2 = e->pad1;
        e->pad1 = e->pending_pad;
        e->has_pending = 0;
        e->pad_hist = (aa_mul64(410, e->pad_hist + e->pad2) + aa_mul64(205, e->pad1)) >> 10;
    }
    return 0;
}

/* Implements rate_check. */
static void rate_check(aptx_aa_encoder *e, uint32_t bytes, uint32_t pad) {
    AARate *r = &e->rate;
    uint32_t per_s, pps, a, b, measured, v;
    if (!e->packet_blocks)
        return;
    per_s = 2 * e->p.rate / (e->p.bands * (uint32_t)e->packet_blocks);
    pps = (per_s & 1) + (per_s >> 1);
    a = (UINT32_C(410) * (r->ap + r->a) + UINT32_C(1640) * (bytes + pad) * pps) >> 10;
    r->ap = r->a;
    r->a = a;
    b = (UINT32_C(410) * (r->bp + r->bq) + UINT32_C(1640) * bytes * pps) >> 10;
    r->bp = r->bq;
    r->bq = b;
    measured = a > r->nominal ? a : b;
    v = r->nominal - measured + r->target;
    r->target =
        r->nominal > v ? r->nominal : (e->p.bands == 8 ? AA_MIN(v, (5 * r->nominal) >> 2) : v);
}

/* Implements close_packet. */
static int close_packet(aptx_aa_encoder *e, uint8_t *out) {
    size_t written = aa_bit_len(&e->pack.payload) / 8, pad;
    int err = aa_pack_finish(&e->pack, out, &pad);
    if (err)
        return e->failed = err;
    e->ov_hist = (aa_mul64(410, e->ov_hist + e->ov_prev) +
                  aa_mul64(205, (int64_t)e->pack.carry_len)) >> 10;
    e->ov_prev = (int64_t)e->pack.carry_len;
    e->pending_pad = (int64_t)pad;
    e->has_pending = 1;
    rate_check(e, (uint32_t)(written + 3 - e->carry_in), (uint32_t)pad);
    e->carry_in = 0;
    e->packet_blocks = 0;
    return 0;
}

/* Implements pcm_valid. */
static int pcm_valid(const int32_t *pcm, size_t n) {
    size_t i;
    uint32_t accum = 0;
    if (!pcm && n)
        return 0;
    for (i = 0; i < n; i++)
        accum |= ((uint32_t)pcm[i] + 0x800000u);
    return (accum & ~UINT32_C(0xFFFFFF)) == 0;
}
typedef struct {
    const int32_t *first, *second;
    size_t first_len, second_len;
} AAPcmSpans;

/* Implements pcm_block. */
static const int32_t *pcm_block(const AAPcmSpans *pcm, size_t pos, size_t width,
                                int32_t scratch[AA_WIDTH]) {
    if (pos >= pcm->first_len)
        return pcm->second + pos - pcm->first_len;
    if (width <= pcm->first_len - pos)
        return pcm->first + pos;
    {
        size_t first = pcm->first_len - pos;
        memcpy(scratch, pcm->first + pos, first * sizeof(*scratch));
        memcpy(scratch + first, pcm->second, (width - first) * sizeof(*scratch));
    }
    return scratch;
}

/* Implements aptx_aa_encode. */
int aptx_aa_encode(aptx_aa_encoder *e, const int32_t *first, size_t first_len,
                   const int32_t *second, size_t second_len, uint8_t *out, size_t cap,
                   aptx_aa_progress *p) {
    AAPcmSpans pcm = {first, second, first_len, second_len};
    int32_t split_block[AA_WIDTH];
    size_t n, pos = 0;
    int err;
    if (p) {
        p->consumed = 0;
        p->written = 0;
    }
    if (second_len > SIZE_MAX - first_len)
        return APTX_AA_INVALID;
    n = first_len + second_len;
    if (!e || !out || !p || !pcm_valid(first, first_len) || !pcm_valid(second, second_len))
        return APTX_AA_INVALID;
    if (cap < e->pack.packet_len)
        return APTX_AA_OUTPUT_TOO_SMALL;
    if (e->failed)
        return e->failed;
    for (;;) {
        int close;
        size_t req = frame_size(e, &close), have = e->frame_len / e->p.width;
        if (have < req) {
            size_t take = AA_MIN(req - have, (n - pos) / e->p.width) & ~(size_t)1, j;
            for (j = 0; j < take; j++) {
                const int32_t *block =
                    pcm_block(&pcm, pos + j * e->p.width, e->p.width, split_block);
                aa_analyse(&e->engine, block, e->frame + e->frame_len);
                e->frame_len += e->p.width;
            }
            pos += take * e->p.width;
            if (have + take < req) {
                p->consumed = pos;
                return 0;
            }
        }
        err = code_frame(e);
        if (err) {
            p->consumed = pos;
            return err;
        }
        if (close ||
            (int64_t)(aa_bit_len(&e->pack.payload) / 8) >= (int64_t)e->pack.packet_len - 3) {
            err = close_packet(e, out);
            p->consumed = pos;
            if (!err)
                p->written = e->pack.packet_len;
            return err;
        }
    }
}

/* Implements aptx_aa_flush. */
int aptx_aa_flush(aptx_aa_encoder *e, uint8_t *out, size_t cap, size_t *written) {
    int err;
    if (written)
        *written = 0;
    if (!e || !out || !written)
        return APTX_AA_INVALID;
    if (cap < e->pack.packet_len)
        return APTX_AA_OUTPUT_TOO_SMALL;
    if (e->failed)
        return e->failed;
    if (!e->frame_len && !e->pack.open)
        return 0;
    if (e->frame_len) {
        err = code_frame(e);
        if (err)
            return err;
    }
    err = close_packet(e, out);
    if (!err)
        *written = e->pack.packet_len;
    return err;
}
