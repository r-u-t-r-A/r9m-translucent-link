#pragma once
/*
 * R9M transparent serial link - compile time configuration
 *
 * Everything here can be overridden with -D lines in link_defines.txt.
 * Both modules MUST be built with identical radio settings; only the role
 * (and optionally the serial port type) differs.
 *
 * Required in link_defines.txt:
 *   -DMY_BINDING_PHRASE="something unique"   (identifies your pair of modules)
 *   -DRegulatory_Domain_xxx                 (frequency band / hop table)
 */

#include <stdint.h>

// ---------------------------------------------------------------------------
// Role: exactly one of LINK_ROLE_MASTER / LINK_ROLE_SLAVE is set by the target
// PlatformIO env. The master owns the timing and frequency hopping, the slave follows it.
// Apart from that the link is fully symmetric; it does not matter which end
// sits on the aircraft.
// ---------------------------------------------------------------------------
#if defined(LINK_ROLE_MASTER) == defined(LINK_ROLE_SLAVE)
#error "Define exactly one of LINK_ROLE_MASTER or LINK_ROLE_SLAVE"
#endif

#if !defined(MY_UID)
#error "Set -DMY_BINDING_PHRASE=\"...\" in link_defines.txt, both modules must use the same phrase"
#endif

// ---------------------------------------------------------------------------
// Serial port
// ---------------------------------------------------------------------------
// LINK_USB_SERIAL (set by the *_usb envs): use the STM32 USB device as a
// virtual COM port (CDC ACM) instead of a UART. Needs a hardware mod: PA11 =
// D-, PA12 = D+, 1.5k pull-up D+ to 3.3V, and the LEDs removed from those pins.
// No baud rate to match, and the host is flow controlled by USB itself.
// The port counts as closed when the host has read nothing for
// LINK_USB_CLOSED_MS while data is waiting; that data is then discarded, like
// a cable with nothing on the other end. Shorter stalls hold the link back
// instead of losing bytes. DTR is ignored, because Mission Planner doesn't
// set it. LINK_USB_REQUIRE_DTR 1: the port is only open while DTR is set.
#ifdef LINK_USB_IGNORE_DTR
#error "LINK_USB_IGNORE_DTR is gone: DTR is now ignored by default (see LINK_USB_REQUIRE_DTR)"
#endif
#ifndef LINK_USB_REQUIRE_DTR
#define LINK_USB_REQUIRE_DTR    0
#endif
#ifndef LINK_USB_CLOSED_MS
#define LINK_USB_CLOSED_MS      250
#endif
//
// UART mode:
#ifndef LINK_UART_BAUD
#define LINK_UART_BAUD          115200
#endif
// Default is USART1 on PA10 (RX) / PA9 (TX), the pads used by the R9M WiFi
// backpack mod. Any pin pair that maps to one USART on the STM32F103 works,
// e.g. PB11/PB10 for USART3 (note that these go through the S.Port buffer).
#ifndef LINK_UART_RX_PIN
#define LINK_UART_RX_PIN        PA10
#endif
#ifndef LINK_UART_TX_PIN
#define LINK_UART_TX_PIN        PA9
#endif
// When the radio link is down, discard what arrives on the port instead of
// queueing it. Prevents a burst of stale MAVLink/MSP on reconnect.
#ifndef LINK_DROP_INPUT_WHEN_DISCONNECTED
#define LINK_DROP_INPUT_WHEN_DISCONNECTED 1
#endif
// MAVLink mode, for ArduPilot/PX4 telemetry. The link stays transparent, but
//  - a UART input that overflows drops whole MAVLink frames, never parts;
//  - RADIO_STATUS messages (like a SiK radio: rssi, remrssi, txbuf) are
//    inserted between frames every LINK_MAVLINK_STATUS_MS. ArduPilot uses
//    txbuf to slow its streams and parameter download to what the radio
//    carries, and ground stations show the RSSI values.
// Leave it off for MSP or other non-MAVLink data.
#ifndef LINK_MAVLINK
#define LINK_MAVLINK            0
#endif
#ifndef LINK_MAVLINK_STATUS_MS
#define LINK_MAVLINK_STATUS_MS  50
#endif
// Buffer between serial input and the air (power of 2).
#ifndef LINK_TX_FIFO_SIZE
#define LINK_TX_FIFO_SIZE       2048
#endif
// Buffer between the air and serial output (power of 2). Packets are only
// ACKed when they fit here, so a slow port back-pressures the remote end
// instead of losing data.
#ifndef LINK_RX_FIFO_SIZE
#define LINK_RX_FIFO_SIZE       1024
#endif

// ---------------------------------------------------------------------------
// LEDs (UNDEF_PIN to disable). In USB mode the stock LED pins are the USB
// data lines, so the LEDs are off unless moved to other pins.
// ---------------------------------------------------------------------------
#if defined(LINK_USB_SERIAL)
  #ifndef LINK_LED_RED_PIN
  #define LINK_LED_RED_PIN      UNDEF_PIN
  #endif
  #ifndef LINK_LED_GREEN_PIN
  #define LINK_LED_GREEN_PIN    UNDEF_PIN
  #endif
#else
  #ifndef LINK_LED_RED_PIN
  #define LINK_LED_RED_PIN      GPIO_PIN_LED_RED
  #endif
  #ifndef LINK_LED_GREEN_PIN
  #define LINK_LED_GREEN_PIN    GPIO_PIN_LED_GREEN
  #endif
