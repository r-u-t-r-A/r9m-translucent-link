#pragma once
/*
 * Hardware independent part of the R9M serial link: packet format and the
 * per-direction alternating-bit ARQ. Kept free of Arduino/radio dependencies
 * so it can be unit tested natively (test/test_r9m_link).
 *
 * Packet (fixed PKT_LEN bytes):
 *   [0] flags  b7 from-slave, b6 SEQ, b5 ACK, b4 ACK valid, b3..0 version
 *   [1] session id of sender (random per boot)
 *   [2] hop index (master: current, slave: echo)
 *   [3] payload length
 *   [4] session id of the peer whose data the ACK bit refers to
 *   [5] status byte, opaque to the ARQ (link_main: sender's RSSI)
 *   [6..]  payload, zero padded
 *   [-2..] CRC16-CCITT, seeded from the binding phrase
 *
 * Sender: a payload is repeated in every packet until the peer ACKs its SEQ.
 * An ACK only counts if it names our current session: the master transmits
 * every frame whether or not it hears the slave, so after a slave reboot it
 * keeps sending ACKs meant for the previous slave instance until it hears the
 * new one, and those must not acknowledge the new instance's data.
 * Receiver: accepts a payload if SEQ is the expected one (or it is the first
 * one from a new peer session) AND it fits in the output buffer, otherwise it
 * drops it and the ACK it sends tells the sender to repeat / move on.
 */

#include <stdint.h>
#include <string.h>

#define LINK_PROTO_VERSION 2

// Ring buffer, N must be a power of 2 <= 32768
template <uint16_t N>
class LinkRing
{
    static_assert(N && !(N & (N - 1)) && N <= 32768, "N must be a power of 2 <= 32768");
    uint8_t buf[N];
    uint16_t head = 0; // write position
    uint16_t tail = 0; // read position
public:
    uint16_t size() const { return (uint16_t)(head - tail); }
    uint16_t space() const { return N - size(); }
    void push(uint8_t b) { buf[head++ & (N - 1)] = b; }
    uint8_t pop() { return buf[tail++ & (N - 1)]; }
    uint8_t peek(uint16_t i) const { return buf[(uint16_t)(tail + i) & (N - 1)]; } // i < size()
    void drop(uint16_t n) { tail += n; }                                          // n <= size()
    void clear() { tail = head; }
};

