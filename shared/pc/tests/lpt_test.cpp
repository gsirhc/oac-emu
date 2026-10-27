#include <gtest/gtest.h>

#include "../lpt.h"

namespace {

using pc::Lpt;

TEST(LptTest, OwnsThreePorts) {
    Lpt p(0x378, 0);
    EXPECT_TRUE(p.owns(0x378));
    EXPECT_TRUE(p.owns(0x37A));
    EXPECT_FALSE(p.owns(0x37B));
}

TEST(LptTest, IdleStatusLooksLikeAReadyPrinter) {
    Lpt p(0x378, 0);
    uint8_t s = p.in(0x379);
    EXPECT_NE(s & 0x80, 0);  // /BUSY
    EXPECT_NE(s & 0x40, 0);  // /ACK
    EXPECT_EQ(s & 0x20, 0);  // no PE
    EXPECT_NE(s & 0x10, 0);  // SELECT
    EXPECT_NE(s & 0x08, 0);  // /ERROR
}

TEST(LptTest, StrobeRisingEdgeCapturesData) {
    Lpt p(0x378, 0);
    p.tick(0, 8e6);
    p.out(0x378, 'A');
    p.out(0x37A, 0x01);  // strobe high
    p.out(0x37A, 0x00);  // strobe low
    ASSERT_EQ(p.capture_size(), 1u);
    EXPECT_EQ(p.capture()[0], 'A');
    p.out(0x378, 'B');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    ASSERT_EQ(p.capture_size(), 2u);
    EXPECT_EQ(p.capture()[1], 'B');
}

TEST(LptTest, BusyClearsAfterCharacterTime) {
    Lpt p(0x378, 100);  // 100 cps -> 80_000 cycles at 8 MHz
    p.tick(0, 8e6);
    p.out(0x378, 'X');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    EXPECT_EQ(p.in(0x379) & 0x80, 0);  // busy
    p.tick(40000, 8e6);
    EXPECT_EQ(p.in(0x379) & 0x80, 0);
    p.tick(80000, 8e6);
    EXPECT_NE(p.in(0x379) & 0x80, 0);
}

TEST(LptTest, AckLowImmediatelyAfterStrobe) {
    // BIOS-bochs-legacy INT 17h polls /ACK once after the strobe pulse.
    Lpt p(0x378, 100);
    p.tick(0, 8e6);
    p.out(0x378, 'A');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    EXPECT_EQ(p.in(0x379) & 0x40, 0);  // /ACK asserted
}

TEST(LptTest, StrobeWhileBusyStillCaptures) {
    Lpt p(0x378, 100);
    p.tick(0, 8e6);
    p.out(0x378, 'A');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    p.out(0x378, 'B');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    ASSERT_EQ(p.capture_size(), 2u);
    EXPECT_EQ(p.capture()[1], 'B');
}

TEST(LptTest, InitPulseClearsCapture) {
    Lpt p(0x378, 0);
    p.tick(0, 8e6);
    p.out(0x378, 'Z');
    p.out(0x37A, 0x01);
    p.out(0x37A, 0x00);
    EXPECT_EQ(p.capture_size(), 1u);
    p.out(0x37A, 0x04);  // /INIT high (idle)
    p.out(0x37A, 0x00);  // /INIT low
    EXPECT_EQ(p.capture_size(), 0u);
}

TEST(LptTest, IrqOnAckWhenEnabled) {
    Lpt p(0x378, 1000);
    p.tick(0, 8e6);
    p.out(0x37A, 0x10);  // IRQ enable, strobe low, /INIT high
    p.out(0x378, 'Q');
    p.out(0x37A, 0x11);  // strobe
    p.out(0x37A, 0x10);
    EXPECT_TRUE(p.irq_pending());
}

}  // namespace