#endif

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------
// Presets (both directions get the listed payload rate simultaneously):
//   1 FAST   SF6 BW500 CR4/5 64B pkt, 35ms frame  ~1600 B/s/dir (ELRS 200Hz modulation)
//   2 NORMAL SF7 BW500 CR4/5 64B pkt, 59ms frame    ~940 B/s/dir (~ELRS 100Hz)
//   3 LONG   SF8 BW500 CR4/7 48B pkt, 110ms frame  ~360 B/s/dir (~ELRS 50Hz)
//   4 XLONG  SF9 BW500 CR4/7 32B pkt, 145ms frame  ~165 B/s/dir (~ELRS 25Hz)
// Set LINK_PRESET 0 and define LINK_SF/LINK_BW_KHZ/LINK_CR/LINK_PKT_LEN for
// a custom mode.
#ifndef LINK_PRESET
#define LINK_PRESET             1
#endif

#if LINK_PRESET == 1
  #define LINK_SF 6
  #define LINK_BW_KHZ 500
  #define LINK_CR 5
  #define LINK_PKT_LEN 64
#elif LINK_PRESET == 2
  #define LINK_SF 7
  #define LINK_BW_KHZ 500
  #define LINK_CR 5
  #define LINK_PKT_LEN 64
#elif LINK_PRESET == 3
  #define LINK_SF 8
  #define LINK_BW_KHZ 500
  #define LINK_CR 7
  #define LINK_PKT_LEN 48
#elif LINK_PRESET == 4
  #define LINK_SF 9
  #define LINK_BW_KHZ 500
  #define LINK_CR 7
  #define LINK_PKT_LEN 32
#elif LINK_PRESET != 0
  #error "Unknown LINK_PRESET"
#endif

#ifndef LINK_PREAMBLE_LEN
#define LINK_PREAMBLE_LEN       8
#endif

// Output power, PWR_10mW .. PWR_1000mW (names in include/target_r9m.h).
// Above PWR_250mW the R9M needs the fan mod, the fan output is switched on
// automatically.
#ifndef LINK_POWER
#define LINK_POWER              PWR_100mW
#endif

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
// Time allowed from the end of a received packet until the reply is on air.
#ifndef LINK_TURNAROUND_US
#define LINK_TURNAROUND_US      1000
#endif
// Extra idle time per frame used for hopping.
#ifndef LINK_GUARD_US
#define LINK_GUARD_US           1000
#endif
// Link is reported down (LED, input dropping) after this long without a valid
// packet from the other side.
#ifndef LINK_TIMEOUT_MS
#define LINK_TIMEOUT_MS         1000
#endif
// The slave keeps hopping blind for this long after losing the master, then
// parks on the sync channel to re-acquire.
#ifndef LINK_SLAVE_COAST_MS
#define LINK_SLAVE_COAST_MS     1500
#endif

// ---------------------------------------------------------------------------
// Derived values, do not edit
// ---------------------------------------------------------------------------
#define LINK_HDR_LEN            6
#define LINK_CRC_LEN            2
#define LINK_PAYLOAD_MAX        (LINK_PKT_LEN - LINK_HDR_LEN - LINK_CRC_LEN)

#if LINK_SF < 6 || LINK_SF > 12
#error "LINK_SF must be 6..12"
#endif
#if LINK_CR < 5 || LINK_CR > 8
#error "LINK_CR is the coding rate denominator, 5..8 (4/5..4/8)"
#endif
#if LINK_BW_KHZ != 125 && LINK_BW_KHZ != 250 && LINK_BW_KHZ != 500
#error "LINK_BW_KHZ must be 125, 250 or 500"
#endif
#if LINK_PKT_LEN < 8 || LINK_PKT_LEN > 128
#error "LINK_PKT_LEN must be 8..128"
#endif
#if (LINK_TX_FIFO_SIZE & (LINK_TX_FIFO_SIZE - 1)) || (LINK_RX_FIFO_SIZE & (LINK_RX_FIFO_SIZE - 1))
#error "FIFO sizes must be powers of 2"
#endif

namespace linkcfg {

constexpr uint32_t BW_HZ = LINK_BW_KHZ * 1000UL;
constexpr uint32_t SYMBOL_US = (1UL << LINK_SF) * 1000000UL / BW_HZ;

// SX1276 datasheet time-on-air, implicit header, no HW CRC, LDRO off
constexpr uint32_t ceil_div(int32_t a, int32_t b) { return a <= 0 ? 0 : (uint32_t)((a + b - 1) / b); }
constexpr uint32_t PAYLOAD_SYMBOLS = 8 + ceil_div(8 * LINK_PKT_LEN - 4 * LINK_SF + 28 - 20, 4 * LINK_SF) * LINK_CR;
constexpr uint32_t TOA_US = ((LINK_PREAMBLE_LEN * 4 + 17) * SYMBOL_US) / 4 + PAYLOAD_SYMBOLS * SYMBOL_US;

// One frame = master packet + slave packet + turnarounds + guard
constexpr uint32_t FRAME_US = 2 * TOA_US + 2 * LINK_TURNAROUND_US + LINK_GUARD_US;

static_assert(SYMBOL_US <= 16000, "symbol time > 16ms needs LowDataRateOptimize, which the driver does not set");
static_assert(LINK_PKT_LEN <= RXBuffSize, "increase -DRXBuffSize in platformio.ini");

} // namespace linkcfg
