#include "aa_synthesis.h"
#if APTX_AA_DECODER_FIXED_POINT
#if APTX_AA_OPT_SYNTH
#define AA_SYNTH_MUL(a, b) ((int64_t)(a) * (b))
#else
#define AA_SYNTH_MUL(a, b) aa_mul64((int64_t)(a), (int64_t)(b))
#endif
#else
#define AA_SYNTH_MUL(a, b) ((AASynthAccum)(a) * (b))
#endif

/* Implements aa_synthesis_init. */
void aa_synthesis_init(AASynthesis *s, AAParams p) {
    memset(s, 0, sizeof(*s));
    s->bands = p.bands;
    switch (p.variant) {
    case 0:
        s->taps = 94;
        s->fir = &AA_SYNTH_0[0][0];
        break;
    case 1:
        s->taps = 70;
        s->fir = &AA_SYNTH_1[0][0];
        break;
    case 3:
        s->taps = 256;
        s->fir = &AA_SYNTH_3[0][0];
        break;
    default:
        s->taps = 128;
        s->fir = &AA_SYNTH_4[0][0];
        break;
    }
}
#if APTX_AA_DECODER_FIXED_POINT

/* Implements pcm_round. */
static int32_t pcm_round(int64_t v) {

    return aa_sat24((v + (INT64_C(1) << (AA_SYNTH_FRAC_BITS - 1))) >> AA_SYNTH_FRAC_BITS);
}
#else

/* Implements pcm_round. */
static int32_t pcm_round(double v) {
    int32_t i;
    if (v >= 8388607.0)
        return 8388607;
    if (v <= -8388608.0)
        return -8388608;
    v += 0.5;
    i = (int32_t)v;
    return v < (double)i ? i - 1 : i;
}
#endif

/* Implements aa_synthesis_run. */
void aa_synthesis_run(AASynthesis *s, const int32_t *bands, int32_t *pcm) {
    unsigned ch, lag, j;
    unsigned taps = s->taps, bands_count = s->bands;
    unsigned base = s->pos + taps - 1;
    for (ch = 0; ch < 2; ch++) {
        if (bands_count == 4) {
            AASynthAccum b0 = bands[ch * 4 + 0];
            AASynthAccum b1 = bands[ch * 4 + 1];
            AASynthAccum b2 = bands[ch * 4 + 2];
            AASynthAccum b3 = bands[ch * 4 + 3];
            const AASynthCoeff *f0 = &s->fir[0 * taps];
            const AASynthCoeff *f1 = &s->fir[1 * taps];
            const AASynthCoeff *f2 = &s->fir[2 * taps];
            const AASynthCoeff *f3 = &s->fir[3 * taps];
#if APTX_AA_OPT_SYNTH
            unsigned idx = base & 511;
#endif
            for (lag = 0; lag < taps; lag++) {
                AASynthAccum acc = AA_SYNTH_MUL(f0[lag], b0) + AA_SYNTH_MUL(f1[lag], b1) +
                                   AA_SYNTH_MUL(f2[lag], b2) + AA_SYNTH_MUL(f3[lag], b3);
#if APTX_AA_OPT_SYNTH
                s->overlap[ch][idx] += acc;
                idx = (idx - 1) & 511;
#else
                s->overlap[ch][(base - lag) & 511] += acc;
#endif
            }
        } else if (bands_count == 8) {
            AASynthAccum b0 = bands[ch * 8 + 0];
            AASynthAccum b1 = bands[ch * 8 + 1];
            AASynthAccum b2 = bands[ch * 8 + 2];
            AASynthAccum b3 = bands[ch * 8 + 3];
            AASynthAccum b4 = bands[ch * 8 + 4];
            AASynthAccum b5 = bands[ch * 8 + 5];
            AASynthAccum b6 = bands[ch * 8 + 6];
            AASynthAccum b7 = bands[ch * 8 + 7];
            const AASynthCoeff *f0 = &s->fir[0 * taps];
            const AASynthCoeff *f1 = &s->fir[1 * taps];
            const AASynthCoeff *f2 = &s->fir[2 * taps];
            const AASynthCoeff *f3 = &s->fir[3 * taps];
            const AASynthCoeff *f4 = &s->fir[4 * taps];
            const AASynthCoeff *f5 = &s->fir[5 * taps];
            const AASynthCoeff *f6 = &s->fir[6 * taps];
            const AASynthCoeff *f7 = &s->fir[7 * taps];
#if APTX_AA_OPT_SYNTH
            unsigned idx = base & 511;
#endif
            for (lag = 0; lag < taps; lag++) {
                AASynthAccum acc = AA_SYNTH_MUL(f0[lag], b0) + AA_SYNTH_MUL(f1[lag], b1) +
                                   AA_SYNTH_MUL(f2[lag], b2) + AA_SYNTH_MUL(f3[lag], b3) +
                                   AA_SYNTH_MUL(f4[lag], b4) + AA_SYNTH_MUL(f5[lag], b5) +
                                   AA_SYNTH_MUL(f6[lag], b6) + AA_SYNTH_MUL(f7[lag], b7);
#if APTX_AA_OPT_SYNTH
                s->overlap[ch][idx] += acc;
                idx = (idx - 1) & 511;
#else
                s->overlap[ch][(base - lag) & 511] += acc;
#endif
            }
        } else {
            for (lag = 0; lag < taps; lag++) {
                AASynthAccum acc = 0;
                for (unsigned k = 0; k < bands_count; k++)
                    acc += AA_SYNTH_MUL(s->fir[k * taps + lag], bands[ch * bands_count + k]);
                s->overlap[ch][(base - lag) & 511] += acc;
            }
        }
        for (j = 0; j < bands_count; j++) {
            unsigned at = (s->pos + j) & 511;
            pcm[2 * j + ch] = pcm_round(s->overlap[ch][at]);
            s->overlap[ch][at] = 0;
        }
    }
    s->pos = (s->pos + bands_count) & 511;
}
#undef AA_SYNTH_MUL
