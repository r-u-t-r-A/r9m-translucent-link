#include "FHSS.h"

static const fhss_config_t domainConfig =
#if defined(Regulatory_Domain_AU_915)
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20, 921000000};
#elif defined(Regulatory_Domain_FCC_915)
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000};
#elif defined(Regulatory_Domain_EU_868)
    {"EU868",  FREQ_HZ_TO_REG_VAL(865275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000};
#elif defined(Regulatory_Domain_IN_866)
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000};
#elif defined(Regulatory_Domain_AU_433)
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000};
#elif defined(Regulatory_Domain_EU_433)
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000};
#elif defined(Regulatory_Domain_US_433)
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000};
#elif defined(Regulatory_Domain_US_433_WIDE)
    {"US433W", FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000};
#endif

const fhss_config_t *const FHSSconfig = &domainConfig;

uint8_t FHSSsequence[FHSS_SEQUENCE_LEN];
uint16_t FHSSsequenceCount;
volatile uint8_t FHSSptr;
uint_fast8_t sync_channel;
int32_t FreqCorrection;
uint32_t freq_spread;

bool isDomain868()
{
#if defined(Regulatory_Domain_EU_868)
    return true;
#else
    return false;
#endif
}

/*
 * Fill the sequence with the sync channel every freq_count entries, then
 * within each block swap every other entry with a random one of the block.
 */
void FHSSrandomiseFHSSsequence(const uint32_t seed)
{
    const uint32_t freqCount = FHSSconfig->freq_count;
    sync_channel = (freqCount / 2) + 1;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (freqCount - 1);
    FHSSsequenceCount = (FHSS_SEQUENCE_LEN / freqCount) * freqCount;
    FHSSptr = 0;
    rngSeed(seed);

    for (uint16_t i = 0; i < FHSSsequenceCount; i++)
    {
        if (i % freqCount == 0)
            FHSSsequence[i] = sync_channel;
        else if (i % freqCount == sync_channel)
            FHSSsequence[i] = 0;
        else
            FHSSsequence[i] = i % freqCount;
    }

    for (uint16_t i = 0; i < FHSSsequenceCount; i++)
    {
        if (i % freqCount != 0)
        {
            uint8_t offset = (i / freqCount) * freqCount;
            uint8_t rand = rngN(freqCount - 1) + 1;
            uint8_t temp = FHSSsequence[i];
            FHSSsequence[i] = FHSSsequence[offset + rand];
            FHSSsequence[offset + rand] = temp;
        }
    }
}
