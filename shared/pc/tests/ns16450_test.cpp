#include <gtest/gtest.h>

#include "../ns16450.h"

namespace {

using pc::Ns16450;

TEST(Ns16450Test, OwnsEightPortsAtBase) {
    Ns16450 u(0x3F8);
    EXPECT_TRUE(u.owns(0x3F8));
    EXPECT_TRUE(u.owns(0x3FF));
    EXPECT_FALSE(u.owns(0x3F7));
    EXPECT_FALSE(u.owns(0x400));
}

TEST(Ns16450Test, ScratchAndLcrRoundTrip) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x03);  // 8N1
    EXPECT_EQ(u.in(0x3FB), 0x03);
    u.out(0x3FF, 0xA5);
    EXPECT_EQ(u.in(0x3FF), 0xA5);
}

TEST(Ns16450Test, DlabSwapsDivisorOntoDataPorts) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x80);  // DLAB
    u.out(0x3F8, 0x60);  // DLL = 96 -> 1200 baud
    u.out(0x3F9, 0x00);
    EXPECT_EQ(u.in(0x3F8), 0x60);
    EXPECT_EQ(u.in(0x3F9), 0x00);
    u.out(0x3FB, 0x02);  // 7N1, DLAB clear
    u.out(0x3F9, 0x01);  // IER
    EXPECT_EQ(u.in(0x3F9), 0x01);
}

TEST(Ns16450Test, LoopbackEchoesThrIntoRbr) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x03);
    u.out(0x3FC, 0x10);  // loopback
    u.out(0x3F8, 0x5A);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x01);
    EXPECT_EQ(u.in(0x3F8), 0x5A);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x00);
}

TEST(Ns16450Test, IrqRequiresOut2AndEnabledSource) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x03);
    u.out(0x3F9, 0x01);  // ERBFI
    u.push_rx(0x11);
    // Instant delivery needs a tick with enough credit; force via huge step at 1200 8N1.
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 0x01);  // divisor 1 -> very fast
    u.out(0x3F9, 0x00);
    u.out(0x3FB, 0x03);
    u.tick(0, 8e6);
    u.tick(100000, 8e6);
    EXPECT_FALSE(u.irq_pending());  // OUT2 clear
    u.out(0x3FC, 0x08);             // OUT2
    EXPECT_TRUE(u.irq_pending());
    EXPECT_EQ(u.in(0x3FA) & 0x07, 0x04);  // RDA
}

TEST(Ns16450Test, LsrReadClearsOverrun) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 1);
    u.out(0x3F9, 0);
    u.out(0x3FB, 0x03);
    u.tick(0, 8e6);
    u.push_rx(0x11);
    u.push_rx(0x22);
    u.tick(100000, 8e6);
    EXPECT_EQ(u.in(0x3FD) & 0x02, 0x02);  // OE set (second byte while RBR full)
    EXPECT_EQ(u.in(0x3FD) & 0x02, 0x00);  // cleared by the previous read
}

TEST(Ns16450Test, PushRxArrivesAfterCharacterTime) {
    Ns16450 u(0x3F8);
    // 1200 baud, 7N1: divisor 96, frame = 1+7+1 = 9 bits -> 7.5 ms.
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 96);
    u.out(0x3F9, 0);
    u.out(0x3FB, 0x02);  // 7 data bits
    u.tick(0, 8e6);
    u.push_rx(0x4D);
    u.tick(1000, 8e6);  // too soon
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x00);
    // 9 bits at 1200 baud = 9/1200 s * 8e6 = 60000 cycles.
    u.tick(0 + 60000, 8e6);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x01);
    EXPECT_EQ(u.in(0x3F8), 0x4D);
}

TEST(Ns16450Test, IdleTimeDoesNotBankCharacterTimes) {
    Ns16450 u(0x3F8);
    u.out(0x3FB, 0x80);
    u.out(0x3F8, 96);
    u.out(0x3F9, 0);
    u.out(0x3FB, 0x02);
    u.tick(0, 8e6);
    u.tick(10'000'000, 8e6);  // long idle must not pre-pay the next byte
    u.push_rx(0x4D);
    u.push_rx(0x11);
    u.push_rx(0x22);
    u.tick(10'000'000 + 1000, 8e6);
    EXPECT_EQ(u.in(0x3FD) & 0x01, 0x00);  // still shifting the first byte
    EXPECT_EQ(u.rx_queued(), 2u);
}

}  // namespace
