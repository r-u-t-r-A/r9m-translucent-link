/*
 * R9M transparent serial link
 *
 * Turns two FrSky R9M (STM32F103 + SX1276) modules into a bidirectional,
 * reliable, transparent serial bridge. All settings are compile time
 * (link_config.h / link_defines.txt).
 *
 * Timing
 * ------
 * Time is split into frames of linkcfg::FRAME_US. Every frame:
 *   1. master hops to the next channel and sends one packet
 *   2. slave receives it and immediately answers on the same channel
 *   3. slave hops to the next channel and listens
 * The master packet carries its hop index, so the slave locks to the hop
 * sequence from any single packet. When the slave loses the master it keeps
 * hopping blind for LINK_SLAVE_COAST_MS, then parks on the sync channel
 * (visited by the master once per pass through the channel list).
 *
 * Data integrity (ARQ, flow control, reboot handling) is in link_arq.h,
 * the optional MAVLink mode (LINK_MAVLINK) in link_mavlink.h.
 *
 * All radio (SPI) access happens from loop(); the DIO0 interrupt only sets a
 * flag, so there is no concurrent SPI access to reason about. The serial port
 * (UART or USB CDC) is interrupt driven by the Arduino core.
 */

#include "targets.h"
#include "SX127xDriver.h"
#include "FHSS.h"
#include "DAC.h"
#include <Wire.h>

#include "link_config.h"
#include "link_arq.h"
#include "link_mavlink.h"

using namespace linkcfg;

SX127xDriver Radio;

#if defined(LINK_ROLE_MASTER)
static constexpr bool IS_MASTER = true;
#else
static constexpr bool IS_MASTER = false;
#endif

static LinkArq<LINK_PKT_LEN, LINK_TX_FIFO_SIZE, LINK_RX_FIFO_SIZE> link;
static_assert(decltype(link)::PAYLOAD_MAX == LINK_PAYLOAD_MAX, "header size mismatch");

#if defined(LINK_USB_SERIAL)
#define LinkPort SerialUSB
static_assert(LINK_LED_RED_PIN != PA11 && LINK_LED_RED_PIN != PA12 &&
              LINK_LED_GREEN_PIN != PA11 && LINK_LED_GREEN_PIN != PA12,
              "PA11/PA12 are the USB data lines, move the LEDs or set them to UNDEF_PIN");
#else
static HardwareSerial LinkPort(LINK_UART_RX_PIN, LINK_UART_TX_PIN);
#endif

static const uint8_t UID[6] = {MY_UID};

static uint32_t lastValidRxMs;
static bool linkUp;

// ---------------------------------------------------------------------------
// Radio events (set by driver callbacks, consumed in loop)
// ---------------------------------------------------------------------------
static uint8_t txPkt[LINK_PKT_LEN];
static uint8_t rxPkt[LINK_PKT_LEN];
static bool evRx;
static bool evTxDone;
static uint32_t rxTimeUs;
static bool rxFeiPositive;
static int16_t rxRssiDbm;
static int8_t rxSnrRaw;

// Link quality, reported in RADIO_STATUS (LINK_MAVLINK) and sent to the peer
// in every packet header
static uint16_t rxErrors;   // packets that failed the CRC
static uint8_t noiseLevel;  // estimated noise floor, SiK RSSI scale

#if defined(Regulatory_Domain_EU_433) || defined(Regulatory_Domain_AU_433) || \
    defined(Regulatory_Domain_US_433) || defined(Regulatory_Domain_US_433_WIDE)
static constexpr int16_t RSSI_OFFSET_DBM = -164; // SX1276 LF port
#else
static constexpr int16_t RSSI_OFFSET_DBM = -157; // SX1276 HF port
#endif

static bool onRxDone(SX12xxDriverCommon::rx_status status)
{
    if (status != SX12xxDriverCommon::SX12XX_RX_OK)
        return false;
    rxTimeUs = micros();
    memcpy(rxPkt, Radio.RXdataBuffer, LINK_PKT_LEN);
    rxFeiPositive = Radio.GetFrequencyErrorbool();
    // Same correction as SX127xDriver::GetLastPacketStats(), but in 16 bits:
    // the driver's int8_t result wraps below -128 dBm.
    rxSnrRaw = Radio.GetLastPacketSNRRaw(SX12XX_Radio_1);
    rxRssiDbm = RSSI_OFFSET_DBM + Radio.UnsignedGetLastPacketRSSI(SX12XX_Radio_1);
    if (rxSnrRaw < 0)
        rxRssiDbm += rxSnrRaw / RADIO_SNR_SCALE;
    evRx = true;
    return true;
}

