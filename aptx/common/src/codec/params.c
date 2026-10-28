#include "aa_internal.h"

/* Implements aa_params. */
int aa_params(unsigned variant, uint32_t rate, AAParams *p) {
    if ((variant != 0 && variant != 1 && variant != 3 && variant != 4) ||
        (rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000) ||
        (rate > 48000 && variant != 3))
        return APTX_AA_INVALID;
    p->variant = variant;
    p->rate = rate;
    p->bands = variant < 2 ? 4 : 8;
    p->width = 2 * p->bands;
    return 0;
}

/* Implements aa_config_params. */
int aa_config_params(const aptx_aa_config *c, AAParams *p) {
    unsigned variant;
    if (!c || c->channels != 2 || (c->profile != APTX_AA_HQ && c->profile != APTX_AA_LL) ||
        (c->decoder != APTX_AA_R1 && c->decoder != APTX_AA_R2))
        return APTX_AA_INVALID;
    if (c->sample_rate > 48000) {
        if (c->profile == APTX_AA_LL)
            return APTX_AA_INVALID;
        variant = 3;
    } else if (c->decoder == APTX_AA_R1)
        variant = 0;
    else
        variant = c->profile == APTX_AA_HQ ? 3 : 4;
    return aa_params(variant, c->sample_rate, p);
}
