#include "mode_tables.h"
#include <audio_common/ring_buffer.h>

/* Implements aptx_aa_packet_budget. */
size_t aptx_aa_packet_budget(size_t mtu) {
    size_t n = mtu > 8 ? mtu - 8 : 0;
    return AA_MIN(AA_MAX(n, 128), APTX_AA_MAX_PACKET);
}

/* Implements aptx_aa_table_row. */
int aptx_aa_table_row(const aptx_aa_config *c, const aptx_aa_link *link, size_t budget,
                      unsigned quality, aptx_aa_row *out) {
    AAParams p;
    unsigned idx, top;
    AARowRate rate;
    AARowPacket packet;
    aptx_aa_link zero = {0, 0, 0};
    aptx_aa_row row;
    if (!out || quality < 1 || quality > 7 || aa_config_params(c, &p))
        return APTX_AA_INVALID;
    if (!link)
        link = &zero;
    top = c->profile == APTX_AA_HQ ? c->sample_rate == 96000 : !!link->enhanced_ll;
    idx = c->decoder == APTX_AA_R1 ? 2 * !!link->qhs : 1 + !!link->qhs + 2 * !!link->twm + 4 * top;
    rate = c->profile == APTX_AA_HQ ? AA_HQ[idx][quality - 1] : AA_LL[idx][quality - 1];
    packet = c->profile == APTX_AA_HQ ? AA_HQ_PACKET[idx][quality >= 3]
                                      : AA_LL_PACKET[idx][quality >= 3];
    row.bitrate = rate.kbps * 1000;
    row.period_ms = rate.ms;
    row.samples_per_channel = rate.samples;
    row.packet_bytes = packet.bytes & ~(size_t)3;
    row.cfg = packet.cfg;
    if (row.packet_bytes > budget && c->profile == APTX_AA_HQ) {

        uint32_t b = (uint32_t)budget * 8000u / row.period_ms;
        row.bitrate = b > 25000u ? b - 25000u : 0;
        row.packet_bytes = budget & ~(size_t)3;
    }
    if (row.packet_bytes > budget || row.bitrate < aptx_aa_min_bitrate(c) || row.packet_bytes < 12)
        return APTX_AA_PACKET_TOO_SMALL;
    *out = row;
    return 0;
}

typedef struct {
    aptx_aa_row row;
    size_t period;
    unsigned level;
    uint32_t bitrate;
} AAStep;
struct aptx_aa_stream {
    int owned;
    aptx_aa_encoder *core;
    aptx_aa_config config;
    aptx_aa_row row;
    AudioRingBuffer pcm;
    uint8_t window[APTX_AA_MAX_PACKET];
    size_t consumed, period, budget;
    int64_t deficit;
    unsigned level, pending;
    int hold, map_one;
    uint32_t bitrate;
    AAStep before;
};

/* Implements row_period. */
static size_t row_period(aptx_aa_config c, aptx_aa_row r) {

    return (size_t)(r.period_ms * c.sample_rate * c.channels / 1000);
}

/* Implements save_step. */
static void save_step(aptx_aa_stream *s) {
    s->before.row = s->row;
    s->before.period = s->period;
    s->before.level = s->level;
    s->before.bitrate = s->bitrate;
}

/* Implements set_bitrate. */
static void set_bitrate(aptx_aa_stream *s, uint32_t b) {
    s->bitrate = b;
    aptx_aa_set_bitrate(s->core, b);
}

/* Implements level_row. */
static int level_row(aptx_aa_stream *s, unsigned q, aptx_aa_row *r) {
    int err = aptx_aa_table_row(&s->config, NULL, s->budget, q, r);
    return err ? err : (r->packet_bytes == s->row.packet_bytes ? 0 : APTX_AA_INVALID);
}

/* Implements after_packet. */
static void after_packet(aptx_aa_stream *s) {
    uint32_t b, floor;
    save_step(s);
    if (!s->hold && s->pending) {
        unsigned q = s->pending;
        aptx_aa_row r;
        s->pending = 0;
        if (!level_row(s, q, &r)) {
            s->level = q;
            s->row = r;
            if (r.bitrate != s->bitrate) {
                set_bitrate(s, r.bitrate);
                s->period = row_period(s->config, r);
            }
        }
    }
    b = s->row.bitrate;
    floor = aptx_aa_min_bitrate(&s->config);
    if (s->deficit > 96)
        b = AA_MAX(b - b / 4, AA_MIN(floor, b));
    set_bitrate(s, b);
}

/* Implements aptx_aa_stream_create. */
aptx_aa_stream *aptx_aa_stream_create(const aptx_aa_config *c, size_t budget) {
    return aptx_aa_stream_create_with_allocator(c, budget, NULL);
}

