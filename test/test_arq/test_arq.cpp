/*
 * Native tests for the R9M serial link ARQ (src/link_arq.h).
 *
 * Simulates the frame exchange of link_main.cpp: every frame the master sends
 * one packet, and the slave answers only if it received it. Packets can be
 * lost or corrupted, the UARTs can drain slowly, and either side can reboot.
 * The receivers must always deliver an exact, in-order copy of what was sent.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unity.h>

#include "../../src/link_arq.h"

typedef LinkArq<64, 2048, 1024> Arq;
static const uint16_t SEED = 0xBEEF;

// Deterministic byte stream: stream id + position -> byte
static uint8_t streamByte(uint32_t stream, uint32_t pos)
{
    uint32_t x = (pos + 1) * 2654435761u ^ (stream * 40503u);
    x ^= x >> 15;
    return (uint8_t)(x ^ (x >> 8));
}

struct Endpoint
{
    Arq arq;
    uint32_t txStream = 0;    // id of the stream this side is sending
    uint32_t txPos = 0;       // bytes of it pushed into the UART buffer
    std::vector<uint8_t> rx;  // bytes this side wrote to its UART
    unsigned drainPerFrame = 100000;

    void feed(unsigned maxBytes)
    {
        while (maxBytes-- && arq.uartToAir.space())
            arq.uartToAir.push(streamByte(txStream, txPos++));
    }
    void drain()
    {
        unsigned n = drainPerFrame;
        while (n-- && arq.airToUart.size())
            rx.push_back(arq.airToUart.pop());
    }
};

struct Sim
{
    Endpoint m, s;
    unsigned lossM2S = 0;    // percent
    unsigned lossS2M = 0;
    unsigned corrupt = 0;    // percent of packets with a flipped bit
    unsigned feedPerFrame = 100;
    uint8_t hop = 0;

    Sim()
    {
        srand(1234);
        m.arq.begin(true, SEED, 0x11);
        s.arq.begin(false, SEED, 0x22);
        m.txStream = 1;
        s.txStream = 2;
    }

    bool air(uint8_t *pkt, unsigned lossPct)
    {
        if ((unsigned)(rand() % 100) < lossPct)
            return false;
        if ((unsigned)(rand() % 100) < corrupt)
            pkt[rand() % 64] ^= (uint8_t)(1 << (rand() % 8));
        return true;
    }

    void frame()
    {
        uint8_t pkt[64];
        m.feed(feedPerFrame);
        s.feed(feedPerFrame);

        m.arq.build(pkt, ++hop);
        if (air(pkt, lossM2S) && s.arq.valid(pkt))
        {
            s.arq.process(pkt);
            s.arq.build(pkt, hop);
            if (air(pkt, lossS2M) && m.arq.valid(pkt))
                m.arq.process(pkt);
        }
        m.drain();
        s.drain();
    }

    void run(unsigned frames)
    {
        while (frames--)
            frame();
    }
};

// rx[from..] must equal stream bytes starting at streamPos
static void assertStream(const std::vector<uint8_t> &rx, size_t from, uint32_t stream, uint32_t streamPos)
{
    for (size_t i = from; i < rx.size(); i++)
    {
        if (rx[i] != streamByte(stream, streamPos + (uint32_t)(i - from)))
        {
            char msg[80];
            snprintf(msg, sizeof(msg), "stream %u mismatch at rx byte %u", (unsigned)stream, (unsigned)i);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// Find where in `stream` the bytes rx[from..] start, searching a window
static long findOffset(const std::vector<uint8_t> &rx, size_t from, uint32_t stream, uint32_t lo, uint32_t hi)
{
    for (uint32_t k = lo; k <= hi; k++)
    {
        bool ok = true;
        for (size_t i = from; i < rx.size() && ok; i++)
            ok = rx[i] == streamByte(stream, k + (uint32_t)(i - from));
        if (ok)
            return (long)k;
    }
    return -1;
}

void test_lossless_full_rate(void)
{
    Sim sim;
    sim.run(1000);
    assertStream(sim.s.rx, 0, 1, 0);
    assertStream(sim.m.rx, 0, 2, 0);
    // Each direction carries a full payload every frame once started
    TEST_ASSERT_GREATER_OR_EQUAL(998u * Arq::PAYLOAD_MAX, sim.s.rx.size());
    TEST_ASSERT_GREATER_OR_EQUAL(998u * Arq::PAYLOAD_MAX, sim.m.rx.size());
}

void test_heavy_loss_is_exact(void)
{
    Sim sim;
    sim.lossM2S = 30;
    sim.lossS2M = 40;
    sim.run(20000);
    assertStream(sim.s.rx, 0, 1, 0);
    assertStream(sim.m.rx, 0, 2, 0);
    TEST_ASSERT_GREATER_THAN(100000u, sim.s.rx.size());
    TEST_ASSERT_GREATER_THAN(100000u, sim.m.rx.size());
}

void test_corruption_is_rejected(void)
{
    Sim sim;
    sim.corrupt = 20;
    sim.lossM2S = 5;
    sim.lossS2M = 5;
    sim.run(20000);
    assertStream(sim.s.rx, 0, 1, 0);
    assertStream(sim.m.rx, 0, 2, 0);
    TEST_ASSERT_GREATER_THAN(100000u, sim.s.rx.size());
}

void test_slow_uart_backpressure(void)
{
    Sim sim;
    sim.s.drainPerFrame = 7; // far slower than the air rate
    sim.lossM2S = 10;
    sim.lossS2M = 10;
    sim.run(20000);
    assertStream(sim.s.rx, 0, 1, 0);
    assertStream(sim.m.rx, 0, 2, 0);
    // Throughput limited by the UART, but nothing lost
    TEST_ASSERT_GREATER_THAN(20000u * 7 - 2048, sim.s.rx.size());
}

void test_idle_link_and_sparse_data(void)
{
    Sim sim;
    sim.feedPerFrame = 0;
    sim.run(100);
    TEST_ASSERT_EQUAL(0, sim.s.rx.size());
    TEST_ASSERT_EQUAL(0, sim.m.rx.size());
    sim.feedPerFrame = 1;
    sim.lossM2S = 20;
    sim.lossS2M = 20;
    sim.run(5000);
    assertStream(sim.s.rx, 0, 1, 0);
    assertStream(sim.m.rx, 0, 2, 0);
    TEST_ASSERT_GREATER_THAN(4000u, sim.s.rx.size());
}

// One side reboots mid-stream, many times: it loses its own buffers, but
// afterwards both directions must again be exact, contiguous streams. The
// rounds matter: the window where a freshly booted side's stale ACK bit could
// be mistaken for a real one is narrow and only hit some of the time.
static void rebootTest(bool rebootMaster)
{
    Sim sim;
    sim.lossM2S = 30;
    sim.lossS2M = 30;

    Endpoint &rb = rebootMaster ? sim.m : sim.s;
    Endpoint &other = rebootMaster ? sim.s : sim.m;

    sim.run(500);
    assertStream(other.rx, 0, rb.txStream, 0);
    assertStream(rb.rx, 0, other.txStream, 0);
    uint32_t rbStreamPos = 0; // position in other's stream where rb.rx starts

    for (unsigned round = 0; round < 200; round++)
    {
        // Survivor-stream bytes that the old instance got (written or buffered)
        const uint32_t rbDelivered = rbStreamPos + rb.rx.size() + rb.arq.airToUart.size();
        const size_t otherRxBefore = other.rx.size() + other.arq.airToUart.size();

        rb.arq.begin(rebootMaster, SEED, (uint8_t)(0x40 + round)); // new session id
        rb.txStream += 10;
        rb.txPos = 0;
        rb.rx.clear();

        sim.run(20 + rand() % 200);

        // Survivor: what it had before, then the new stream from its start
        assertStream(other.rx, otherRxBefore, rb.txStream, 0);

        // Rebooted side: one contiguous piece of the survivor's stream,
        // starting around where the old instance stopped (the payload in
        // flight is resent)
        const uint32_t lo = rbDelivered > Arq::PAYLOAD_MAX ? rbDelivered - Arq::PAYLOAD_MAX : 0;
        long k = findOffset(rb.rx, 0, other.txStream, lo, rbDelivered + Arq::PAYLOAD_MAX);
        if (k < 0)
        {
            char msg[80];
            snprintf(msg, sizeof(msg), "round %u: rebooted side stream not contiguous", round);
            TEST_FAIL_MESSAGE(msg);
        }
        rbStreamPos = (uint32_t)k;
    }
    // and the link still moves data afterwards
    const size_t before = other.rx.size();
    sim.run(2000);
    TEST_ASSERT_GREATER_THAN(10000u, other.rx.size() - before);
}

void test_master_reboot(void) { rebootTest(true); }
void test_slave_reboot(void) { rebootTest(false); }

void test_wrong_seed_or_role_rejected(void)
{
    Arq a, b, c;
    a.begin(true, SEED, 1);
    b.begin(false, SEED ^ 1, 2); // different binding phrase
    c.begin(true, SEED, 3);      // another master
    uint8_t pkt[64];
    a.uartToAir.push(42);
    a.build(pkt, 0);
    TEST_ASSERT_FALSE(b.valid(pkt));
    TEST_ASSERT_FALSE(c.valid(pkt));
}

// Partial writes: peek a chunk, the port takes only part, drop that part
void test_ring_peek_drop_wraps(void)
{
    LinkRing<16> r;
    uint32_t in = 0, out = 0;
    for (unsigned round = 0; round < 1000; round++)
    {
        while (r.space() && (rand() % 4))
            r.push(streamByte(7, in++));
        uint16_t n = r.size() < 5 ? r.size() : 5;
        uint16_t taken = n ? (uint16_t)(rand() % (n + 1)) : 0;
        for (uint16_t i = 0; i < taken; i++)
            TEST_ASSERT_EQUAL_UINT8(streamByte(7, out + i), r.peek(i));
        r.drop(taken);
        out += taken;
        TEST_ASSERT_EQUAL(in - out, r.size());
    }
    TEST_ASSERT_GREATER_THAN(1000u, out);
}

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_lossless_full_rate);
    RUN_TEST(test_heavy_loss_is_exact);
    RUN_TEST(test_corruption_is_rejected);
    RUN_TEST(test_slow_uart_backpressure);
    RUN_TEST(test_idle_link_and_sparse_data);
    RUN_TEST(test_master_reboot);
    RUN_TEST(test_slave_reboot);
    RUN_TEST(test_wrong_seed_or_role_rejected);
    RUN_TEST(test_ring_peek_drop_wraps);
    UNITY_END();
    return 0;
}