static inline uint16_t linkCrc16(const uint8_t *data, uint8_t len, uint16_t crc)
{
    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

template <uint8_t PKT_LEN, uint16_t TX_FIFO, uint16_t RX_FIFO>
class LinkArq
{
public:
    static const uint8_t HDR_LEN = 6;
    static const uint8_t CRC_LEN = 2;
    static const uint8_t PAYLOAD_MAX = PKT_LEN - HDR_LEN - CRC_LEN;
    static_assert(PKT_LEN > HDR_LEN + CRC_LEN, "packet too small");

    static const uint8_t F_FROM_SLAVE = 0x80;
    static const uint8_t F_SEQ = 0x40;
    static const uint8_t F_ACK = 0x20;
    static const uint8_t F_ACKV = 0x10;
    static const uint8_t F_VER_MASK = 0x0F;

    LinkRing<TX_FIFO> uartToAir; // bytes waiting to go over the air
    LinkRing<RX_FIFO> airToUart; // bytes received, waiting for the UART
    uint8_t txStatus = 0;        // sent in every packet
    uint8_t peerStatus = 0;      // from the last valid packet of the peer

    // Also used to simulate a reboot in tests: all link state is reset
    void begin(bool isMaster, uint16_t crcSeed, uint8_t session)
    {
        master = isMaster;
        seed = crcSeed;
        mySession = session;
        txPending = false;
        txSeq = 0;
        txLen = 0;
        rxExpect = 0;
        rxFresh = true;
        ackValid = false;
        peerKnown = false;
        peerSession = 0;
        peerStatus = 0;
        uartToAir.clear();
        airToUart.clear();
    }

    void build(uint8_t *pkt, uint8_t hopIndex)
    {
        if (!txPending && uartToAir.size())
        {
            txLen = uartToAir.size() < PAYLOAD_MAX ? (uint8_t)uartToAir.size() : PAYLOAD_MAX;
            for (uint8_t i = 0; i < txLen; i++)
                txData[i] = uartToAir.pop();
            txPending = true;
        }

        uint8_t len = txPending ? txLen : 0;
        pkt[0] = (master ? 0 : F_FROM_SLAVE)
               | (txSeq ? F_SEQ : 0)
               | ((rxExpect ^ 1) ? F_ACK : 0)
               | (ackValid ? F_ACKV : 0)
               | LINK_PROTO_VERSION;
        pkt[1] = mySession;
        pkt[2] = hopIndex;
        pkt[3] = len;
        pkt[4] = peerSession;
        pkt[5] = txStatus;
        memcpy(&pkt[HDR_LEN], txData, len);
        memset(&pkt[HDR_LEN + len], 0, PAYLOAD_MAX - len);
        uint16_t crc = linkCrc16(pkt, PKT_LEN - CRC_LEN, seed);
        pkt[PKT_LEN - 2] = crc >> 8;
        pkt[PKT_LEN - 1] = crc & 0xFF;
    }

    // CRC, protocol version, and sent by the other role
    bool valid(const uint8_t *pkt) const
    {
        uint16_t crc = linkCrc16(pkt, PKT_LEN - CRC_LEN, seed);
        if (pkt[PKT_LEN - 2] != (crc >> 8) || pkt[PKT_LEN - 1] != (crc & 0xFF))
            return false;
        if ((pkt[0] & F_VER_MASK) != LINK_PROTO_VERSION)
            return false;
        bool fromSlave = (pkt[0] & F_FROM_SLAVE) != 0;
        if (fromSlave != master)
            return false;
        return pkt[3] <= PAYLOAD_MAX;
    }

    // pkt must have passed valid()
    void process(const uint8_t *pkt)
    {
        const uint8_t flags = pkt[0];
        const uint8_t session = pkt[1];
        const uint8_t len = pkt[3];
        peerStatus = pkt[5];

        if (!peerKnown || session != peerSession)
        {
            // First contact or the peer rebooted: its receive state is fresh,
            // and the ACK bit in this packet refers to nothing we sent.
            peerKnown = true;
            peerSession = session;
            rxFresh = true;
            ackValid = false;
        }
        else if (txPending && (flags & F_ACKV) && pkt[4] == mySession && (((flags & F_ACK) ? 1 : 0) == txSeq))
        {
            txPending = false;
            txSeq ^= 1;
        }

        if (len)
        {
            uint8_t seq = (flags & F_SEQ) ? 1 : 0;
            // seq != rxExpect is a repeat of something already delivered
            // (our ACK was lost); the ACK we send next covers it.
            if ((rxFresh || seq == rxExpect) && airToUart.space() >= len)
            {
                for (uint8_t i = 0; i < len; i++)
                    airToUart.push(pkt[HDR_LEN + i]);
                rxExpect = seq ^ 1;
                rxFresh = false;
                ackValid = true;
            }
        }
    }

private:
    bool master = false;
    uint16_t seed = 0;
    uint8_t mySession = 0;
    // outgoing direction
    bool txPending = false;  // txData holds a payload not yet acknowledged
    uint8_t txSeq = 0;
    uint8_t txLen = 0;
    uint8_t txData[PAYLOAD_MAX];
    // incoming direction
    uint8_t rxExpect = 0;    // seq expected next from the peer
    bool rxFresh = true;     // accept the next payload whatever its seq
    bool ackValid = false;   // accepted something in this peer session
    bool peerKnown = false;
    uint8_t peerSession = 0;
};