static void onTxDone()
{
    evTxDone = true;
}

// DIO0 (RxDone|TxDone) only latches a flag; the SPI work happens in loop().
// The level is checked as well in case an edge is ever missed.
static volatile bool dioPending;

static void dioEdgeIsr()
{
    dioPending = true;
}

static inline void pollRadio()
{
    if (dioPending || digitalRead(GPIO_PIN_DIO0))
    {
        dioPending = false;
        SX127xHal::dioISR_1();
    }
}

static void acceptPacket()
{
    lastValidRxMs = millis();
    link.txStatus = mavRssiFromDbm(rxRssiDbm); // tells the peer how well we hear it
    noiseLevel = mavRssiFromDbm(rxRssiDbm - rxSnrRaw / RADIO_SNR_SCALE);
    link.process(rxPkt);
}

static void rejectPacket()
{
    rxErrors++;
}

// ---------------------------------------------------------------------------
// Master: owns the frame timing and hop sequence
// ---------------------------------------------------------------------------
#if defined(LINK_ROLE_MASTER)
static uint32_t nextFrameUs;
static bool awaitingReply;

static void roleInit()
{
    nextFrameUs = micros() + FRAME_US;
}

static void roleLoop()
{
    pollRadio();

    if (evTxDone)
    {
        evTxDone = false;
        Radio.RXnb(); // listen for the reply on the same channel
        awaitingReply = true;
    }

    if (evRx)
    {
        evRx = false;
        if (awaitingReply)
        {
            if (link.valid(rxPkt) && rxPkt[2] == FHSSgetCurrIndex())
            {
                awaitingReply = false;
                Radio.SetTxIdleMode();
                acceptPacket();
            }
            else
            {
                rejectPacket();
            }
        }
    }

    const uint32_t now = micros();
    if ((int32_t)(now - nextFrameUs) >= 0)
    {
        nextFrameUs += FRAME_US;
        if ((int32_t)(now - nextFrameUs) >= 0) // fell behind, don't try to catch up
            nextFrameUs = now + FRAME_US;

        awaitingReply = false;
        evRx = false;
        Radio.SetFrequencyReg(FHSSgetNextFreq(), SX12XX_Radio_All);
        link.build(txPkt, FHSSgetCurrIndex());
        Radio.TXnb(txPkt, SX12XX_Radio_1);
    }
}

static bool isSearching() { return false; }
#endif

// ---------------------------------------------------------------------------
// Slave: follows the master's hop index
// ---------------------------------------------------------------------------
#if defined(LINK_ROLE_SLAVE)
// After a master packet received at t, the next one ends at t+FRAME. If it
// doesn't arrive, hop in the middle of the gap between where it would have
// ended and where the one after it starts.
static constexpr uint32_t MISS_HOP_US = FRAME_US + (FRAME_US - TOA_US) / 2;
static constexpr uint16_t COAST_FRAMES = (uint32_t)LINK_SLAVE_COAST_MS * 1000 / FRAME_US;
static constexpr uint32_t TX_TIMEOUT_US = TOA_US + 2000;
static constexpr int32_t AFC_STEP = 4; // FREQ_STEP (61Hz) units per packet

static bool synced;
static bool transmitting;
static uint32_t txStartUs;
static uint32_t hopDeadlineUs;
static uint16_t missCount;

static void enterSearch()
{
    synced = false;
    FreqCorrection = 0;
    Radio.SetPPMoffsetReg(0);
    FHSSsetCurrIndex(0); // sequence entry 0 is the sync channel
    Radio.SetFrequencyReg(FHSSgetInitialFreq(), SX12XX_Radio_All, true);
}

static void roleInit()
{
    enterSearch();
}

static void afcUpdate(bool feiPositive)
{
    // Same direction as ELRS rx_main HandleFreqCorr(), with a larger step
    // because packets arrive at a lower rate. Applied from the next hop on.
    FreqCorrection += feiPositive ? -AFC_STEP : AFC_STEP;
    FreqCorrection = constrain(FreqCorrection, FreqCorrectionMin, FreqCorrectionMax);
    Radio.SetPPMoffsetReg(FreqCorrection);
}

