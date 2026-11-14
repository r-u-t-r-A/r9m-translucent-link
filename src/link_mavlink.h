#pragma once
/*
 * Optional MAVLink awareness (LINK_MAVLINK=1). Hardware independent so it can
 * be unit tested natively (test/test_mavlink).
 *
 * The link stays a byte pipe; MAVLink frames are only located by their
 * length fields (no CRC check), which is enough to:
 *   - drop whole frames instead of random bytes when the input overflows
 *     (MavInput), so everything that does get through is intact;
 *   - insert RADIO_STATUS messages between frames on the output side, the
 *     way SiK telemetry radios do. ArduPilot uses its txbuf field as flow
 *     control: it slows its telemetry streams below 50 % free and pauses the
 *     parameter download at 33 % or less.
 * Bytes outside MAVLink frames are passed through unchanged.
 */

#include <stdint.h>

// Tracks MAVLink frame boundaries in a byte stream, one byte at a time.
class MavTracker
{
    uint8_t state = 0; // 0 between frames, 1 got v1 STX, 2 got v2 STX, 3 got v2 LEN
    uint8_t len = 0;
    uint16_t left = 0; // bytes of the current frame still to come once the length is known

public:
    static const uint8_t STX_V1 = 0xFE;
    static const uint8_t STX_V2 = 0xFD;
    static const uint16_t MAX_FRAME = 10 + 255 + 2 + 13; // v2 header + payload + CRC + signature

    void reset()
    {
        state = 0;
        left = 0;
    }

    // Returns true if b was the last byte of a frame
    bool feed(uint8_t b)
    {
        if (left)
            return --left == 0;
        switch (state)
        {
        case 0:
            if (b == STX_V1)
                state = 1;
            else if (b == STX_V2)
                state = 2;
            return false;
        case 1: // v1: STX LEN SEQ SYS COMP MSGID payload CRC2 -> len + 8 bytes
            left = b + 6;
            state = 0;
            return false;
        case 2:
            len = b;
            state = 3;
            return false;
        default: // v2: STX LEN INCOMPAT COMPAT SEQ SYS COMP MSGID3 payload CRC2 [SIG13]
            left = len + 9 + ((b & 0x01) ? 13 : 0);
            state = 0;
            return false;
        }
    }

    bool betweenFrames() const { return state == 0 && left == 0; }

    // Bytes that can be passed on before the next frame boundary: 0 when
    // between frames, 1 while the length is still unknown.
    uint16_t untilBoundary() const { return state ? 1 : left; }
};

// Serial input without flow control: frames are queued whole or not at all.
template <class Ring>
class MavInput
{
    MavTracker tracker;
    uint8_t stage[MavTracker::MAX_FRAME];
    uint16_t staged = 0;
    uint32_t lastByteMs = 0;

    void commit(Ring &out)
    {
        if (out.space() >= staged)
        {
            for (uint16_t i = 0; i < staged; i++)
                out.push(stage[i]);
        }
        else
        {
            droppedFrames++;
        }
        staged = 0;
    }

public:
    uint32_t droppedFrames = 0;
    uint32_t droppedBytes = 0; // loose bytes (outside frames) that did not fit

    void reset()
    {
        tracker.reset();
        staged = 0;
    }

    void put(uint8_t b, Ring &out, uint32_t nowMs)
    {
        lastByteMs = nowMs;
        const bool wasBetween = tracker.betweenFrames();
        const bool ended = tracker.feed(b);
        if (wasBetween && tracker.betweenFrames())
        {
            if (out.space())
                out.push(b);
            else
                droppedBytes++;
            return;
        }
        stage[staged++] = b;
        if (ended)
            commit(out);
    }

    // A frame that stops arriving halfway is passed on as it is after
    // timeoutMs, so a stray STX byte in non-MAVLink data can't hold it up.
    void poll(Ring &out, uint32_t nowMs, uint32_t timeoutMs)
    {
        if (staged && (nowMs - lastByteMs) >= timeoutMs)
        {
            commit(out);
            tracker.reset();
        }
    }
};

// RADIO_STATUS (#109) as a MAVLink 2 frame, from sysid 51 / compid 68
// (MAV_COMP_ID_TELEMETRY_RADIO) like a SiK radio. buf must hold
// MAV_RADIO_STATUS_MAX_LEN bytes; returns the frame length.
#define MAV_RADIO_STATUS_MAX_LEN 21

struct MavRadioStatus
{
    uint16_t rxerrors;
    uint16_t fixed;
    uint8_t rssi;
    uint8_t remrssi;
    uint8_t txbuf;
    uint8_t noise;
    uint8_t remnoise;
};

static inline uint16_t mavCrcAccumulate(uint8_t b, uint16_t crc)
{
    uint8_t tmp = b ^ (uint8_t)(crc & 0xFF);
    tmp ^= (uint8_t)(tmp << 4);
    return (uint16_t)((crc >> 8) ^ ((uint16_t)tmp << 8) ^ ((uint16_t)tmp << 3) ^ (tmp >> 4));
}

static inline uint8_t mavPackRadioStatus(uint8_t *buf, uint8_t seq, const MavRadioStatus &s)
{
    static const uint8_t MSGID = 109;
    static const uint8_t CRC_EXTRA = 185;
    uint8_t *p = &buf[10];
    p[0] = s.rxerrors & 0xFF;
    p[1] = s.rxerrors >> 8;
    p[2] = s.fixed & 0xFF;
    p[3] = s.fixed >> 8;
    p[4] = s.rssi;
    p[5] = s.remrssi;
    p[6] = s.txbuf;
    p[7] = s.noise;
    p[8] = s.remnoise;
    uint8_t len = 9;
    while (len > 1 && p[len - 1] == 0) // MAVLink 2 drops trailing zero bytes
        len--;

    buf[0] = MavTracker::STX_V2;
    buf[1] = len;
    buf[2] = 0; // incompat flags
    buf[3] = 0; // compat flags
    buf[4] = seq;
    buf[5] = 51; // sysid, as SiK
    buf[6] = 68; // MAV_COMP_ID_TELEMETRY_RADIO
    buf[7] = MSGID;
    buf[8] = 0;
    buf[9] = 0;
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 1; i < 10 + len; i++)
        crc = mavCrcAccumulate(buf[i], crc);
    crc = mavCrcAccumulate(CRC_EXTRA, crc);
    buf[10 + len] = crc & 0xFF;
    buf[11 + len] = crc >> 8;
    return 12 + len;
}

// LoRa RSSI in dBm to the 0..254 scale SiK radios use (dBm = value / 1.9 - 127),
// which Mission Planner and ArduPilot logs understand.
static inline uint8_t mavRssiFromDbm(int16_t dbm)
{
    int32_t v = ((int32_t)dbm + 127) * 19 / 10;
    return v < 0 ? 0 : (v > 254 ? 254 : (uint8_t)v);
}
