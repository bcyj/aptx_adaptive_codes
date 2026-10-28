#include "encoder_internal.h"

/* Implements quantise. */
static int32_t quantise(AABand *band, int32_t x, int32_t pred, uint8_t bits, int low_power, int top,
                        int32_t *carry, int32_t *recon) {
    unsigned bt, e, h, idx = 0, j;
    int32_t d, a, lo, hi, dl, dh, code, mask, v, q, c, rho, nr, r, ldmax;
    const AAQuantTable *tab;
    int64_t big_a;
    int32_t step;
    if (!bits) {
        band->step = 8;
        band->ld = 244;
        band->r = 0;
        *recon = 0;
        return 0;
    }
    bt = AA_MIN(bits, 9);
    e = bits > 9 ? bits - 9 : 0;
    tab = &QUANT[bt - 2];
    step = band->step;
    d = aa_sub(x, aa_wmul(pred, 256));
    a = d < 0 ? aa_i32(0u - (uint32_t)d) : d;
    big_a = (int64_t)a * 2048;
    for (h = 1u << (bt - 2); h; h >>= 1)
        if (big_a - (int64_t)tab->threshold[idx + h] * step >= 0)
            idx += h;
    lo = tab->threshold[idx];
    hi = tab->threshold[idx + 1];
    dl = tab->dither[idx];
    dh = tab->dither[idx + 1];
    code = (int32_t)idx;
    mask = (1 << e) - 1;
    if (e) {
        int32_t rr = 0, lp = lo, hp = hi;
        unsigned k;
        for (k = 0; k < e; k++) {
            int32_t mid = lo + ((hi - lo) >> 1);
            lp = lo;
            hp = hi;
            if (big_a - (int64_t)mid * step < 0) {
                code *= 2;
                rr *= 2;
                hi = mid;
            } else {
                code = 2 * code + 1;
                rr = 2 * rr + 1;
                lo = mid;
            }
        }
        lo = lp;
        hi = hp;
        dh -= aa_wmul(aa_wmul(mask, -1 - rr), dh - dl) >> e;
    }
    if (!low_power) {
        int32_t dith = band->dith;
        int32_t g = aa_sat24((int64_t)dl - dh), ee = aa_r23((int64_t)dith * dith);
        int64_t off = aa_r23(aa_mul64(2097152 - (int64_t)ee, g));
        int64_t thr = aa_sat24(off + hi + lo), spread = aa_mul64(dith, (int64_t)hi - lo);
        v = aa_r23(thr * 8388608 + (d >= 0 ? spread : -spread));
    } else
        v = aa_sat24((int64_t)*carry + hi + lo);
    if ((int64_t)a * 4096 - (int64_t)v * step < 0)
        code--;
    q = d < 0 ? ~code : code;
    c = q >> e;
    j = (unsigned)(c >= 0 ? c + 1 : -c);
    rho = tab->threshold[j];
    if (e) {
        int32_t sig = q < 0 ? -1 : 0, m9 = mask & (sig ^ q), m6 = mask & ~m9;
        rho -= aa_wmul(tab->threshold[j] - tab->threshold[j - 1], m6) >> e;
    }
    if (c < 0)
        rho = -rho;
    if (!low_power && bits <= 9)
        rho = aa_r23((int64_t)tab->dither[j] * band->dith + (int64_t)rho * 8388608);
    nr = aa_sat24(aa_i32((uint32_t)(((int64_t)band->step * rho) >> 19)));
    ldmax = 6399 - 256 * (int32_t)bits;
    band->ld =
        aa_clamp(aa_r23(INT64_C(8350720) * band->ld + (int64_t)tab->incr[j] * 8388608), 0, ldmax);
    band->step = LOG_TABLE[(band->ld >> 3) & 31] >> (24 - (int)bits - (band->ld >> 8));
    r = nr;
    if (low_power) {
        *carry = band->ld;
        if (top)
            r = band->r;
    }
    band->r = r;
    *recon = r;
    return q;
}

/* Implements aa_code. */
void aa_code(AAEngine *e, const int32_t *x, const uint8_t *bits, int32_t *q, int32_t *recon) {
    unsigned ch, k, s = e->p.bands;
    int32_t carry = 0;
    for (ch = 0; ch < 2; ch++) {
        AAChannel *c = &e->ch[ch];
        for (k = 0; k < s; k++) {
            unsigned i = ch * s + k;
            int32_t pred = c->pred[k].taps ? c->pred[k].pr : 0;
            q[i] = quantise(&c->band[k], x[i], pred, bits[i], e->p.variant == 1, k == s - 1, &carry,
                            &recon[i]);
        }
        if (e->p.variant != 1)
            aa_dither(c, q[ch * s], q[ch * s + 1], s);
        for (k = 0; k < s; k++)
            if (c->pred[k].taps)
                aa_predictor_run(&c->pred[k], recon[ch * s + k]);
    }
}
