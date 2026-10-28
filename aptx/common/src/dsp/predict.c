#include "aa_internal.h"

/* Implements aa_predictor_run. */
void aa_predictor_run(AAPredictor *p, int32_t x) {
    int64_t t = (int64_t)p->z + x, acc;
    int32_t cur = 1, p1 = 0, p2 = 0, w, k, a1, a2, y, pole, e, rnd;
    unsigned i, n = p->taps;
    if (t < 0) {
        cur = -1;
        p1 = -p->s0;
        p2 = -p->s1;
    } else if (t > 0) {
        p1 = p->s0;
        p2 = p->s1;
    }
    p->s1 = p->s0;
    p->s0 = cur;
    w = aa_cr32(aa_wadd(aa_wmul(255, p->a1), aa_wmul(12582912, p1)), 8);
    k = aa_clamp(aa_cr32(aa_wmul(-p1, p->a1), 1), -1048576, 1048576);
    a2 = aa_cr32(aa_wadd(aa_wmul(16, aa_wadd(k, aa_wmul(p2, 524288))) & ~255, aa_wmul(254, p->a2)),
                 8);
    a2 = aa_clamp(a2, -3145728, 3145728);
    a1 = aa_clamp(w, a2 - 3932160, 3932160 - a2);
    p->a1 = a1;
    p->a2 = a2;
    y = aa_sat24((int64_t)p->pr + x);
    pole = aa_i32((uint32_t)(((int64_t)p->y1 * a2 + (int64_t)a1 * y) >> 21));
    p->y1 = y;
    e = x == 0 ? 0 : (x < 0 ? -8388608 : 8388608);
#if APTX_AA_OPT_PREDICT
    {
        int32_t mask0 = p->hist[0] >> 31;
        int32_t term0 = (e ^ mask0) - mask0;
        int32_t b0 = aa_sext24(aa_wadd(p->b[0], aa_cr32(aa_wsub(term0, p->b[0]), 8)));
        p->b[0] = b0;
        acc = (int64_t)b0 * x;
        for (i = 1; i < n; i++) {
            int32_t mask = p->hist[i] >> 31;
            int32_t term = (e ^ mask) - mask;
            int32_t bi = aa_sext24(aa_wadd(p->b[i], aa_cr32(aa_wsub(term, p->b[i]), 8)));
            p->b[i] = bi;
            acc += (int64_t)bi * p->hist[i - 1];
        }
    }
    p->z = aa_sat24(aa_i32((uint32_t)(acc >> 22)));
    rnd = aa_sat24(((int64_t)pole + 1) >> 1);
    p->pr = aa_sat24((int64_t)p->z + rnd);
    if (n == 8) {
        p->hist[7] = p->hist[6];
        p->hist[6] = p->hist[5];
        p->hist[5] = p->hist[4];
        p->hist[4] = p->hist[3];
        p->hist[3] = p->hist[2];
        p->hist[2] = p->hist[1];
        p->hist[1] = p->hist[0];
    } else if (n == 4) {
        p->hist[3] = p->hist[2];
        p->hist[2] = p->hist[1];
        p->hist[1] = p->hist[0];
    } else if (n == 2) {
        p->hist[1] = p->hist[0];
    } else {
        for (i = n; i > 1; --i)
            p->hist[i - 1] = p->hist[i - 2];
    }
#else
    for (i = 0; i < n; i++) {
        int32_t term = p->hist[i] < 0 ? -e : e;
        p->b[i] = aa_sext24(aa_wadd(p->b[i], aa_cr32(aa_wsub(term, p->b[i]), 8)));
    }
    acc = (int64_t)p->b[0] * x;
    for (i = 1; i < n; i++)
        acc += (int64_t)p->b[i] * p->hist[i - 1];
    p->z = aa_sat24(aa_i32((uint32_t)(acc >> 22)));
    rnd = aa_sat24(((int64_t)pole + 1) >> 1);
    p->pr = aa_sat24((int64_t)p->z + rnd);
    aa_history_shift(p->hist, n, 1);
#endif
    p->hist[0] = x;
}

/* Implements aa_dither. */
void aa_dither(AAChannel *c, int32_t q0, int32_t q1, unsigned bands) {
    int64_t p;
    uint64_t pl, v;
    unsigned k, shift = bands == 8 ? 2 : 5;
    c->seed = aa_i32((uint32_t)((((4 * q0) & 12) | (q1 & 3)) * 256) + (uint32_t)c->seed * 16);
    p = (int64_t)(c->seed >> 8) * INT64_C(0x009e3776);
    pl = (uint32_t)p;
    v = ((pl >> 22) & 3) | (((((uint64_t)p >> 24) + pl) & UINT64_C(0x3fffff)) << 2);
    for (k = 0; k < bands; k++)
        c->band[k].dith = aa_sext24((int32_t)(((uint32_t)v << (shift * k)) & 0xffffff));
}
