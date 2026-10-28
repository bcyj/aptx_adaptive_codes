#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include "host_allocator.h"
#include <aptx/encoder/encoder.h>
#include <audio_math/byte_order.h>
#include <audio_math/integer.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    uint32_t rate;
    unsigned bytes;
    int right24;
    uint64_t remaining;
} Input;

/* Implements skip. */
static int skip(FILE *f, uint32_t n) {
    unsigned char buf[4096];
    while (n) {
        size_t take = n < sizeof(buf) ? n : sizeof(buf);
        if (fread(buf, 1, take, f) != take)
            return -1;
        n -= (uint32_t)take;
    }
    return 0;
}

/* Implements wav_header. */
static int wav_header(Input *in) {
    unsigned char h[40];
    int format_seen = 0;
    if (fread(h, 1, 12, in->file) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return -1;
    while (fread(h, 1, 8, in->file) == 8) {
        uint32_t n = audio_math_read_le32(h + 4);
        if (!memcmp(h, "fmt ", 4)) {
            unsigned tag, channels, bits, align;
            size_t take = n < 40 ? n : 40;
            if (n < 16 || fread(h, 1, take, in->file) != take)
                return -1;
            tag = audio_math_read_le16(h);
            channels = audio_math_read_le16(h + 2);
            in->rate = audio_math_read_le32(h + 4);
            align = audio_math_read_le16(h + 12);
            bits = audio_math_read_le16(h + 14);
            if (tag == 0xfffe) {
                static const unsigned char pcm_guid[16] = {1,   0, 0, 0,   0, 0,  16,  0,
                                                           128, 0, 0, 170, 0, 56, 155, 113};
                if (n < 40 || audio_math_read_le16(h + 16) < 22 || memcmp(h + 24, pcm_guid, 16))
                    return -1;
                tag = 1;
            }
            if (tag != 1 || channels != 2 || (bits != 16 && bits != 24 && bits != 32) ||
                align != bits / 8 * 2)
                return -1;
            in->bytes = bits / 8;
            in->right24 = 0;
            format_seen = 1;
            if (skip(in->file, n - (uint32_t)take))
                return -1;
        } else if (!memcmp(h, "data", 4)) {
            if (!format_seen || n % (2 * in->bytes))
                return -1;
            in->remaining = n;
            return 0;
        } else if (skip(in->file, n))
            return -1;
        if ((n & 1) && skip(in->file, 1))
            return -1;
    }
    return -1;
}

/* Implements read_pcm. */
static int read_pcm(Input *in, int32_t *out, size_t max_frames, size_t *samples) {
    unsigned char bytes[8192 * 2 * 4];
    size_t want = max_frames * 2 * in->bytes, n, j;
    if (in->remaining != UINT64_MAX && want > in->remaining)
        want = (size_t)in->remaining;
    n = fread(bytes, 1, want, in->file);
    if (ferror(in->file) || (in->remaining != UINT64_MAX && n != want) || n % (2 * in->bytes))
        return -1;
    if (in->remaining != UINT64_MAX)
        in->remaining -= n;
    *samples = n / in->bytes;
    for (j = 0; j < *samples; j++) {
        const unsigned char *p = bytes + j * in->bytes;
        uint32_t u;
        int32_t v;
        if (in->bytes == 2) {
            u = (uint32_t)audio_math_read_le16(p) << 16;
            v = audio_math_shift_right_bounded_i32(audio_math_int32_from_bits(u), 8);
        } else if (in->bytes == 3) {
            u = audio_math_read_le24(p) << 8;
            v = audio_math_shift_right_bounded_i32(audio_math_int32_from_bits(u), 8);
        } else {
            u = audio_math_read_le32(p);
            v = audio_math_int32_from_bits(u);
            if (!in->right24)
                v = audio_math_shift_right_bounded_i32(v, 8);
        }
        if (v < -8388608 || v > 8388607)
            return -1;
        out[j] = v;
    }
    return 0;
}

/* Implements usage. */
static void usage(void) {
    puts("aptx_aa_encode -i input.wav -o output.aptxaa [options]\n"
         "  --raw FORMAT    Raw input: s16le, s24le, s32le, s32le24\n"
         "                  s32le24 is right-justified signed 24-bit in int32\n"
         "  --rate HZ       Raw rate (default 48000); WAV rate is read from header\n"
         "  --decoder r1|r2 Compatibility level (default r1)\n"
         "  --profile hq|ll Music mode (default hq)\n"
         "  --quality 1..7  Initial table row (default 3)\n"
         "  --bitrate BPS   Override table bitrate\n"
         "  --packet BYTES  Override fixed codec packet size, multiple of 4\n"
         "  --period MS     Override packet period\n"
         "  --chunk FRAMES  Input frames per encode call (default rate/1000)\n"
         "  --variant N     Expert override: 0,1,3,4\n"
         "  --wire          Swap adjacent bytes for the transport payload\n"
         "  --sizes FILE    Write: packet_index bytes forced_flush\n"
         "  --flush         Close pending encoder packet at EOF\n"
         "Raw codec packets are concatenated; no RTP/A2DP headers are added.\n"
         "Without --flush, stop exactly at the reference loop's last packet.\n"
         "Neither mode pads PCM or guarantees emission of the codec carry tail.");
}

/* Implements number. */
static int number(const char *s, uint32_t *n) {
    char *end;
    unsigned long v;
    if (!*s || *s == '-')
        return -1;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || *end || v > UINT32_MAX || !v)
        return -1;
    *n = (uint32_t)v;
    return 0;
}

