#pragma once
/*
 * Minimal replacement for the ExpressLRS targets.h: only what the reused
 * SX127x / FHSS / RFAMP / DAC code needs, for the FrSky R9M only.
 */
#include <Arduino.h>

#define UNDEF_PIN (-1)

#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define WORD_PADDED(size) (((size) + 3) & ~3)
#define ICACHE_RAM_ATTR
#ifndef DMA_ATTR
#define DMA_ATTR
#endif

#ifndef RADIO_SX127X
#define RADIO_SX127X 1
#endif

#include "target_r9m.h"

// Pins of dual-radio / other hardware referenced by the shared driver code
#ifndef GPIO_PIN_NSS_2
#define GPIO_PIN_NSS_2 UNDEF_PIN
#endif
#ifndef GPIO_PIN_RST_2
#define GPIO_PIN_RST_2 UNDEF_PIN
#endif
#ifndef GPIO_PIN_DIO0_2
#define GPIO_PIN_DIO0_2 UNDEF_PIN
#endif
#ifndef GPIO_PIN_PA_ENABLE
#define GPIO_PIN_PA_ENABLE UNDEF_PIN
#endif
#ifndef GPIO_PIN_TX_ENABLE_2
#define GPIO_PIN_TX_ENABLE_2 UNDEF_PIN
#endif
#ifndef GPIO_PIN_RX_ENABLE_2
#define GPIO_PIN_RX_ENABLE_2 UNDEF_PIN
#endif

#if !(defined(Regulatory_Domain_AU_915) || defined(Regulatory_Domain_FCC_915) || \
      defined(Regulatory_Domain_EU_868) || defined(Regulatory_Domain_IN_866) || \
      defined(Regulatory_Domain_AU_433) || defined(Regulatory_Domain_EU_433) || \
      defined(Regulatory_Domain_US_433) || defined(Regulatory_Domain_US_433_WIDE) || \
      defined(UNIT_TEST))
#error "Define a Regulatory_Domain_xxx in link_defines.txt"
#endif