static void hopAndListen()
{
    Radio.SetFrequencyReg(FHSSgetNextFreq(), SX12XX_Radio_All, true);
}

static void roleLoop()
{
    pollRadio();

    if (evRx)
    {
        evRx = false;
        if (!transmitting && !link.valid(rxPkt))
        {
            rejectPacket();
        }
        else if (!transmitting)
        {
            const uint8_t hopIndex = rxPkt[2];
            FHSSsetCurrIndex(hopIndex);
            acceptPacket();
            afcUpdate(rxFeiPositive);
            link.build(txPkt, hopIndex);
            Radio.TXnb(txPkt, SX12XX_Radio_1);
            transmitting = true;
            txStartUs = micros();
            synced = true;
            missCount = 0;
            hopDeadlineUs = rxTimeUs + MISS_HOP_US;
        }
    }

    if (transmitting && (evTxDone || (micros() - txStartUs) > TX_TIMEOUT_US))
    {
        evTxDone = false;
        transmitting = false;
        hopAndListen();
    }

    if (synced && !transmitting && (int32_t)(micros() - hopDeadlineUs) >= 0)
    {
        if (++missCount > COAST_FRAMES)
        {
            enterSearch();
        }
        else
        {
            hopAndListen();
            hopDeadlineUs += FRAME_US;
        }
    }
}

static bool isSearching() { return !synced; }
#endif

// ---------------------------------------------------------------------------
// Serial port, LEDs, power
// ---------------------------------------------------------------------------
#if LINK_MAVLINK
static MavInput<decltype(link.uartToAir)> mavIn;
static MavTracker mavOut;      // frame boundaries in what goes out of the port
static bool mavOutResync;      // output was discarded: skip to the next frame start
static uint32_t lastOutMs;     // last time output made progress
static uint32_t lastStatusMs;
static uint8_t statusSeq;

static MavRadioStatus radioStatus()
{
    // txbuf is the free share of the first half of the input buffer, so
    // ArduPilot throttles while there is still room for the bytes already on
    // their way, and the queue (= latency) stays short.
    const uint16_t window = LINK_TX_FIFO_SIZE / 2;
    const uint16_t used = link.uartToAir.size();
    MavRadioStatus st = {};
    st.txbuf = used >= window ? 0 : (uint8_t)(100 - (uint32_t)used * 100 / window);
    st.rxerrors = rxErrors;
    if (linkUp)
    {
        st.rssi = link.txStatus;
        st.remrssi = link.peerStatus;
        st.noise = noiseLevel;
    }
    return st;
}
#endif

