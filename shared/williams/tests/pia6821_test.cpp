#include <gtest/gtest.h>

#include "williams/pia6821.h"

namespace {

using williams::Pia6821;

TEST(Pia6821, DdrAndOutputRegisterShareAddress) {
    Pia6821 p;
    p.reset();
    p.write(0, 0x0F);
    EXPECT_EQ(p.read(0), 0x0F);
    p.write(1, 0x04);
    p.write(0, 0xA5);
    EXPECT_EQ(p.read(1) & 0x04, 0x04);
    p.in_a = [] { return uint8_t(0x30); };
    EXPECT_EQ(p.read(0), 0x35);
}

TEST(Pia6821, PortBOutputsReadBackTheRegister) {
    Pia6821 p;
    p.in_b = [] { return uint8_t(0x00); };
    p.reset();
    p.write(2, 0xF0);
    p.write(3, 0x04);
    p.write(2, 0xAA);
    EXPECT_EQ(p.read(2), 0xA0);
}

TEST(Pia6821, Ca1EdgeSetsFlagAndIrqWhenEnabled) {
    Pia6821 p;
    p.reset();
    p.write(1, 0x07);
    EXPECT_FALSE(p.irq_a());
    p.set_ca1(true);
    EXPECT_EQ(p.read(1) & 0x80, 0x80);
    EXPECT_TRUE(p.irq_a());
    p.read(0);
    EXPECT_FALSE(p.irq_a());
    EXPECT_EQ(p.read(1) & 0x80, 0);
}

TEST(Pia6821, Cb1FallingEdgeWhenB1Clear) {
    Pia6821 p;
    p.reset();
    p.write(3, 0x05);
    p.set_cb1(true);
    EXPECT_FALSE(p.irq_b());
    p.set_cb1(false);
    EXPECT_TRUE(p.irq_b());
}

TEST(Pia6821, FlagSetsWhileDisabledThenRaisesIrqOnEnable) {
    Pia6821 p;
    p.reset();
    p.write(1, 0x06);
    p.set_ca1(true);
    EXPECT_FALSE(p.irq_a());
    p.write(1, 0x07);
    EXPECT_TRUE(p.irq_a());
}

TEST(Pia6821, C2FollowsB3InSetResetMode) {
    Pia6821 p;
    bool cb2 = false;
    p.out_cb2 = [&](bool v) { cb2 = v; };
    p.reset();
    p.write(3, 0x3C);
    EXPECT_TRUE(cb2);
    EXPECT_TRUE(p.cb2());
    p.write(3, 0x34);
    EXPECT_FALSE(cb2);
}

TEST(Pia6821, Ca2ReadStrobeRestoresOnE) {
    Pia6821 p;
    p.reset();
    p.write(1, 0x2C);
    EXPECT_TRUE(p.ca2());
    p.read(0);
    EXPECT_FALSE(p.ca2());
    p.clock_e();
    EXPECT_TRUE(p.ca2());
}

TEST(Pia6821, Cb2WriteStrobeRestoresOnCb1) {
    Pia6821 p;
    p.reset();
    p.write(3, 0x26);
    p.write(2, 0x55);
    EXPECT_FALSE(p.cb2());
    p.set_cb1(true);
    EXPECT_TRUE(p.cb2());
}

TEST(Pia6821, C2InputEdgeSetsBit6) {
    Pia6821 p;
    p.reset();
    p.write(1, 0x1C);
    p.set_ca2(true);
    EXPECT_EQ(p.read(1) & 0x40, 0x40);
    EXPECT_TRUE(p.irq_a());
}

TEST(Pia6821, OutputCallbackSeesPortLevels) {
    Pia6821 p;
    uint8_t seen = 0;
    p.out_b = [&](uint8_t v) { seen = v; };
    p.reset();
    p.write(2, 0xFF);
    p.write(3, 0x04);
    p.write(2, 0x3E);
    EXPECT_EQ(seen, 0x3E);
}

}  // namespace
