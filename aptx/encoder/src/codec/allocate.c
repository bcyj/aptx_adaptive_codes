#include "encoder_internal.h"

/* Implements pool_rate. */
static uint32_t pool_rate(AAParams p, uint32_t rate) {
    uint32_t lo = p.variant == 3 ? 90000 : 200000;
    uint32_t hi = p.variant == 3 ? 1100000 : (p.variant == 4 ? 420000 : 700000);
    return aa_clampu(rate, lo, hi);
}

/* Implements aa_pool. */
uint32_t aa_pool(AAParams p, uint32_t target) {

    uint32_t r = pool_rate(p, target);
    return (uint32_t)(r / p.rate * p.bands + r % p.rate * p.bands / p.rate);
}

/* Implements max_bits. */
static const uint8_t *max_bits(AAParams p, uint32_t r, int tonal) {
    if (p.bands == 4)
        return r <= 319999 ? MAX_BITS_4BAND_320
                           : (r <= 369999 ? MAX_BITS_4BAND_370 : MAX_BITS_4BAND_MAX);
    if (p.rate <= 48000) {
        if (p.variant == 4 && (tonal || r > 419999))
            return MAX_BITS_8BAND_420;
        if (tonal)
            return MAX_BITS_8BAND_MAX;
        if (r <= 249999)
            return MAX_BITS_8BAND_250;
        if (r <= 319999)
            return MAX_BITS_8BAND_320;
        if (r <= 369999)
            return MAX_BITS_8BAND_370;
        if (r <= 419999)
            return MAX_BITS_8BAND_420;
        return r <= 519999 ? MAX_BITS_8BAND_520 : MAX_BITS_8BAND_MAX;
    }
    if (tonal)
        return MAX_BITS_8BAND_96K_MAX;
    if (r <= 419999)
        return MAX_BITS_8BAND_96K_420;
    if (r <= 499999)
        return MAX_BITS_8BAND_96K_500;
    if (r <= 599999)
        return MAX_BITS_8BAND_96K_600;
    return r <= 649999 ? MAX_BITS_8BAND_96K_650 : MAX_BITS_8BAND_96K_MAX;
}

/* Implements scale_factor. */
static int scale_factor(uint32_t a) {
    uint64_t j = (((uint64_t)a << 24) + (UINT64_C(1) << 31)) >> 32;
    if (j > INT32_MAX)
        j = INT32_MAX;
    return j > 1 ? (int)audio_math_ceil_log2_u32((uint32_t)j) - 1 : 0;
}

/* Implements slice. */
static int slice(int pool, const int *need, const uint8_t *cap, unsigned w, int m) {
    int l = m + 1, acc = 0, prev = 0, cnt;
    if (m <= -12)
        return pool == 0 ? l - 1 : l;
    for (;;) {
        unsigned i;
        cnt = 0;
        for (i = 0; i < w; i++)
            cnt += need[i] == l ? 2 : (need[i] > l && (int)cap[i] + l - 1 > need[i]);
        acc += prev;
        if (cnt + acc >= pool || l <= -10)
            break;
        prev = cnt;
        l--;
    }
    return acc + cnt == pool ? l - 2 : l - 1;
}

/* Implements distribute. */
static void distribute(unsigned s, int pool, const int *need, const int *maxb, int sl, int *out) {
    unsigned i, k, w = 2 * s;
    int total = 0;
    memset(out, 0, w * sizeof(int));
    out[0] = 2;
    out[s] = 2;
    for (i = 0; i < w; i++) {
        if (need[i] >= sl + 2)
            out[i] = AA_MIN(need[i] - sl, maxb[i]);
        total += out[i];
    }
    if (total < pool) {
        for (k = 0; k < s; k++)
            for (i = k; i < w; i += s) {
                if (out[i] > 1 && maxb[i] > out[i]) {
                    out[i]++;
                    total++;
                } else if (need[i] == sl + 1 && pool > total + 1 && maxb[i] > 0) {
                    out[i] = 2;
                    total += 2;
                }
                if (total >= pool)
                    goto filled;
            }
    }
filled:
    if (total < pool)
        for (k = 0; k < 32; k++) {
            i = k % w;
            if (out[i] > 1 && maxb[i] > out[i]) {
                out[i]++;
                total++;
            } else if (out[i] == 0 && maxb[i] > 0) {
                out[i] = 2;
                total += 2;
            }
            if (total >= pool)
                break;
        }
    if (total > pool)
        for (k = w; k > 0; k--) {
            i = k - 1;
            if (out[i] > 2) {
                out[i]--;
                total--;
            }
            if (total <= pool)
                break;
        }
}

