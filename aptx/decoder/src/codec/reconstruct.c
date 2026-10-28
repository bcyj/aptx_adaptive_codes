#include "decoder_internal.h"

/* Implements dequantise. */
static int32_t dequantise(AABand *b, int32_t q, unsigned bits, int low_power, int top) {
    unsigned ext, mask, j;
    int32_t c, rho, nr, r, ldmax;
    const AAQuantTable *tab;
    if (!bits) {
        b->step = 8;
        b->ld = 244;
        b->r = 0;
        return 0;
    }
    ext = bits > 9 ? bits - 9 : 0;
    mask = (1u << ext) - 1;
    tab = &QUANT[AA_MIN(bits, 9) - 2];
    c = q >> ext;
    j = (unsigned)(c >= 0 ? c + 1 : -c);
    rho = tab->threshold[j];
    if (ext) {
        int32_t sign = q < 0 ? -1 : 0;
        int32_t m9 = (int32_t)mask & (sign ^ q), m6 = (int32_t)mask & ~m9;
        rho -= aa_wmul(tab->threshold[j] - tab->threshold[j - 1], m6) >> ext;
    }
    if (c < 0)
        rho = -rho;
    if (!low_power && bits <= 9)
        rho = aa_r23((int64_t)tab->dither[j] * b->dith + (int64_t)rho * 8388608);
    nr = aa_sat24(aa_i32((uint32_t)(((int64_t)b->step * rho) >> 19)));
    ldmax = 6399 - 256 * (int32_t)bits;
    b->ld = aa_clamp(aa_r23(INT64_C(8350720) * b->ld + (int64_t)tab->incr[j] * 8388608), 0, ldmax);
    b->step = LOG_TABLE[(b->ld >> 3) & 31] >> (24 - (int)bits - (b->ld >> 8));
    r = low_power && top ? b->r : nr;
    b->r = r;
    return r;
}

/* Implements aa_reconstruct. */
void aa_reconstruct(AAEngine *e, const uint8_t *bits, const int32_t *q, int32_t *out,
                    int32_t *recon) {
    unsigned ch, k, s = e->p.bands;
    for (ch = 0; ch < 2; ch++) {
        AAChannel *c = &e->ch[ch];
        for (k = 0; k < s; k++) {
            unsigned i = ch * s + k;
            int32_t pred = c->pred[k].taps ? c->pred[k].pr : 0;
            recon[i] = dequantise(&c->band[k], q[i], bits[i], e->p.variant == 1, k == s - 1);
            out[i] = aa_sat24((int64_t)pred + recon[i]);
        }
        if (e->p.variant != 1)
            aa_dither(c, q[ch * s], q[ch * s + 1], s);
        for (k = 0; k < s; k++)
            if (c->pred[k].taps)
                aa_predictor_run(&c->pred[k], recon[ch * s + k]);
    }
}
