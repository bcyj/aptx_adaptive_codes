#ifndef AA_MODE_TABLES_H
#define AA_MODE_TABLES_H
#include "encoder_internal.h"
typedef struct {
    uint32_t kbps, ms, samples;
} AARowRate;
typedef struct {
    size_t bytes;
    uint8_t cfg;
} AARowPacket;
extern const AARowRate AA_HQ[9][7];
extern const AARowPacket AA_HQ_PACKET[9][2];
extern const AARowRate AA_LL[9][7];
extern const AARowPacket AA_LL_PACKET[9][2];

#endif
