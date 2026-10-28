#ifndef AA_INTERNAL_H
#define AA_INTERNAL_H
#include "aa_runtime.h"
#include "aa_tables.h"
#include <aptx/common/types.h>
#include <limits.h>
#if APTX_AA_NO_CRT
#define AUDIO_MATH_NO_CRT 1
#endif
#include <audio_math/integer.h>

_Static_assert(sizeof(int32_t) == 4 && sizeof(int64_t) == 8, "fixed width integers required");
_Static_assert(sizeof(int) == 4, "32-bit int required");
_Static_assert((-1 >> 1) == -1, "arithmetic signed shifts required");

#ifndef APTX_AA_OPT_PREDICT
#if defined(_M_IX86) || defined(__i386__)
#define APTX_AA_OPT_PREDICT 1
#else
#define APTX_AA_OPT_PREDICT 0
#endif
#endif
#ifndef APTX_AA_OPT_SYNTH
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__i386__) || defined(_M_IX86)
#define APTX_AA_OPT_SYNTH 0
#else
#define APTX_AA_OPT_SYNTH 1
#endif
#endif
#define AA_WIDTH 16
#define AA_BLOCKS 64
#define AA_MIN(a, b) ((a) < (b) ? (a) : (b))
#define AA_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Implements aa_history_shift. */
static inline void aa_history_shift(int32_t *data, size_t count, size_t shift) {
    size_t i;
    if ((shift & 1) == 0 && (count & 1) == 0) {
        size_t c64 = count >> 1;
        size_t s64 = shift >> 1;
        for (i = c64; i > s64; --i) {
            uint64_t value;
            memcpy(&value, data + 2 * (i - 1 - s64), sizeof(value));
            memcpy(data + 2 * (i - 1), &value, sizeof(value));
        }
        return;
    }
    for (i = count; i > shift; --i)
        data[i - 1] = data[i - 1 - shift];
}

/* Implements aa_i32. */
static inline int32_t aa_i32(uint32_t u) {
    return audio_math_int32_from_bits(u);
}

/* Implements aa_i64. */
static inline int64_t aa_i64(uint64_t u) {
    return audio_math_int64_from_bits(u);
}

/* Implements aa_mul64. */
static AUDIO_MATH_FORCE_INLINE int64_t aa_mul64(int64_t a, int64_t b) {
    return aa_i64(audio_math_multiply_wrap_u64((uint64_t)a, (uint64_t)b));
}

/* Implements aa_wadd. */
static inline int32_t aa_wadd(int32_t a, int32_t b) {
    return audio_math_add_wrap_i32(a, b);
}

/* Implements aa_wsub. */
static inline int32_t aa_wsub(int32_t a, int32_t b) {
    return audio_math_subtract_wrap_i32(a, b);
}

/* Implements aa_wmul. */
static inline int32_t aa_wmul(int32_t a, int32_t b) {
    return audio_math_multiply_wrap_i32(a, b);
}

/* Implements aa_clamp. */
static inline int32_t aa_clamp(int32_t a, int32_t lo, int32_t hi) {
    return audio_math_clamp_i32(a, lo, hi);
}

/* Implements aa_clampu. */
static inline uint32_t aa_clampu(uint32_t a, uint32_t lo, uint32_t hi) {
    return audio_math_clamp_u32(a, lo, hi);
}

/* Implements aa_sat24. */
static inline int32_t aa_sat24(int64_t a) {
    return audio_math_saturate_i64_to_i24(a);
}

/* Implements aa_sat32. */
static inline int32_t aa_sat32(int64_t a) {
    return audio_math_saturate_i64_to_i32(a);
}

/* Implements aa_add. */
static inline int32_t aa_add(int32_t a, int32_t b) {
    return audio_math_add_saturate_i32(a, b);
}

/* Implements aa_sub. */
static inline int32_t aa_sub(int32_t a, int32_t b) {
    return audio_math_subtract_saturate_i32(a, b);
}

/* Implements aa_cr. */
static inline int64_t aa_cr(int64_t v, unsigned n) {
    return audio_math_round_shift_even_i64(v, n);
}

/* Implements aa_cr32. */
static inline int32_t aa_cr32(int32_t v, unsigned n) {
    return audio_math_round_shift_even_i32(v, n);
}

/* Implements aa_r23. */
static inline int32_t aa_r23(int64_t v) {
    return aa_sat24(aa_i32((uint32_t)aa_cr(v, 23)));
}

/* Implements aa_r64. */
static inline int32_t aa_r64(int64_t v) {
    return aa_sat32((v >> 32) + ((uint32_t)v >= UINT32_C(0x80000000)));
}

/* Implements aa_sext24. */
static inline int32_t aa_sext24(int32_t v) {
    return audio_math_sign_extend_i32((uint32_t)v, 24);
}

/* Implements aa_double_sat. */
static inline int64_t aa_double_sat(int64_t v) {
    return audio_math_double_saturate_i64(v);
}

typedef struct {
    unsigned variant;
    uint32_t rate;
    unsigned bands, width;
} AAParams;
typedef struct {
    const int32_t *coef;
    unsigned n;
    int32_t a[16], b[16];
} AAQmf;
typedef struct {
    AAQmf outer, low, high;
    const int32_t *proto;
    unsigned taps;
    int32_t hist[256];
} AAAnalysis;
typedef struct {
    int32_t s0, s1, a1, a2, b[24], hist[24], y1, z, pr;
    unsigned taps;
} AAPredictor;
typedef struct {
    int32_t step, ld, dith, r;
} AABand;
typedef struct {
    AAAnalysis analysis;
    AAPredictor pred[8];
    AABand band[8];
    int32_t seed;
} AAChannel;
typedef struct {
    AAParams p;
    AAChannel ch[2];
} AAEngine;

/* Implements aa_params. */
int aa_params(unsigned variant, uint32_t rate, AAParams *p);

/* Implements aa_config_params. */
int aa_config_params(const aptx_aa_config *c, AAParams *p);

/* Implements aa_engine_init. */
void aa_engine_init(AAEngine *e, AAParams p);

/* Implements aa_predictor_run. */
void aa_predictor_run(AAPredictor *p, int32_t x);

/* Implements aa_dither. */
void aa_dither(AAChannel *c, int32_t q0, int32_t q1, unsigned bands);

#endif
