#include <gtest/gtest.h>

#include "../ns16450.h"
#include "../serial_mouse.h"

namespace {

using ibmpcat::SerialMouse;

void program_1200_7n1(pc::Ns16450 &u) {
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 96);
    u.out(0x3F9, 0);
    u.out(0x3FB, 0x02);
}

void drain_one(pc::Ns16450 &u, uint64_t &t) {
    // 9 bits at 1200 baud, 8 MHz CPU: 60000 cycles.
    t += 60000;
    u.tick(t, 8e6);
}

TEST(SerialMouseTest, SendsMAfterDtrRise) {
    pc::Ns16450 u(0x3F8);
    SerialMouse m(u);
    m.reset();
    program_1200_7n1(u);
    uint64_t t = 0;
    u.tick(t, 8e6);
    u.out(0x3FC, 0x01);  // DTR
    m.tick();
    drain_one(u, t);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x01);
    EXPECT_EQ(u.in(0x3F8), 0x4D);
}

TEST(SerialMouseTest, PacketAfterInject) {
    pc::Ns16450 u(0x3F8);
    SerialMouse m(u);
    m.reset();
    program_1200_7n1(u);
    uint64_t t = 0;
    u.tick(t, 8e6);
    u.out(0x3FC, 0x03);  // DTR|RTS
    m.tick();
    drain_one(u, t);
    EXPECT_EQ(u.in(0x3F8), 0x4D);  // eat ident
    m.inject(5, -3, SerialMouse::kLeft);
    uint8_t pkt[3];
    for (int i = 0; i < 3; ++i) {
        drain_one(u, t);
        ASSERT_EQ(u.in(0x3FD) & 0x01, 0x01);
        pkt[i] = u.in(0x3F8);
    }
    EXPECT_EQ(pkt[0], 0x6C);  // sync|left|Yhi=3
    EXPECT_EQ(pkt[1], 5);
    EXPECT_EQ(pkt[2], 0x3D);  // low 6 bits of -3
}

TEST(SerialMouseTest, SilentWithoutPower) {
    pc::Ns16450 u(0x3F8);
    SerialMouse m(u);
    m.reset();
    program_1200_7n1(u);
    uint64_t t = 0;
    u.tick(t, 8e6);
    m.inject(1, 1, 0);
    drain_one(u, t);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x00);
}

TEST(SerialMouseTest, CoalescesMotionWhileAPacketIsQueued) {
    pc::Ns16450 u(0x3F8);
    SerialMouse m(u);
    m.reset();
    program_1200_7n1(u);
    uint64_t t = 0;
    u.tick(t, 8e6);
    u.out(0x3FC, 0x03);
    m.tick();
    drain_one(u, t);
    EXPECT_EQ(u.in(0x3F8), 0x4D);

    m.inject(10, 0, 0);
    EXPECT_EQ(u.rx_queued(), 3u);
    m.inject(10, 0, 0);
    m.inject(10, 0, 0);
    EXPECT_EQ(u.rx_queued(), 3u);  // still one report, not nine bytes

    uint8_t first[3];
    for (int i = 0; i < 3; ++i) {
        drain_one(u, t);
        ASSERT_EQ(u.in(0x3FD) & 0x01, 0x01);
        first[i] = u.in(0x3F8);
    }
    EXPECT_EQ(first[1], 10);  // first report was the initial 10

    m.tick();  // previous packet gone; flush the coalesced 20
    uint8_t second[3];
    for (int i = 0; i < 3; ++i) {
        drain_one(u, t);
        ASSERT_EQ(u.in(0x3FD) & 0x01, 0x01);
        second[i] = u.in(0x3F8);
    }
    EXPECT_EQ(second[1], 20);
}

}  // namespace
