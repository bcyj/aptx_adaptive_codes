#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include "host_allocator.h"
#include <aptx/decoder/decoder.h>
#include <audio_math/byte_order.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Implements wav_header. */
static int wav_header(FILE *f, uint32_t rate, unsigned bits, uint32_t bytes) {
    unsigned char b[44] = {0};
    unsigned align = bits / 8 * 2;
    memcpy(b, "RIFF", 4);
    audio_math_write_le32(b + 4, bytes + 36);
    memcpy(b + 8, "WAVEfmt ", 8);
    audio_math_write_le32(b + 16, 16);
    audio_math_write_le16(b + 20, 1);
    audio_math_write_le16(b + 22, 2);
    audio_math_write_le32(b + 24, rate);
    audio_math_write_le32(b + 28, rate * align);
    audio_math_write_le16(b + 32, (uint16_t)align);
    audio_math_write_le16(b + 34, (uint16_t)bits);
    memcpy(b + 36, "data", 4);
    audio_math_write_le32(b + 40, bytes);
    return fwrite(b, 1, sizeof(b), f) == sizeof(b) ? 0 : -1;
}

/* Implements write_pcm. */
static int write_pcm(FILE *f, const int32_t *pcm, size_t n, unsigned bits, int raw, size_t *skip,
                     uint64_t *total) {
    unsigned char b[4096];
    size_t at = 0;
    if (*skip) {
        size_t take = *skip < n ? *skip : n;
        pcm += take;
        n -= take;
        *skip -= take;
    }
    while (at < n) {
        size_t j, take = n - at, bytes;
        unsigned width = raw ? 4 : bits / 8;
        if (take > sizeof(b) / width)
            take = sizeof(b) / width;
        for (j = 0; j < take; j++) {
            uint32_t v = (uint32_t)pcm[at + j];
            if (width == 2)
                audio_math_write_le16(b + j * 2, (uint16_t)(pcm[at + j] >> 8));
            else if (width == 3)
                audio_math_write_le_bytes(b + j * 3, v, 3);
            else
                audio_math_write_le32(b + j * 4, raw ? v : v << 8);
        }
        bytes = take * width;
        if (!raw && *total + bytes > UINT32_MAX - 36)
            return -1;
        if (fwrite(b, 1, bytes, f) != bytes)
            return -1;
        *total += bytes;
        at += take;
    }
    return 0;
}

/* Implements number. */
static int number(const char *s, unsigned *out) {
    char *end;
    unsigned long v;
    errno = 0;
    if (!*s || *s == '-')
        return -1;
    v = strtoul(s, &end, 10);
    if (errno || *end || v > UINT32_MAX)
        return -1;
    *out = (unsigned)v;
    return 0;
}

/* Implements geometry. */
static int geometry(const unsigned char b[4], unsigned *variant, uint32_t *rate, size_t *bytes) {
    unsigned type, h, body;
    if (b[0] == 0) {
        type = b[1] & 127;
        *bytes = (size_t)(b[2] + 3) * 4;
        body = b[3];
    } else if (b[1] == 0) {
        type = b[0] & 127;
        *bytes = (size_t)(b[3] + 3) * 4;
        body = b[2];
    } else
        return -1;
    if (type < 1 || type > 4 || *bytes < 12 || *bytes > APTX_AA_MAX_PACKET || !(body & 128))
        return -1;
    *variant = type == 1 ? 0 : (type == 2 ? 1 : type);
    h = (body >> 3) & 15;
    if (type == 2) {
        if (h != 9 && h != 11)
            return -1;
        h--;
    }
    if (h == 8)
        *rate = 44100;
    else if (h == 10)
        *rate = 48000;
    else if (h == 12 && type == 3)
        *rate = 88200;
    else if (h == 14 && type == 3)
        *rate = 96000;
    else
        return -1;
    return 0;
}

/* Implements usage. */
static void usage(void) {
    puts("aptx_aa_decode -i input.aptxaa -o output.wav [options]\n"
         "  --bits 16|24|32 WAV integer PCM depth (default 24)\n"
         "  --raw           Write right-justified signed 24-bit in LE int32 slots\n"
         "  --keep-delay    Retain analysis/synthesis delay (default: trim it)\n"
         "  --rate HZ       Verify rate against first packet (no resampling)\n"
         "  --variant N     Verify config 0,1,3,4 against first packet\n"
         "Rate, packet lengths, config and adjacent-byte swap are detected.\n"
         "Input must start at the first codec packet. No RTP/A2DP headers.\n"
         "The final incomplete code pair is discarded; synthesis tail is emitted.");
}