/* Implements aa_allocate. */
uint32_t aa_allocate(AAParams p, uint32_t target, uint32_t nominal, const int32_t *x, size_t n,
                     uint8_t *bits, AAAllocScratch *scratch) {
    uint32_t *peak = scratch->peak, pool = aa_pool(p, target);
    int *sf = scratch->sf, *need = scratch->need, *caps = scratch->caps, *o = scratch->out;
    int tonal = 0, alt = p.variant == 3 && nominal <= 179999, top = 0, sl;
    const uint8_t *mb;
    const int8_t *off;
    unsigned i, ch, ri = 0, s = p.bands, w = p.width;
    size_t j;
    memset(peak, 0, 16 * sizeof(*peak));
    memset(sf, 0, 16 * sizeof(*sf));
    for (j = 0; j < n; j++) {
        uint32_t a = x[j] < 0 ? 0u - (uint32_t)x[j] : (uint32_t)x[j];
        i = (unsigned)(j & (w - 1));
        if (a > peak[i])
            peak[i] = a;
    }
    for (i = 0; i < w; i++)
        sf[i] = scale_factor(peak[i]);
    if (s == 8)
        for (ch = 0; ch < 2; ch++) {
            int hi = sf[ch * s], lo = hi;
            unsigned count = 0;
            for (i = 0; i < s; i++) {
                hi = AA_MAX(hi, sf[ch * s + i]);
                lo = AA_MIN(lo, sf[ch * s + i]);
            }
            for (i = 0; i < s; i++)
                count += (sf[ch * s + i] < hi - 10);
            if (hi - lo > 10 && count >= 6)
                tonal = 1;
        }
    if (alt)
        memset(sf + s, 0, s * sizeof(int));
    ri = (p.rate > 16000) + (p.rate > 32000) + (p.rate > 44100) + (p.rate > 48000);
    off = (s == 4 ? OFFSET_4BAND_DUAL_MONO
                  : (tonal ? OFFSET_8BAND_DUAL_MONO_TONE : OFFSET_8BAND_DUAL_MONO)) +
          w * ri;
    for (i = 0; i < w; i++) {
        int d = sf[i] - off[i];
        need[i] = sf[i] == 0 ? -5 : (d > 0 ? (d + 1) >> 1 : d);
        top = AA_MAX(top, need[i]);
    }
    mb = max_bits(p, pool_rate(p, target), tonal);
    sl = slice((int)pool, need, mb, w, top);
    for (i = 0; i < w; i++)
        caps[i] = mb[i];
    distribute(s, (int)pool, need, caps, sl, o);
    if (p.variant == 3) {
        int sums[2] = {0}, total;
        for (i = 0; i < w; i++)
            if (sf[i])
                sums[i / s] += o[i];
        for (i = 0; i < w; i++)
            if (!sf[i])
                o[i] = 0;
        total = sums[0] + sums[1];
        if (total) {
            if (!alt) {
                if (!sums[1]) {
                    o[s] = 2;
                    total += 2;
                }
                if (!sums[0]) {
                    o[0] = 2;
                    total += 2;
                }
            }
            if (total >= 1 && total <= 6) {
                int extra = 7 - total, pass;
                for (pass = 0; pass < 2; pass++)
                    for (i = 0; i < w; i++) {
                        if (extra <= 0)
                            goto done;
                        if (pass || o[i] > 0) {
                            int add = AA_MAX(0, AA_MIN(extra, caps[i] - o[i]));
                            o[i] += add;
                            extra -= add;
                            if (o[i] == 1) {
                                o[i] = 2;
                                extra--;
                            }
                        }
                    }
            }
        }
    }
done:
    for (i = 0; i < w; i++)
        bits[i] = (uint8_t)o[i];
    return pool;
}