/* Implements aptx_aa_stream_create_with_allocator. */
aptx_aa_stream *aptx_aa_stream_create_with_allocator(const aptx_aa_config *c, size_t budget,
                                                     const aptx_aa_allocator *allocator) {
    aptx_aa_row r;
    aptx_aa_stream *s;
    void *memory;
    if (aptx_aa_table_row(c, NULL, budget, 3, &r))
        return NULL;
    size_t bytes = aptx_aa_stream_workspace_bytes(0);
    s = (aptx_aa_stream *)aa_memory_alloc(allocator, bytes);
    if (!s)
        return NULL;
    memory = s;
    if (aptx_aa_stream_init_in_memory(s, bytes, c, budget, 0, &s)) {
        aa_memory_free(memory);
        return NULL;
    }
    s->owned = 1;
    return s;
}

/* Implements aptx_aa_stream_destroy. */
void aptx_aa_stream_destroy(aptx_aa_stream *s) {
    if (s) {
        int owned = s->owned;
        s->owned = 0;
        aptx_aa_destroy(s->core);
        if (owned)
            aa_memory_free(s);
    }
}

/* Implements align_workspace. */
static size_t align_workspace(size_t n) {
    size_t aligned;
    return audio_memory_align_size(n, APTX_AA_WORKSPACE_ALIGNMENT, &aligned) ? 0 : aligned;
}

/* Implements aptx_aa_stream_workspace_bytes. */
size_t aptx_aa_stream_workspace_bytes(size_t queue_samples) {
    size_t stream_bytes, encoder_bytes, total;
    if (!queue_samples)
        queue_samples = APTX_AA_MAX_QUEUED_SAMPLES;
    if (queue_samples < 32 || queue_samples > APTX_AA_MAX_QUEUED_SAMPLES || (queue_samples & 1))
        return 0;
    stream_bytes = align_workspace(sizeof(aptx_aa_stream));
    encoder_bytes = align_workspace(aptx_aa_encoder_workspace_bytes());
    if (!stream_bytes || !encoder_bytes || stream_bytes > SIZE_MAX - encoder_bytes)
        return 0;
    total = stream_bytes + encoder_bytes;
    if (queue_samples > (SIZE_MAX - total) / sizeof(int32_t))
        return 0;
    return total + queue_samples * sizeof(int32_t);
}

/* Implements aptx_aa_stream_init_in_memory. */
int aptx_aa_stream_init_in_memory(void *memory, size_t bytes, const aptx_aa_config *c,
                                  size_t budget, size_t queue_samples, aptx_aa_stream **out) {
    aptx_aa_stream *s;
    aptx_aa_row row;
    aptx_aa_config configuration;
    AAParams params;
    aptx_aa_encoder *core = NULL;
    unsigned char *base = memory;
    size_t need = aptx_aa_stream_workspace_bytes(queue_samples);
    size_t stream_bytes = align_workspace(sizeof(aptx_aa_stream));
    size_t encoder_bytes = align_workspace(aptx_aa_encoder_workspace_bytes());
    int err;
    if (!out)
        return APTX_AA_INVALID;
    *out = NULL;
    if (!memory || (uintptr_t)memory % APTX_AA_WORKSPACE_ALIGNMENT || !need || !stream_bytes ||
        !encoder_bytes || bytes < need)
        return APTX_AA_INVALID;
    err = aptx_aa_table_row(c, NULL, budget, 3, &row);
    if (err)
        return err;
    if (aa_config_params(c, &params))
        return APTX_AA_INVALID;
    configuration = *c;
    s = memory;
    memset(s, 0, sizeof(*s));
    if (aptx_aa_encoder_init_variant_in_memory(base + stream_bytes, encoder_bytes, params.variant,
                                               params.rate, row.bitrate, row.packet_bytes, &core))
        return APTX_AA_INVALID;
    s->core = core;
    audio_ring_init(&s->pcm, (unsigned char *)s->core + encoder_bytes,
                    queue_samples ? queue_samples : APTX_AA_MAX_QUEUED_SAMPLES, sizeof(int32_t));
    s->config = configuration;
    s->row = row;
    s->period = row_period(configuration, row);
    s->budget = AA_MAX(budget, row.packet_bytes);
    s->level = 3;
    s->map_one = 1;
    s->bitrate = row.bitrate;
    save_step(s);
    *out = s;
    return 0;
}