/* Implements main. */
int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL;
    unsigned bits = 24, requested_rate = 0, requested_variant = 99;
    int raw = 0, keep = 0, status = 1, j;
    FILE *in = NULL, *out = NULL;
    aptx_aa_dec *d = NULL;
    unsigned char packet[APTX_AA_MAX_PACKET];
    int32_t *pcm = NULL;
    size_t capacity = 8192, skip = 0, packets = 0;
    uint32_t rate;
    unsigned variant;
    size_t bytes;
    uint64_t total = 0;
    for (j = 1; j < argc; j++) {
        const char *arg = argv[j], *v;
        unsigned n;
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            usage();
            return 0;
        }
        if (!strcmp(arg, "--raw")) {
            raw = 1;
            continue;
        }
        if (!strcmp(arg, "--keep-delay")) {
            keep = 1;
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
        else {
            if (number(v, &n)) {
                fprintf(stderr, "Invalid number: %s\n", v);
                return 1;
            }
            if (!strcmp(arg, "--bits") && (n == 16 || n == 24 || n == 32))
                bits = n;
            else if (!strcmp(arg, "--rate"))
                requested_rate = n;
            else if (!strcmp(arg, "--variant") && (n == 0 || n == 1 || n == 3 || n == 4))
                requested_variant = n;
            else {
                fprintf(stderr, "Invalid option/value: %s %s\n", arg, v);
                return 1;
            }
        }
    }
    if (!input || !output) {
        usage();
        return 1;
    }
    if (!strcmp(input, output)) {
        fputs("Input/output paths must be different.\n", stderr);
        return 1;
    }
    in = fopen(input, "rb");
    if (!in) {
        perror(input);
        goto done;
    }
    if (fread(packet, 1, 4, in) != 4 || geometry(packet, &variant, &rate, &bytes)) {
        fputs("Input is not a supported first codec packet.\n", stderr);
        goto done;
    }
    if ((requested_rate && requested_rate != rate) ||
        (requested_variant != 99 && requested_variant != variant)) {
        fputs("Requested config/rate differs from the stream.\n", stderr);
        goto done;
    }
    d = aptx_aa_decoder_create_variant_with_allocator(variant, rate, &example_allocator);
    if (!d) {
        fputs("Decoder initialization failed.\n", stderr);
        goto done;
    }
    if (!keep)
        skip = 2 * aptx_aa_decoder_delay(d);
    pcm = (int32_t *)malloc(capacity * sizeof(*pcm));
    if (!pcm)
        goto done;
    out = fopen(output, "wb");
    if (!out) {
        perror(output);
        goto done;
    }
    if (!raw && wav_header(out, rate, bits, 0))
        goto write_error;
    for (;;) {
        size_t n;
        int err;
        unsigned type;
        if (packet[0] == 0) {
            bytes = (size_t)(packet[2] + 3) * 4;
            type = packet[1] & 127;
        } else if (packet[1] == 0) {
            bytes = (size_t)(packet[3] + 3) * 4;
            type = packet[0] & 127;
        } else {
            fputs("Invalid packet marker.\n", stderr);
            goto done;
        }
        if (bytes < 12 || bytes > sizeof(packet) ||
            type != (variant == 0 ? 1 : (variant == 1 ? 2 : variant))) {
            fputs("Invalid packet geometry or config change; reset is required.\n", stderr);
            goto done;
        }
        if (fread(packet + 4, 1, bytes - 4, in) != bytes - 4) {
            fputs("Truncated physical packet.\n", stderr);
            goto done;
        }
        err = aptx_aa_decoder_push(d, packet, bytes, pcm, capacity, &n);
        if (err == APTX_AA_OUTPUT_TOO_SMALL) {
            fprintf(stderr, "Packet %zu needs %zu output samples; fixed capacity is %zu.\n",
                    packets, n, capacity);
            goto done;
        }
        if (err) {
            fprintf(stderr, "Packet %zu: %s\n", packets, aptx_aa_strerror(err));
            goto done;
        }
        if (write_pcm(out, pcm, n, bits, raw, &skip, &total))
            goto write_error;
        packets++;
        n = fread(packet, 1, 4, in);
        if (!n && !ferror(in))
            break;
        if (n != 4) {
            fputs("Truncated packet header/read failure.\n", stderr);
            goto done;
        }
    }
    {
        size_t n;
        int err = aptx_aa_decoder_finish(d, pcm, capacity, &n);
        if (err == APTX_AA_OUTPUT_TOO_SMALL) {
            fprintf(stderr, "Finish needs %zu output samples; fixed capacity is %zu.\n", n,
                    capacity);
            goto done;
        }
        if (err) {
            fprintf(stderr, "Finish: %s\n", aptx_aa_strerror(err));
            goto done;
        }
        if (write_pcm(out, pcm, n, bits, raw, &skip, &total))
            goto write_error;
    }
    if (!raw && (fseek(out, 0, SEEK_SET) || wav_header(out, rate, bits, (uint32_t)total)))
        goto write_error;
    if (fflush(out))
        goto write_error;
    fprintf(stderr, "Decoded %zu packets, config %u, %u Hz; %llu stereo frames.\n", packets,
            variant, rate, (unsigned long long)(total / (raw ? 8 : bits / 8 * 2)));
    status = 0;
    goto done;
write_error:
    fputs("Output write failed or RIFF size limit exceeded.\n", stderr);
done:
    if (in)
        fclose(in);
    if (out && fclose(out))
        status = 1;
    free(pcm);
    aptx_aa_decoder_destroy(d);
    return status;
}
