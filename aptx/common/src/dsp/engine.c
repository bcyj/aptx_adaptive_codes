#include "aa_internal.h"

/* Implements qmf_init. */
static void qmf_init(AAQmf *q, const int32_t *coef, unsigned n) {
    memset(q, 0, sizeof(*q));
    q->coef = coef;
    q->n = n;
}

/* Implements aa_engine_init. */
void aa_engine_init(AAEngine *e, AAParams p) {
    unsigned ch, k;
    const size_t *taps =
        p.bands == 4 ? TAPS_4BAND : (p.rate > 48000 ? TAPS_8BAND_96K : TAPS_8BAND_48K);
    memset(e, 0, sizeof(*e));
    e->p = p;
    for (ch = 0; ch < 2; ch++) {
        AAChannel *c = &e->ch[ch];
        qmf_init(&c->analysis.outer, p.variant == 1 ? OUTER_12 : OUTER_16,
                 p.variant == 1 ? 12 : 16);
        qmf_init(&c->analysis.low, p.variant == 1 ? INNER_12 : INNER_16, p.variant == 1 ? 12 : 16);
        qmf_init(&c->analysis.high, p.variant == 1 ? INNER_12 : INNER_16, p.variant == 1 ? 12 : 16);
        c->analysis.proto = p.variant == 4 ? PROTO_128 : PROTO_256;
        c->analysis.taps = p.variant == 4 ? 128 : 256;
        for (k = 0; k < p.bands; k++) {
            c->band[k].step = 8;
            c->band[k].ld = 244;
            c->pred[k].taps = (unsigned)((p.variant == 1 && ch > 0) ? 0 : taps[k]);
            c->pred[k].s0 = 1;
            c->pred[k].s1 = 1;
        }
    }
}