static void serviceSerial(uint32_t nowMs)
{
    while (LinkPort.available())
    {
        if (!linkUp && LINK_DROP_INPUT_WHEN_DISCONNECTED)
        {
            LinkPort.read();
            continue;
        }
#if LINK_MAVLINK && !defined(LINK_USB_SERIAL)
        // A UART has no flow control: always read it, and when the buffer is
        // full drop whole MAVLink frames rather than leaving it to the UART
        // to lose bytes in the middle of frames.
        mavIn.put(LinkPort.read(), link.uartToAir, nowMs);
#else
        // Leave it in the core's buffer. For USB this back-pressures the host.
        if (!link.uartToAir.space())
            break;
        link.uartToAir.push(LinkPort.read());
#endif
    }
#if LINK_MAVLINK && !defined(LINK_USB_SERIAL)
    mavIn.poll(link.uartToAir, nowMs, 50);
#endif

#if defined(LINK_USB_SERIAL)
    // No program has the port open (DTR low): nobody is listening, so drop
    // the data like an unplugged cable instead of stalling the far end.
    if (!SerialUSB.dtr())
    {
        link.airToUart.clear();
#if LINK_MAVLINK
        mavOutResync = true;
#endif
        return;
    }
#endif

    int room = LinkPort.availableForWrite();

#if LINK_MAVLINK
    if (mavOutResync)
    {
        // Resume output at a frame start, so the reader never sees the tail
        // of a frame whose beginning was discarded.
        while (link.airToUart.size() && link.airToUart.peek(0) != MavTracker::STX_V1 &&
               link.airToUart.peek(0) != MavTracker::STX_V2)
            link.airToUart.drop(1);
        if (!link.airToUart.size())
            return;
        mavOut.reset();
        mavOutResync = false;
    }

    // The rest of a frame that never arrives (link lost, far end rebooted)
    // must not hold up RADIO_STATUS for good.
    if (!mavOut.betweenFrames() && !link.airToUart.size() && (nowMs - lastOutMs) > 200)
        mavOut.reset();

    // RADIO_STATUS goes between frames. When one is due, output stops at the
    // end of the current frame until there is room for it.
    const bool statusDue = (nowMs - lastStatusMs) >= LINK_MAVLINK_STATUS_MS;
    if (statusDue && mavOut.betweenFrames())
    {
        if (room < MAV_RADIO_STATUS_MAX_LEN)
            return;
        uint8_t frame[MAV_RADIO_STATUS_MAX_LEN];
        const uint8_t len = mavPackRadioStatus(frame, statusSeq++, radioStatus());
        LinkPort.write(frame, len);
        lastStatusMs = nowMs;
        room -= len;
    }
#endif

    // Only remove what the port actually took. USB CDC write() refuses data
    // while the host is not reading (and HardwareSerial is limited to its free
    // space), so a slow reader back-pressures the link instead of losing bytes.
    uint16_t n = room > 0 ? min((uint16_t)room, link.airToUart.size()) : 0;
    uint8_t chunk[64];
    n = min(n, (uint16_t)sizeof(chunk));
#if LINK_MAVLINK
    if (statusDue && !mavOut.betweenFrames())
        n = min(n, mavOut.untilBoundary());
#endif
    if (n)
    {
        for (uint16_t i = 0; i < n; i++)
            chunk[i] = link.airToUart.peek(i);
        const uint16_t written = LinkPort.write(chunk, n);
#if LINK_MAVLINK
        for (uint16_t i = 0; i < written; i++)
            mavOut.feed(chunk[i]);
        if (written)
            lastOutMs = nowMs;
#endif
        link.airToUart.drop(written);
    }
}

static void updateLinkState(uint32_t nowMs)
{
    bool up = lastValidRxMs != 0 && (nowMs - lastValidRxMs) < LINK_TIMEOUT_MS;
    if (up != linkUp)
    {
        linkUp = up;
        if (!linkUp && LINK_DROP_INPUT_WHEN_DISCONNECTED)
        {
            link.uartToAir.clear();
#if LINK_MAVLINK
            mavIn.reset();
#endif
        }
    }
}

static void setLed(int pin, bool on)
{
    if (pin != UNDEF_PIN)
        digitalWrite(pin, on ? HIGH : LOW);
}

static void updateLeds(uint32_t nowMs)
{
    // Connected: solid green. Disconnected: red blinks, fast on a slave that
    // is parked searching for the master.
    bool red = !linkUp && ((nowMs >> (isSearching() ? 6 : 8)) & 1);
    setLed(LINK_LED_GREEN_PIN, linkUp);
    setLed(LINK_LED_RED_PIN, red);
}

static void setupPower()
{
    static const int16_t mvTable[] = POWER_OUTPUT_VALUES;
    static const int16_t mvTable868[] = POWER_OUTPUT_VALUES_868;
    constexpr int idx = (int)LINK_POWER - (int)MinPower;
    static_assert(idx >= 0 && (int)LINK_POWER <= (int)MaxPower, "LINK_POWER out of range for this module");

    Wire.setSCL(GPIO_PIN_SCL);
    Wire.setSDA(GPIO_PIN_SDA);
    Wire.begin();
    TxDAC.init();
    TxDAC.setPower(isDomain868() ? mvTable868[idx] : mvTable[idx]);

    pinMode(GPIO_PIN_FAN_EN, OUTPUT);
    digitalWrite(GPIO_PIN_FAN_EN, LINK_POWER >= PWR_250mW ? HIGH : LOW);
}

