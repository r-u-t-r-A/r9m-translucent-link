/*
 * Native tests for the optional MAVLink mode (src/link_mavlink.h).
 */
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <unity.h>

#include "../../src/link_arq.h"
#include "../../src/link_mavlink.h"

typedef std::vector<uint8_t> Bytes;

// Synthetic frames: only the length fields matter to the tracker
static Bytes frameV1(uint8_t len, uint8_t fill)
{
    Bytes f = {MavTracker::STX_V1, len};
    for (unsigned i = 0; i < 4u + len + 2u; i++)
        f.push_back((uint8_t)(fill + i));
    return f;
}

static Bytes frameV2(uint8_t len, uint8_t fill, bool signedFrame = false)
{
    Bytes f = {MavTracker::STX_V2, len, (uint8_t)(signedFrame ? 0x01 : 0x00)};
    for (unsigned i = 0; i < 7u + len + 2u + (signedFrame ? 13u : 0u); i++)
        f.push_back((uint8_t)(fill + i));
    return f;
}

static void append(Bytes &a, const Bytes &b) { a.insert(a.end(), b.begin(), b.end()); }

void test_radio_status_matches_pymavlink()
{
    // Reference frames from pymavlink (MAVLink 2, sysid 51, compid 68)
    struct Case
    {
        uint8_t seq;
        MavRadioStatus st;
        Bytes expect;
    } cases[] = {
        {7, {513, 0, 150, 140, 87, 40, 0},
         {0xFD, 0x08, 0x00, 0x00, 0x07, 0x33, 0x44, 0x6D, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x96, 0x8C, 0x57, 0x28, 0x9C, 0x9A}},
        {200, {0, 0, 0, 0, 100, 0, 0},
         {0xFD, 0x07, 0x00, 0x00, 0xC8, 0x33, 0x44, 0x6D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x13, 0x6C}},
        {0, {0, 0, 0, 0, 0, 0, 0},
         {0xFD, 0x01, 0x00, 0x00, 0x00, 0x33, 0x44, 0x6D, 0x00, 0x00, 0x00, 0x6E, 0x3C}},
        {9, {65535, 1, 254, 254, 0, 1, 2},
         {0xFD, 0x09, 0x00, 0x00, 0x09, 0x33, 0x44, 0x6D, 0x00, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0xFE, 0xFE, 0x00, 0x01, 0x02, 0x48, 0x45}},
    };
    for (const Case &c : cases)
    {
        uint8_t buf[MAV_RADIO_STATUS_MAX_LEN];
        uint8_t len = mavPackRadioStatus(buf, c.seq, c.st);
        TEST_ASSERT_EQUAL_UINT(c.expect.size(), len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(c.expect.data(), buf, len);

        // and the tracker sees exactly one frame in it
        MavTracker t;
        for (uint8_t i = 0; i < len; i++)
            TEST_ASSERT_EQUAL(i == len - 1, t.feed(buf[i]));
        TEST_ASSERT_TRUE(t.betweenFrames());
    }
}

void test_tracker_finds_frame_ends()
{
    Bytes s = {'A', 'B'};
    std::vector<size_t> ends;
    append(s, frameV1(9, 0x10));          ends.push_back(s.size() - 1);
    append(s, frameV2(20, 0xFD));         ends.push_back(s.size() - 1); // payload full of STX bytes
    append(s, frameV2(5, 0x20, true));    ends.push_back(s.size() - 1);
    s.push_back('C');
    append(s, frameV2(0, 0xFE));          ends.push_back(s.size() - 1);
    append(s, frameV1(255, 0x00));        ends.push_back(s.size() - 1);
    append(s, frameV2(255, 0x00, true));  ends.push_back(s.size() - 1);
    s.push_back('D');

    TEST_ASSERT_EQUAL_UINT(17, frameV1(9, 0).size());
    TEST_ASSERT_EQUAL_UINT(32, frameV2(20, 0).size());
    TEST_ASSERT_EQUAL_UINT(30, frameV2(5, 0, true).size());
    TEST_ASSERT_EQUAL_UINT(MavTracker::MAX_FRAME, frameV2(255, 0, true).size());

    MavTracker t;
    std::vector<size_t> found;
    for (size_t i = 0; i < s.size(); i++)
        if (t.feed(s[i]))
            found.push_back(i);
    TEST_ASSERT_EQUAL_UINT(ends.size(), found.size());
    for (size_t i = 0; i < ends.size(); i++)
        TEST_ASSERT_EQUAL_UINT(ends[i], found[i]);
    TEST_ASSERT_TRUE(t.betweenFrames());
}

void test_tracker_until_boundary()
{
    MavTracker t;
    TEST_ASSERT_EQUAL_UINT(0, t.untilBoundary());
    t.feed(MavTracker::STX_V2);
    TEST_ASSERT_EQUAL_UINT(1, t.untilBoundary()); // length not known yet
    t.feed(10);
    TEST_ASSERT_EQUAL_UINT(1, t.untilBoundary());
    t.feed(0);                                    // incompat flags
    TEST_ASSERT_EQUAL_UINT(10 + 9, t.untilBoundary());
    for (int i = 0; i < 18; i++)
        TEST_ASSERT_FALSE(t.feed(0x55));
    TEST_ASSERT_EQUAL_UINT(1, t.untilBoundary());
    TEST_ASSERT_TRUE(t.feed(0x55));
    TEST_ASSERT_EQUAL_UINT(0, t.untilBoundary());
}

typedef LinkRing<64> SmallRing;

static Bytes drainAll(SmallRing &r)
{
    Bytes out;
    while (r.size())
        out.push_back(r.pop());
    return out;
}

void test_input_drops_whole_frames()
{
    SmallRing ring;
    MavInput<SmallRing> in;
    Bytes f = frameV2(18, 0x40); // 30 bytes: two fit in 64, a third doesn't
    for (int k = 0; k < 3; k++)
        for (uint8_t b : f)
            in.put(b, ring, 0);
    TEST_ASSERT_EQUAL_UINT(60, ring.size());
    TEST_ASSERT_EQUAL_UINT32(1, in.droppedFrames);

    Bytes out = drainAll(ring);
    Bytes two = f;
    append(two, f);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(two.data(), out.data(), two.size());

    // room again: the next frame goes through
    for (uint8_t b : f)
        in.put(b, ring, 0);
    TEST_ASSERT_EQUAL_UINT(30, ring.size());
}

void test_input_passes_plain_bytes()
{
    SmallRing ring;
    MavInput<SmallRing> in;
    const char *text = "hello, world";
    for (const char *p = text; *p; p++)
        in.put((uint8_t)*p, ring, 0);
    Bytes out = drainAll(ring);
    TEST_ASSERT_EQUAL_UINT(12, out.size());
    TEST_ASSERT_EQUAL_MEMORY(text, out.data(), 12);
}

void test_input_flushes_stalled_frame()
{
    SmallRing ring;
    MavInput<SmallRing> in;
    in.put(MavTracker::STX_V1, ring, 100); // looks like a frame start...
    in.put(40, ring, 100);
    in.put('x', ring, 100);
    in.poll(ring, 149, 50);
    TEST_ASSERT_EQUAL_UINT(0, ring.size()); // still waiting
    in.poll(ring, 150, 50);
    TEST_ASSERT_EQUAL_UINT(3, ring.size()); // ...but nothing follows: passed on
    in.put('y', ring, 200);                 // and the stream continues as plain bytes
    Bytes out = drainAll(ring);
    Bytes expect = {MavTracker::STX_V1, 40, 'x', 'y'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect.data(), out.data(), expect.size());
}

// Random frames into a small, slowly drained buffer: whatever comes out must be
// a sequence of complete input frames, in order, with nothing cut.
void test_input_overflow_keeps_frames_intact()
{
    srand(42);
    LinkRing<256> ring;
    MavInput<LinkRing<256>> in;
    std::vector<Bytes> sent;
    Bytes out;
    for (int k = 0; k < 2000; k++)
    {
        Bytes f = (rand() & 1) ? frameV2((uint8_t)(rand() % 60), (uint8_t)k, (rand() % 8) == 0)
                               : frameV1((uint8_t)(rand() % 60), (uint8_t)k);
        f[f.size() - 1] = (uint8_t)k; // make frames distinguishable
        f[f.size() - 2] = (uint8_t)(k >> 8);
        sent.push_back(f);
        for (uint8_t b : f)
        {
            in.put(b, ring, 0);
            if ((rand() % 3) == 0 && ring.size()) // drain at about a third of the input rate
                out.push_back(ring.pop());
        }
    }
    while (ring.size())
        out.push_back(ring.pop());

    TEST_ASSERT_GREATER_THAN_UINT32(100, in.droppedFrames);
    size_t pos = 0, next = 0, matched = 0;
    while (pos < out.size())
    {
        bool found = false;
        for (; next < sent.size(); next++)
        {
            const Bytes &f = sent[next];
            if (pos + f.size() <= out.size() && std::equal(f.begin(), f.end(), out.begin() + pos))
            {
                pos += f.size();
                next++;
                matched++;
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "output contains a partial or reordered frame");
    }
    TEST_ASSERT_EQUAL_UINT32(sent.size(), matched + in.droppedFrames);
}

void test_rssi_scale()
{
    TEST_ASSERT_EQUAL_UINT8(0, mavRssiFromDbm(-127));
    TEST_ASSERT_EQUAL_UINT8(0, mavRssiFromDbm(-140));
    TEST_ASSERT_EQUAL_UINT8(146, mavRssiFromDbm(-50)); // (77 * 1.9) = 146.3
    TEST_ASSERT_EQUAL_UINT8(254, mavRssiFromDbm(20));
}

void test_arq_carries_status_byte()
{
    LinkArq<64, 256, 256> a, b;
    a.begin(true, 0x1234, 1);
    b.begin(false, 0x1234, 2);
    uint8_t pkt[64];
    a.txStatus = 0xA5;
    a.build(pkt, 0);
    TEST_ASSERT_TRUE(b.valid(pkt));
    b.process(pkt);
    TEST_ASSERT_EQUAL_HEX8(0xA5, b.peerStatus);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_radio_status_matches_pymavlink);
    RUN_TEST(test_tracker_finds_frame_ends);
    RUN_TEST(test_tracker_until_boundary);
    RUN_TEST(test_input_drops_whole_frames);
    RUN_TEST(test_input_passes_plain_bytes);
    RUN_TEST(test_input_flushes_stalled_frame);
    RUN_TEST(test_input_overflow_keeps_frames_intact);
    RUN_TEST(test_rssi_scale);
    RUN_TEST(test_arq_carries_status_byte);
    return UNITY_END();
}
