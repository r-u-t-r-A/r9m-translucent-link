#pragma once
/*
 * Frequency hopping, reduced from the ExpressLRS FHSS library to the SX127x
 * sub-GHz domains with the domain chosen at compile time. The hop sequence is
 * generated with the same algorithm as ELRS (sync channel at every
 * freq_count-th slot, pseudorandom permutation of the others in each block).
 */

#include "targets.h"
#include "random.h"
#include "SX127xDriver.h" // FREQ_STEP

#define FREQ_HZ_TO_REG_VAL(freq) ((uint32_t)((double)(freq) / (double)FREQ_STEP))
#define FREQ_SPREAD_SCALE 256
#define FHSS_SEQUENCE_LEN 256

// Max automatic frequency correction, register units
#define FreqCorrectionMax ((int32_t)(100000 / FREQ_STEP))
#define FreqCorrectionMin (-FreqCorrectionMax)

typedef struct {
    const char  *domain;
    uint32_t    freq_start;
    uint32_t    freq_stop;
    uint32_t    freq_count;
    uint32_t    freq_center;
} fhss_config_t;

extern const fhss_config_t *const FHSSconfig;
extern volatile uint8_t FHSSptr;
extern int32_t FreqCorrection;
extern uint8_t FHSSsequence[];
extern uint_fast8_t sync_channel;
extern uint32_t freq_spread;
extern uint16_t FHSSsequenceCount;

void FHSSrandomiseFHSSsequence(uint32_t seed);
bool isDomain868();

static inline uint32_t FHSSgetMinimumFreq() { return FHSSconfig->freq_start; }
static inline uint32_t FHSSgetMaximumFreq() { return FHSSconfig->freq_stop; }
static inline uint32_t FHSSgetChannelCount() { return FHSSconfig->freq_count; }
static inline uint16_t FHSSgetSequenceCount() { return FHSSsequenceCount; }
static inline uint8_t FHSSgetCurrIndex() { return FHSSptr; }

static inline void FHSSsetCurrIndex(const uint8_t value)
{
    FHSSptr = value % FHSSgetSequenceCount();
}

// Frequency of the sync channel (sequence entry 0)
static inline uint32_t FHSSgetInitialFreq()
{
    return FHSSconfig->freq_start + (sync_channel * freq_spread / FREQ_SPREAD_SCALE) - FreqCorrection;
}

// Advance to the next hop and return its frequency
static inline uint32_t FHSSgetNextFreq()
{
    FHSSptr = (FHSSptr + 1) % FHSSgetSequenceCount();
    return FHSSconfig->freq_start + (freq_spread * FHSSsequence[FHSSptr] / FREQ_SPREAD_SCALE) - FreqCorrection;
}