static uint8_t randomSession()
{
    // Wideband RSSI noise plus the free running microsecond counter
    uint32_t r = micros() ^ HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    Radio.RXnb();
    for (uint8_t i = 0; i < 32; i++)
    {
        delayMicroseconds(97);
        r = (r * 31) ^ (uint8_t)Radio.GetCurrRSSI(SX12XX_Radio_1) ^ micros();
    }
    Radio.SetTxIdleMode();
    return (uint8_t)(r ^ (r >> 8) ^ (r >> 16) ^ (r >> 24));
}

static void fatal()
{
    // Radio not detected: alternate red/green
    for (;;)
    {
        bool phase = (millis() >> 7) & 1;
        setLed(LINK_LED_RED_PIN, phase);
        setLed(LINK_LED_GREEN_PIN, !phase);
    }
}

static uint8_t regSF()
{
    switch (LINK_SF)
    {
    case 6: return SX127x_SF_6;
    case 7: return SX127x_SF_7;
    case 8: return SX127x_SF_8;
    case 9: return SX127x_SF_9;
    case 10: return SX127x_SF_10;
    case 11: return SX127x_SF_11;
    default: return SX127x_SF_12;
    }
}

static uint8_t regCR()
{
    switch (LINK_CR)
    {
    case 5: return SX127x_CR_4_5;
    case 6: return SX127x_CR_4_6;
    case 7: return SX127x_CR_4_7;
    default: return SX127x_CR_4_8;
    }
}

static uint8_t regBW(uint32_t khz)
{
    return khz == 125 ? SX127x_BW_125_00_KHZ : (khz == 250 ? SX127x_BW_250_00_KHZ : SX127x_BW_500_00_KHZ);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
    if (LINK_LED_RED_PIN != UNDEF_PIN)
        pinMode(LINK_LED_RED_PIN, OUTPUT);
    if (LINK_LED_GREEN_PIN != UNDEF_PIN)
        pinMode(LINK_LED_GREEN_PIN, OUTPUT);
    setLed(LINK_LED_RED_PIN, true);
    setLed(LINK_LED_GREEN_PIN, false);

    LinkPort.begin(LINK_UART_BAUD); // baud is ignored for USB
#if defined(LINK_USB_SERIAL) && LINK_USB_IGNORE_DTR
    SerialUSB.dtr(false); // treat the port as open once the host configures it
#endif

    // XOR so the hop pattern differs from an ELRS link using the same phrase
    const uint32_t uidSeed = ((uint32_t)UID[2] << 24) | ((uint32_t)UID[3] << 16) | ((uint32_t)UID[4] << 8) | UID[5];
    FHSSrandomiseFHSSsequence(uidSeed ^ 0x52394D4CUL); // "R9ML"
    const uint16_t crcSeed = (((uint16_t)UID[0] << 8) | UID[1]) ^ (((uint16_t)UID[5] << 8) | UID[4]) ^ LINK_PROTO_VERSION;

    setupPower();

    Radio.currFreq = FHSSgetInitialFreq(); // selects LF/HF mode in Begin()
    if (!Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq()))
        fatal();
    // Replace the driver's ISR so the radio is only ever touched from loop()
    detachInterrupt(GPIO_PIN_DIO0);
    attachInterrupt(digitalPinToInterrupt(GPIO_PIN_DIO0), dioEdgeIsr, RISING);
    Radio.RXdoneCallback = &onRxDone;
    Radio.TXdoneCallback = &onTxDone;

    // The driver skips register writes for values equal to its cached
    // defaults (SF7, BW125, CR4/5), which would leave the chip's reset value
    // (explicit header) in place. Set something else first to force them.
    Radio.SetSpreadingFactor((SX127x_SpreadingFactor)(LINK_SF == 7 ? SX127x_SF_8 : SX127x_SF_7));
    Radio.SetBandwidthCodingRate((SX127x_Bandwidth)regBW(LINK_BW_KHZ == 500 ? 250 : 500), (SX127x_CodingRate)regCR());
    Radio.Config(regBW(LINK_BW_KHZ), regSF(), regCR(), FHSSgetInitialFreq(), LINK_PREAMBLE_LEN, false, LINK_PKT_LEN);

    link.begin(IS_MASTER, crcSeed, randomSession());

    setLed(LINK_LED_RED_PIN, false);
    roleInit();
}

void loop()
{
    roleLoop();
    const uint32_t nowMs = millis();
    serviceSerial(nowMs);
    updateLinkState(nowMs);
    updateLeds(nowMs);
}