/* Implements encode_pcm_ring. */
static int encode_pcm_ring(aptx_aa_stream *s, aptx_aa_progress *total) {
    AudioRingConstSpan spans[2];
    size_t queued = audio_ring_size(&s->pcm);
    int err;
    audio_ring_read_spans(&s->pcm, queued, spans);
    err = aptx_aa_encode(s->core, spans[0].data, spans[0].length, spans[1].data,
                         spans[1].length, s->window, sizeof(s->window), total);
    if (total->consumed > queued || !audio_ring_consume(&s->pcm, total->consumed))
        return APTX_AA_INVALID;
    return err;
}

/* Implements aptx_aa_stream_push. */
int aptx_aa_stream_push(aptx_aa_stream *s, const int32_t *pcm, size_t n, uint8_t *out, size_t cap,
                        size_t *written) {
    aptx_aa_progress p;
    size_t i, w;
    int err;
    if (written)
        *written = 0;
    if (!s || !out || !written || (!pcm && n))
        return APTX_AA_INVALID;
    if (cap < s->row.packet_bytes)
        return APTX_AA_OUTPUT_TOO_SMALL;
    if (n > audio_ring_free(&s->pcm))
        return APTX_AA_QUEUE_FULL;
    {
        uint32_t accum = 0;
        for (i = 0; i < n; i++)
            accum |= ((uint32_t)pcm[i] + 0x800000u);
        if (accum & ~UINT32_C(0xFFFFFF))
            return APTX_AA_INVALID;
    }
    if (n && audio_ring_write(&s->pcm, pcm, n) != n)
        return APTX_AA_INVALID;
    err = encode_pcm_ring(s, &p);
    s->consumed += p.consumed;
    if (err)
        return err;
    w = p.written;
    if (!w && (int64_t)s->consumed < (int64_t)s->period + s->deficit)
        return 0;
    if (!w) {
        err = aptx_aa_flush(s->core, s->window, sizeof(s->window), &w);
        if (err)
            return err;
    }
    if (!w)
        return 0;
    s->deficit += (int64_t)s->period - (int64_t)s->consumed;
    s->consumed = 0;
    aptx_aa_swap16(s->window, out, w);
    *written = w;
    if (s->config.profile == APTX_AA_LL)
        s->deficit = 0;
    after_packet(s);
    return 0;
}

/* Implements aptx_aa_stream_request_level. */
int aptx_aa_stream_request_level(aptx_aa_stream *s, unsigned q) {
    aptx_aa_row r;
    int err;
    if (!s)
        return APTX_AA_INVALID;
    if (q == 1 && s->map_one)
        q = 2;
    err = level_row(s, q, &r);
    if (err)
        return err;
    s->pending = q;
    return 0;
}

/* Implements aptx_aa_stream_hold_level. */
void aptx_aa_stream_hold_level(aptx_aa_stream *s, int hold) {
    if (s)
        s->hold = !!hold;
}

/* Implements aptx_aa_stream_apply_level_now. */
int aptx_aa_stream_apply_level_now(aptx_aa_stream *s) {
    if (!s || s->consumed || s->hold || !s->pending)
        return 0;
    s->row = s->before.row;
    s->period = s->before.period;
    s->level = s->before.level;
    s->bitrate = s->before.bitrate;
    after_packet(s);
    return 1;
}

/* Implements aptx_aa_stream_reset. */
int aptx_aa_stream_reset(aptx_aa_stream *s, const aptx_aa_config *c, const aptx_aa_row *r) {
    int err;
    if (!s || !r || !r->period_ms)
        return APTX_AA_INVALID;
    err = aptx_aa_reset(s->core, c, r->bitrate, r->packet_bytes);
    if (err)
        return err;
    s->config = *c;
    s->row = *r;
    s->period = row_period(*c, *r);
    s->consumed = 0;
    s->deficit = 0;
    s->budget = AA_MAX(s->budget, r->packet_bytes);
    s->level = 3;
    s->pending = 0;
    s->bitrate = r->bitrate;
    save_step(s);
    return 0;
}

/* Implements aptx_aa_stream_get_row. */
int aptx_aa_stream_get_row(const aptx_aa_stream *s, aptx_aa_row *r) {
    if (!s || !r)
        return APTX_AA_INVALID;
    *r = s->row;
    return 0;
}

/* Implements aptx_aa_stream_level. */
unsigned aptx_aa_stream_level(const aptx_aa_stream *s) {
    return s ? s->level : 0;
}

/* Implements aptx_aa_stream_deficit. */
int64_t aptx_aa_stream_deficit(const aptx_aa_stream *s) {
    return s ? s->deficit : 0;
}

/* Implements aptx_aa_stream_map_level_one. */
void aptx_aa_stream_map_level_one(aptx_aa_stream *s, int enabled) {
    if (s)
        s->map_one = !!enabled;
}
