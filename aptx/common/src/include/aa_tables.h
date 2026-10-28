#ifndef AA_TABLES_H
#define AA_TABLES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t code;
    uint8_t len;
} AACode;
typedef struct {
    size_t add[2], sub[2], diff_out;
    uint32_t diff_const;
    size_t sum_out;
    uint32_t sum_const;
} AAFrontend;
typedef struct {
    const int32_t *threshold, *dither;
    const int16_t *incr;
} AAQuantTable;
typedef struct {
    const AACode *table;
    uint32_t pair_shift, raw_lsbs;
    int32_t msb_mask;
} AACodeword;
extern const int32_t OUTER_16[16];
extern const int32_t INNER_16[16];
extern const int32_t OUTER_12[12];
extern const int32_t INNER_12[12];
extern const int32_t PROTO_256[256];
extern const int32_t PROTO_128[128];
extern const int32_t LOG_TABLE[32];
extern const size_t TAPS_8BAND_48K[8];
extern const size_t TAPS_8BAND_96K[8];
extern const int8_t OFFSET_4BAND_DUAL_MONO[32];
extern const int8_t OFFSET_8BAND_DUAL_MONO[80];
extern const int8_t OFFSET_8BAND_DUAL_MONO_TONE[80];
extern const uint8_t MAX_BITS_4BAND_MAX[8];
extern const uint8_t MAX_BITS_4BAND_370[8];
extern const uint8_t MAX_BITS_4BAND_320[8];
extern const uint8_t MAX_BITS_8BAND_MAX[16];
extern const uint8_t MAX_BITS_8BAND_520[16];
extern const uint8_t MAX_BITS_8BAND_420[16];
extern const uint8_t MAX_BITS_8BAND_370[16];
extern const uint8_t MAX_BITS_8BAND_320[16];
extern const uint8_t MAX_BITS_8BAND_250[16];
extern const uint8_t MAX_BITS_8BAND_96K_MAX[16];
extern const uint8_t MAX_BITS_8BAND_96K_650[16];
extern const uint8_t MAX_BITS_8BAND_96K_600[16];
extern const uint8_t MAX_BITS_8BAND_96K_500[16];
extern const uint8_t MAX_BITS_8BAND_96K_420[16];
extern const int32_t DCT[7];
extern const AAFrontend FRONTEND[4];
extern const AAQuantTable QUANT[8];
extern const size_t TAPS_4BAND[4];
extern const AACode ABS_A[138];
extern const AACode ABS_B[138];
extern const bool PAIR_TABLE_8BAND[8];
extern const bool PAIR_TABLE_4BAND[8];
extern const AACode DIFF[9][16];
extern const size_t DIFF_INDEX[16];
extern const int32_t DIFF_RANGE[16][2];
extern const AACode T1[16];
extern const AACode T2[16];
extern const AACode T3[64];
extern const AACode T4[256];
extern const AACodeword CODEWORD[17];
#endif