/* Implements write_packet. */
static int write_packet(FILE *out, FILE *sizes, uint8_t *packet, size_t n, int wire,
                        size_t *packets, int forced) {
    if (wire)
        aptx_aa_swap16(packet, packet, n);
    if (fwrite(packet, 1, n, out) != n)
        return -1;
    if (sizes && fprintf(sizes, "%zu %zu %d\n", *packets, n, forced) < 0)
        return -1;
    ++*packets;
    return 0;
}

/* Implements main. */
int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL, *raw = NULL, *sizepath = NULL;
    aptx_aa_config c = {48000, 2, APTX_AA_HQ, APTX_AA_R1};
    aptx_aa_row row;
    uint32_t bitrate = 0, packet_bytes = 0, period_ms = 0, chunk = 0, quality = 3;
    int variant = -1, wire = 0, flush_end = 0, rate_set = 0, status = 1, j;
    Input in = {0};
    FILE *out = NULL, *sizes = NULL;
    aptx_aa_encoder *e = NULL;
    int32_t *queue = NULL;
    uint8_t packet[APTX_AA_MAX_PACKET];
    size_t queued = 0, consumed = 0, period, packets = 0;
    uint64_t frames = 0;
    int64_t deficit = 0;
    for (j = 1; j < argc; j++) {
        const char *arg = argv[j], *v;
        uint32_t n;
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            usage();
            return 0;
        }
        if (!strcmp(arg, "--wire")) {
            wire = 1;
            continue;
        }
        if (!strcmp(arg, "--flush")) {
            flush_end = 1;
            continue;
        }
        if (j + 1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", arg);
            return 1;
        }
        v = argv[++j];
        if (!strcmp(arg, "-i"))
            input = v;
        else if (!strcmp(arg, "-o"))
            output = v;
        else if (!strcmp(arg, "--raw"))
            raw = v;
        else if (!strcmp(arg, "--sizes"))
            sizepath = v;
        else if (!strcmp(arg, "--profile")) {
            if (!strcmp(v, "hq"))
                c.profile = APTX_AA_HQ;
            else if (!strcmp(v, "ll"))
                c.profile = APTX_AA_LL;
            else
                goto bad_arg;
        } else if (!strcmp(arg, "--decoder")) {
            if (!strcmp(v, "r1"))
                c.decoder = APTX_AA_R1;
            else if (!strcmp(v, "r2"))
                c.decoder = APTX_AA_R2;
            else
                goto bad_arg;
        } else if (!strcmp(arg, "--variant") && !strcmp(v, "0"))
            variant = 0;
        else {
            if (number(v, &n))
                goto bad_arg;
            if (!strcmp(arg, "--rate")) {
                c.sample_rate = n;
                rate_set = 1;
            } else if (!strcmp(arg, "--quality") && n <= 7)
                quality = n;
            else if (!strcmp(arg, "--bitrate"))
                bitrate = n;
            else if (!strcmp(arg, "--packet"))
                packet_bytes = n;
            else if (!strcmp(arg, "--period"))
                period_ms = n;
            else if (!strcmp(arg, "--chunk") && n <= 8192)
                chunk = n;
            else if (!strcmp(arg, "--variant") && (n == 1 || n == 3 || n == 4))
                variant = (int)n;
            else
                goto bad_arg;
        }
        continue;
    bad_arg:
        fprintf(stderr, "Invalid option/value: %s %s\n", arg, v);
        return 1;
    }
    if (!input || !output) {
        usage();
        return 1;
    }
    if (!strcmp(input, output) ||
        (sizepath && (!strcmp(input, sizepath) || !strcmp(output, sizepath)))) {
        fputs("Input, output and sizes paths must be different.\n", stderr);
        return 1;
    }
    in.file = fopen(input, "rb");
    if (!in.file) {
        perror(input);
        goto done;
    }
    if (raw) {
        if (!strcmp(raw, "s16le"))
            in.bytes = 2;
        else if (!strcmp(raw, "s24le"))
            in.bytes = 3;
        else if (!strcmp(raw, "s32le") || !strcmp(raw, "s32le24")) {
            in.bytes = 4;
            in.right24 = !strcmp(raw, "s32le24");
        } else {
            fputs("Unknown raw format.\n", stderr);
            goto done;
        }
        in.rate = c.sample_rate;
        in.remaining = UINT64_MAX;
    } else {
        if (wav_header(&in)) {
            fputs("Expected stereo RIFF/WAVE integer PCM16/24/32.\n", stderr);
            goto done;
        }
        if (rate_set && c.sample_rate != in.rate) {
            fputs("--rate differs from WAV header; resampling is not performed.\n", stderr);
            goto done;
        }
        c.sample_rate = in.rate;
    }
    if (!chunk)
        chunk = c.sample_rate / 1000;

    if (bitrate && packet_bytes && period_ms)
        memset(&row, 0, sizeof(row));
    else {
        int err = aptx_aa_table_row(&c, NULL, APTX_AA_MAX_PACKET, quality, &row);
        if (err) {
            fprintf(stderr, "Table row: %s\n", aptx_aa_strerror(err));
            goto done;
        }
    }
    if (!bitrate)
        bitrate = row.bitrate;
    if (!packet_bytes)
        packet_bytes = (uint32_t)row.packet_bytes;
    if (!period_ms)
        period_ms = row.period_ms;
    period = (size_t)((uint64_t)period_ms * c.sample_rate * 2 / 1000);
    if (!period || (uint64_t)chunk * 2000 > (uint64_t)period_ms * c.sample_rate * 2) {
        fputs("Chunk must not exceed the packet period.\n", stderr);
        goto done;
    }
    e = variant < 0
            ? aptx_aa_create_with_allocator(&c, bitrate, packet_bytes, &example_allocator)
            : aptx_aa_create_variant_with_allocator((unsigned)variant, c.sample_rate, bitrate,
                                                    packet_bytes, &example_allocator);
    if (!e) {
        fputs("Unsupported encoder configuration or allocation failure.\n", stderr);
        goto done;
    }
    queue = (int32_t *)malloc(APTX_AA_MAX_QUEUED_SAMPLES * sizeof(*queue));
    if (!queue)
        goto done;
    out = fopen(output, "wb");
    if (!out) {
        perror(output);
        goto done;
    }
    if (sizepath) {
        sizes = fopen(sizepath, "w");
        if (!sizes) {
            perror(sizepath);
            goto done;
        }
    }
    for (;;) {
        size_t n, w;
        aptx_aa_progress p;
        int err, forced = 0;
        if (queued + 2 * chunk > APTX_AA_MAX_QUEUED_SAMPLES) {
            fputs("PCM backlog too large; check bitrate, period and packet size.\n", stderr);
            goto done;
        }
        if (read_pcm(&in, queue + queued, chunk, &n)) {
            fputs("Truncated or invalid PCM input.\n", stderr);
            goto done;
        }
        if (!n)
            break;
        frames += n / 2;
        queued += n;
        err = aptx_aa_encode(e, queue, queued, NULL, 0, packet, sizeof(packet), &p);
        if (err) {
            fprintf(stderr, "encode: %s\n", aptx_aa_strerror(err));
            goto done;
        }
        queued -= p.consumed;
        memmove(queue, queue + p.consumed, queued * sizeof(*queue));
        consumed += p.consumed;
        w = p.written;
        if (!w && (int64_t)consumed < (int64_t)period + deficit)
            continue;
        if (!w) {
            forced = 1;
            err = aptx_aa_flush(e, packet, sizeof(packet), &w);
            if (err) {
                fprintf(stderr, "flush: %s\n", aptx_aa_strerror(err));
                goto done;
            }
        }
        if (!w)
            continue;
        if (write_packet(out, sizes, packet, w, wire, &packets, forced)) {
            fputs("Output write failed.\n", stderr);
            goto done;
        }
        deficit += (int64_t)period - (int64_t)consumed;
        consumed = 0;
        if (c.profile == APTX_AA_LL)
            deficit = 0;
        aptx_aa_set_bitrate(e, deficit > 96 ? bitrate - bitrate / 4 : bitrate);
    }
    if (flush_end) {
        size_t w;
        int err = aptx_aa_flush(e, packet, sizeof(packet), &w);
        if (err || (w && write_packet(out, sizes, packet, w, wire, &packets, 1))) {
            fputs("Final flush failed.\n", stderr);
            goto done;
        }
    }
    if (fflush(out) || (sizes && fflush(sizes))) {
        fputs("Output flush failed.\n", stderr);
        goto done;
    }
    fprintf(stderr,
            "Encoded %llu stereo frames, %zu packets x %u bytes; %zu queued samples at EOF.\n",
            (unsigned long long)frames, packets, packet_bytes, queued);
    status = 0;
done:
    if (in.file)
        fclose(in.file);
    if (out && fclose(out))
        status = 1;
    if (sizes && fclose(sizes))
        status = 1;
    aptx_aa_destroy(e);
    free(queue);
    return status;
}
