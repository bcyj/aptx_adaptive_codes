#ifndef AA_SYNTHESIS_H
#define AA_SYNTHESIS_H
#include "aa_internal.h"
#include <aptx/decoder/decoder.h>
#if APTX_AA_DECODER_FIXED_POINT

#define AA_SYNTH_FRAC_BITS 29
typedef int32_t AASynthCoeff;
typedef int64_t AASynthAccum;
#else
typedef double AASynthCoeff;
typedef double AASynthAccum;
#endif
extern const AASynthCoeff AA_SYNTH_0[4][94];
extern const AASynthCoeff AA_SYNTH_1[4][70];
extern const AASynthCoeff AA_SYNTH_3[8][256];
extern const AASynthCoeff AA_SYNTH_4[8][128];
typedef struct {
    const AASynthCoeff *fir;
    unsigned bands, taps, pos;
    AASynthAccum overlap[2][512];
} AASynthesis;

/* Implements aa_synthesis_init. */
void aa_synthesis_init(AASynthesis *, AAParams);

/* Implements aa_synthesis_run. */
void aa_synthesis_run(AASynthesis *, const int32_t *bands, int32_t *pcm);
#endif
