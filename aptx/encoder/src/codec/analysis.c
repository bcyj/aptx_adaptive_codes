#include "encoder_internal.h"

/* Implements qmf_run. */
static void qmf_run(AAQmf *q, int32_t x, int32_t y, int32_t *sum, int32_t *diff) {
    unsigned k, n = q->n;
    int64_t a = 0, b = 0;
    aa_history_shift(q->a, n, 1);
    aa_history_shift(q->b, n, 1);
    q->a[0] = x;
    q->b[0] = y;
    for (k = 0; k < n; k++) {
        a += (int64_t)q->coef[k] * q->a[k];
        b += (int64_t)q->coef[k] * q->b[n - 1 - k];
    }
    a = aa_r23(a);
    b = aa_r23(b);
    *sum = aa_sat24(a + b);
    *diff = aa_sat24(b - a);
}

/* Implements dct_mul. */
static int32_t dct_mul(int32_t v, int32_t k) {
    uint64_t product = (uint64_t)aa_mul64((int64_t)v * 2, k);
    return aa_r64(aa_i64(product));
}

/* Implements analysis_block. */
static void analysis_block(AAAnalysis *a, const int32_t *s, int32_t *out, unsigned bands) {
    unsigned i, m, k;
    if (bands == 4) {
        int32_t s0, d0, s1, d1, b[4];
        qmf_run(&a->outer, s[0], s[1], &s0, &d0);
        qmf_run(&a->outer, s[2], s[3], &s1, &d1);
        qmf_run(&a->low, s0, s1, &b[0], &b[1]);
        qmf_run(&a->high, d0, d1, &b[3], &b[2]);
        for (k = 0; k < 4; k++)
            out[k] = aa_i32((uint32_t)b[k] << 8);
    } else {
        int32_t u[16], o[8], y[8];
        int32_t s04, s26, s15, s37, aa, b, c, d, ad, bc, s0, s1, g, h, e, f, ef, ii, l, j, t;
        aa_history_shift(a->hist, a->taps, 8);
        for (m = 0; m < 8; m++)
            a->hist[m] = s[7 - m];
        for (i = 0; i < 16; i++) {
            uint64_t acc = 0;
            const int32_t *p_proto = &a->proto[i];
            const int32_t *p_hist = &a->hist[i];
            unsigned steps = a->taps >> 4;
            for (m = 0; m < steps; m += 4) {
                acc -= (uint64_t)((int64_t)p_proto[0] * p_hist[0]);
                acc -= (uint64_t)((int64_t)p_proto[16] * p_hist[16]);
                acc -= (uint64_t)((int64_t)p_proto[32] * p_hist[32]);
                acc -= (uint64_t)((int64_t)p_proto[48] * p_hist[48]);
                p_proto += 64;
                p_hist += 64;
            }
            u[i] = aa_r64(aa_i64(acc * 64));
        }
        for (i = 0; i < 4; i++) {
            const AAFrontend *q = &FRONTEND[i];
            int32_t pos = aa_add(u[q->add[0]], u[q->add[1]]);
            int32_t neg = aa_sub(u[q->sub[0]], u[q->sub[1]]);
            o[q->diff_out] = aa_r64(aa_double_sat(aa_mul64(aa_sub(pos, neg), q->diff_const)));
            o[q->sum_out] = aa_r64(aa_double_sat(aa_mul64(aa_add(pos, neg), q->sum_const)));
        }
        s04 = aa_add(o[0], o[4]);
        s26 = aa_add(o[2], o[6]);
        s15 = aa_add(o[1], o[5]);
        s37 = aa_add(o[3], o[7]);
        aa = dct_mul(aa_sub(o[0], o[4]), DCT[0]);
        b = dct_mul(aa_sub(o[1], o[5]), DCT[1]);
        c = dct_mul(aa_sub(o[3], o[7]), DCT[2]);
        d = dct_mul(aa_sub(o[2], o[6]), DCT[3]);
        ad = aa_add(aa, d);
        bc = aa_add(b, c);
        s0 = aa_add(s04, s26);
        s1 = aa_add(s15, s37);
        y[0] = aa_add(s0, s1);
        y[1] = aa_add(ad, bc);
        g = dct_mul(aa_sub(s04, s26), DCT[4]);
        h = dct_mul(aa_sub(s15, s37), DCT[5]);
        y[2] = aa_add(g, h);
        y[4] = dct_mul(aa_sub(s0, s1), DCT[6]);
        e = dct_mul(aa_sub(aa, d), DCT[4]);
        f = dct_mul(aa_sub(b, c), DCT[5]);
        ef = aa_add(e, f);
        y[3] = aa_add(ef, aa_sub(ef, y[1]));
        ii = dct_mul(aa_sub(ad, bc), DCT[6]);
        y[5] = aa_add(ii, aa_sub(ii, y[3]));
        l = dct_mul(aa_sub(g, h), DCT[6]);
        y[6] = aa_add(l, aa_sub(l, y[2]));
        j = dct_mul(aa_sub(e, f), DCT[6]);
        t = aa_add(j, aa_sub(j, ef));
        y[7] = aa_add(t, aa_sub(t, y[5]));
        y[0] = aa_r64((int64_t)y[0] * INT64_C(2147483648));
        for (k = 1; k < 8; k++)
            y[k] = aa_sub(y[k], y[k - 1]);
        memcpy(out, y, sizeof(y));
    }
}

/* Implements aa_analyse. */
void aa_analyse(AAEngine *e, const int32_t *pcm, int32_t *out) {
    unsigned ch, k;
    int32_t buf[8];
    for (ch = 0; ch < 2; ch++) {
        for (k = 0; k < e->p.bands; k++)
            buf[k] = pcm[2 * k + ch];
        analysis_block(&e->ch[ch].analysis, buf, out + ch * e->p.bands, e->p.bands);
    }
}
