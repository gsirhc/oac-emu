#include <gtest/gtest.h>

#include "cpu_m6809.h"

#include <array>
#include <cstdint>
#include <initializer_list>

namespace {

using m6809::Cpu;

struct Rig {
    std::array<uint8_t, 0x10000> mem{};
    Cpu cpu;

    Rig() : cpu(m6809::Bus{[this](uint16_t a) { return mem[a]; },
                           [this](uint16_t a, uint8_t v) { mem[a] = v; }}) {
        mem[0xFFFE] = 0x10;
        mem[0xFFFF] = 0x00;
        cpu.reset();
    }

    void load(std::initializer_list<uint8_t> bytes, uint16_t at = 0x1000) {
        for (uint8_t v : bytes) mem[at++] = v;
    }

    int run(std::initializer_list<uint8_t> bytes) {
        load(bytes);
        cpu.pc = 0x1000;
        return cpu.step();
    }
};

TEST(M6809, ResetLoadsVectorAndMasks) {
    Rig r;
    EXPECT_EQ(r.cpu.pc, 0x1000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_I));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_F));
    EXPECT_EQ(r.cpu.dp, 0);
}

TEST(M6809, AddSetsHalfCarryOverflowCarry) {
    Rig r;
    r.cpu.a = 0x7F;
    EXPECT_EQ(r.run({0x8B, 0x01}), 2);
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_H));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.a = 0xFF;
    r.run({0x8B, 0x01});
    EXPECT_EQ(r.cpu.a, 0x00);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
}

TEST(M6809, AdcAndSbcUseCarry) {
    Rig r;
    r.cpu.cc |= m6809::FLAG_C;
    r.cpu.b = 0x10;
    r.run({0xC9, 0x05});
    EXPECT_EQ(r.cpu.b, 0x16);
    r.cpu.cc |= m6809::FLAG_C;
    r.run({0xC2, 0x06});
    EXPECT_EQ(r.cpu.b, 0x0F);
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_C));
}

TEST(M6809, CmpSetsBorrowWithoutStoring) {
    Rig r;
    r.cpu.a = 0x10;
    r.run({0x81, 0x20});
    EXPECT_EQ(r.cpu.a, 0x10);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.a = 0x80;
    r.run({0x81, 0x01});
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
}

TEST(M6809, NegFlagsPerAppendixA) {
    Rig r;
    r.cpu.a = 0x80;
    r.run({0x40});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.a = 0x00;
    r.run({0x40});
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_C));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
}

TEST(M6809, ComSetsCarryClearsOverflow) {
    Rig r;
    r.cpu.b = 0x0F;
    r.cpu.cc |= m6809::FLAG_V;
    r.run({0x53});
    EXPECT_EQ(r.cpu.b, 0xF0);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_V));
}

TEST(M6809, ShiftsAndRotates) {
    Rig r;
    r.cpu.a = 0x81;
    r.run({0x44});
    EXPECT_EQ(r.cpu.a, 0x40);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.a = 0x81;
    r.run({0x47});
    EXPECT_EQ(r.cpu.a, 0xC0);
    r.cpu.a = 0x40;
    r.run({0x48});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.cc |= m6809::FLAG_C;
    r.cpu.a = 0x00;
    r.run({0x49});
    EXPECT_EQ(r.cpu.a, 0x01);
    r.cpu.cc |= m6809::FLAG_C;
    r.cpu.a = 0x02;
    r.run({0x46});
    EXPECT_EQ(r.cpu.a, 0x81);
}

TEST(M6809, IncDecOverflowAndCarryUntouched) {
    Rig r;
    r.cpu.cc |= m6809::FLAG_C;
    r.cpu.a = 0x7F;
    r.run({0x4C});
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.a = 0x80;
    r.run({0x4A});
    EXPECT_EQ(r.cpu.a, 0x7F);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
}

TEST(M6809, ClrTstAndMemoryRmw) {
    Rig r;
    r.mem[0x2000] = 0x55;
    EXPECT_EQ(r.run({0x7F, 0x20, 0x00}), 7);
    EXPECT_EQ(r.mem[0x2000], 0x00);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    r.mem[0x0042] = 0x80;
    EXPECT_EQ(r.run({0x0D, 0x42}), 6);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.dp = 0x20;
    r.mem[0x2010] = 0x01;
    r.run({0x0C, 0x10});
    EXPECT_EQ(r.mem[0x2010], 0x02);
}

TEST(M6809, DaaCorrectsBcd) {
    Rig r;
    r.cpu.a = 0x19;
    r.run({0x8B, 0x28});
    r.run({0x19});
    EXPECT_EQ(r.cpu.a, 0x47);
    r.cpu.a = 0x99;
    r.run({0x8B, 0x01});
    r.run({0x19});
    EXPECT_EQ(r.cpu.a, 0x00);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
}

TEST(M6809, MulSexAbx) {
    Rig r;
    r.cpu.a = 0x80;
    r.cpu.b = 0x03;
    EXPECT_EQ(r.run({0x3D}), 11);
    EXPECT_EQ(r.cpu.d(), 0x0180);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.b = 0x90;
    r.run({0x1D});
    EXPECT_EQ(r.cpu.a, 0xFF);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.x = 0x10F0;
    r.cpu.b = 0x20;
    EXPECT_EQ(r.run({0x3A}), 3);
    EXPECT_EQ(r.cpu.x, 0x1110);
}

TEST(M6809, SixteenBitArithmetic) {
    Rig r;
    r.cpu.set_d(0x7FFF);
    EXPECT_EQ(r.run({0xC3, 0x00, 0x01}), 4);
    EXPECT_EQ(r.cpu.d(), 0x8000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_V));
    r.run({0x83, 0x80, 0x01});
    EXPECT_EQ(r.cpu.d(), 0xFFFF);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.x = 0x1234;
    r.run({0x8C, 0x12, 0x34});
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
}

TEST(M6809, LoadsAndStoresBigEndian) {
    Rig r;
    EXPECT_EQ(r.run({0xCC, 0xBE, 0xEF}), 3);
    EXPECT_EQ(r.run({0xFD, 0x30, 0x00}), 6);
    EXPECT_EQ(r.mem[0x3000], 0xBE);
    EXPECT_EQ(r.mem[0x3001], 0xEF);
    EXPECT_EQ(r.run({0x10, 0xCE, 0x40, 0x00}), 4);
    EXPECT_EQ(r.cpu.s, 0x4000);
    EXPECT_EQ(r.run({0x10, 0x8E, 0x00, 0x00}), 4);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    EXPECT_EQ(r.run({0x10, 0xBF, 0x30, 0x10}), 7);
    EXPECT_EQ(r.run({0x10, 0xDF, 0x20}), 6);
    EXPECT_EQ(r.mem[0x0020], 0x40);
}

// Table F-2 extra cycles on LDA indexed (4+).
TEST(M6809, IndexedModesAndCycles) {
    Rig r;
    r.cpu.x = 0x2000;
    r.mem[0x2000] = 0x11;
    EXPECT_EQ(r.run({0xA6, 0x84}), 4);
    EXPECT_EQ(r.cpu.a, 0x11);
    EXPECT_EQ(r.run({0xA6, 0x80}), 6);
    EXPECT_EQ(r.cpu.x, 0x2001);
    EXPECT_EQ(r.run({0xA6, 0x81}), 7);
    EXPECT_EQ(r.cpu.x, 0x2003);
    EXPECT_EQ(r.run({0xA6, 0x82}), 6);
    EXPECT_EQ(r.cpu.x, 0x2002);
    EXPECT_EQ(r.run({0xA6, 0x83}), 7);
    EXPECT_EQ(r.cpu.x, 0x2000);
    r.mem[0x1FFF] = 0x22;
    EXPECT_EQ(r.run({0xA6, 0x1F}), 5);
    EXPECT_EQ(r.cpu.a, 0x22);
    r.cpu.b = 0xFF;
    EXPECT_EQ(r.run({0xA6, 0x85}), 5);
    EXPECT_EQ(r.cpu.a, 0x22);
    r.cpu.a = 0x01;
    r.mem[0x2001] = 0x33;
    EXPECT_EQ(r.run({0xA6, 0x86}), 5);
    EXPECT_EQ(r.cpu.a, 0x33);
    EXPECT_EQ(r.run({0xA6, 0x88, 0xFF}), 5);
    EXPECT_EQ(r.cpu.a, 0x22);
    r.mem[0x2100] = 0x44;
    EXPECT_EQ(r.run({0xA6, 0x89, 0x01, 0x00}), 8);
    EXPECT_EQ(r.cpu.a, 0x44);
    r.cpu.set_d(0x0100);
    EXPECT_EQ(r.run({0xA6, 0x8B}), 8);
    EXPECT_EQ(r.cpu.a, 0x44);
    r.mem[0x1013] = 0x55;
    EXPECT_EQ(r.run({0xA6, 0x8C, 0x10}), 5);
    EXPECT_EQ(r.cpu.a, 0x55);
    r.mem[0x1104] = 0x66;
    EXPECT_EQ(r.run({0xA6, 0x8D, 0x01, 0x00}), 9);
    EXPECT_EQ(r.cpu.a, 0x66);
}

TEST(M6809, IndirectIndexedAddsThree) {
    Rig r;
    r.cpu.y = 0x3000;
    r.mem[0x3000] = 0x40;
    r.mem[0x3001] = 0x00;
    r.mem[0x4000] = 0x77;
    EXPECT_EQ(r.run({0xA6, 0xB4}), 7);
    EXPECT_EQ(r.cpu.a, 0x77);
    EXPECT_EQ(r.run({0xA6, 0xB1}), 10);
    EXPECT_EQ(r.cpu.y, 0x3002);
    r.mem[0x5000] = 0x40;
    r.mem[0x5001] = 0x00;
    EXPECT_EQ(r.run({0xA6, 0x9F, 0x50, 0x00}), 9);
    EXPECT_EQ(r.cpu.a, 0x77);
}

TEST(M6809, LeaFlagsAndRegisterSelect) {
    Rig r;
    r.cpu.u = 0x0001;
    EXPECT_EQ(r.run({0x30, 0x5F}), 5);
    EXPECT_EQ(r.cpu.x, 0x0000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    r.cpu.s = 0x8000;
    r.run({0x32, 0x7E});
    EXPECT_EQ(r.cpu.s, 0x7FFE);
    r.run({0x33, 0x61});
    EXPECT_EQ(r.cpu.u, 0x7FFF);
}

TEST(M6809, PushPullOrderAndCycles) {
    Rig r;
    r.cpu.s = 0x8000;
    r.cpu.a = 0x0A;
    r.cpu.b = 0x0B;
    r.cpu.x = 0x1111;
    r.cpu.cc = 0x50;
    EXPECT_EQ(r.run({0x34, 0x17}), 5 + 5);
    EXPECT_EQ(r.cpu.s, 0x7FFB);
    EXPECT_EQ(r.mem[0x7FFB], 0x50);
    EXPECT_EQ(r.mem[0x7FFC], 0x0A);
    EXPECT_EQ(r.mem[0x7FFD], 0x0B);
    EXPECT_EQ(r.mem[0x7FFE], 0x11);
    r.cpu.a = r.cpu.b = 0;
    r.cpu.x = 0;
    r.run({0x35, 0x17});
    EXPECT_EQ(r.cpu.a, 0x0A);
    EXPECT_EQ(r.cpu.b, 0x0B);
    EXPECT_EQ(r.cpu.x, 0x1111);
    r.cpu.u = 0x6000;
    r.cpu.s = 0x1234;
    r.run({0x36, 0x40});
    EXPECT_EQ(r.mem[0x5FFE], 0x12);
    EXPECT_EQ(r.mem[0x5FFF], 0x34);
}

TEST(M6809, BranchesSignedAndUnsigned) {
    Rig r;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x22, 0x10}), 3);
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_N;
    r.run({0x2D, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_N | m6809::FLAG_V;
    r.run({0x2E, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_Z;
    r.run({0x2F, 0xFE});
    EXPECT_EQ(r.cpu.pc, 0x1000);
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x10, 0x27, 0x10, 0x00}), 5);
    EXPECT_EQ(r.cpu.pc, 0x1004);
    EXPECT_EQ(r.run({0x10, 0x26, 0x10, 0x00}), 6);
    EXPECT_EQ(r.cpu.pc, 0x2004);
    EXPECT_EQ(r.run({0x10, 0x21, 0x10, 0x00}), 5);
    EXPECT_EQ(r.cpu.pc, 0x1004);
}

TEST(M6809, SubroutinesReturn) {
    Rig r;
    r.cpu.s = 0x8000;
    EXPECT_EQ(r.run({0xBD, 0x20, 0x00}), 8);
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.mem[0x7FFE], 0x10);
    EXPECT_EQ(r.mem[0x7FFF], 0x03);
    r.mem[0x2000] = 0x39;
    EXPECT_EQ(r.cpu.step(), 5);
    EXPECT_EQ(r.cpu.pc, 0x1003);
    EXPECT_EQ(r.run({0x8D, 0x02}), 7);
    EXPECT_EQ(r.cpu.pc, 0x1004);
    EXPECT_EQ(r.run({0x17, 0x01, 0x00}), 9);
    EXPECT_EQ(r.cpu.pc, 0x1103);
}

TEST(M6809, TfrExgAndCcTransfer) {
    Rig r;
    r.cpu.x = 0xABCD;
    EXPECT_EQ(r.run({0x1F, 0x12}), 6);
    EXPECT_EQ(r.cpu.y, 0xABCD);
    r.cpu.a = 0x01;
    r.cpu.b = 0x02;
    EXPECT_EQ(r.run({0x1E, 0x89}), 8);
    EXPECT_EQ(r.cpu.a, 0x02);
    EXPECT_EQ(r.cpu.b, 0x01);
    r.cpu.a = 0x0F;
    r.run({0x1F, 0x8A});
    EXPECT_EQ(r.cpu.cc, 0x0F);
    r.run({0x1F, 0x8B});
    EXPECT_EQ(r.cpu.dp, 0x0F);
}

TEST(M6809, OrccAndccMasks) {
    Rig r;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x1A, 0x50}), 3);
    EXPECT_EQ(r.cpu.cc, 0x50);
    r.run({0x1C, 0xEF});
    EXPECT_EQ(r.cpu.cc, 0x40);
}

TEST(M6809, IrqStacksEntireStateAndRtiRestores) {
    Rig r;
    r.mem[0xFFF8] = 0x30;
    r.mem[0xFFF9] = 0x00;
    r.mem[0x3000] = 0x3B;
    r.load({0x12, 0x12});
    r.cpu.pc = 0x1000;
    r.cpu.s = 0x8000;
    r.cpu.cc = 0;
    r.cpu.a = 0xAA;
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 19);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.cpu.s, 0x8000 - 12);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_I));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_F));
    EXPECT_EQ(r.mem[0x8000 - 12] & m6809::FLAG_E, m6809::FLAG_E);
    r.cpu.set_irq(false);
    r.cpu.a = 0;
    EXPECT_EQ(r.cpu.step(), 15);
    EXPECT_EQ(r.cpu.pc, 0x1000);
    EXPECT_EQ(r.cpu.a, 0xAA);
    EXPECT_EQ(r.cpu.s, 0x8000);
}

TEST(M6809, MaskedIrqWaits) {
    Rig r;
    r.load({0x12});
    r.cpu.pc = 0x1000;
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 2);
    EXPECT_EQ(r.cpu.pc, 0x1001);
}

TEST(M6809, FirqStacksPcAndCcOnly) {
    Rig r;
    r.mem[0xFFF6] = 0x30;
    r.mem[0xFFF7] = 0x00;
    r.mem[0x3000] = 0x3B;
    r.cpu.pc = 0x1000;
    r.cpu.s = 0x8000;
    r.cpu.cc = 0;
    r.cpu.set_firq(true);
    EXPECT_EQ(r.cpu.step(), 10);
    EXPECT_EQ(r.cpu.s, 0x8000 - 3);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_F));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_I));
    r.cpu.set_firq(false);
    EXPECT_EQ(r.cpu.step(), 6);
    EXPECT_EQ(r.cpu.pc, 0x1000);
}

TEST(M6809, NmiIgnoredUntilStackLoaded) {
    Rig r;
    r.mem[0xFFFC] = 0x30;
    r.mem[0xFFFD] = 0x00;
    r.load({0x12, 0x10, 0xCE, 0x80, 0x00, 0x12});
    r.cpu.pc = 0x1000;
    r.cpu.set_nmi(true);
    r.cpu.set_nmi(false);
    EXPECT_EQ(r.cpu.step(), 2);
    EXPECT_EQ(r.cpu.step(), 4);
    r.cpu.set_nmi(true);
    EXPECT_EQ(r.cpu.step(), 19);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_F));
    // Edge-triggered: a held line does not re-enter.
    r.mem[0x3000] = 0x12;
    EXPECT_EQ(r.cpu.step(), 2);
}

TEST(M6809, SwiFamilyVectorsAndMasks) {
    Rig r;
    r.mem[0xFFFA] = 0x30;
    r.mem[0xFFFB] = 0x00;
    r.mem[0xFFF4] = 0x31;
    r.mem[0xFFF5] = 0x00;
    r.mem[0xFFF2] = 0x32;
    r.mem[0xFFF3] = 0x00;
    r.cpu.s = 0x8000;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x3F}), 19);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_I));
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_F));
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x10, 0x3F}), 20);
    EXPECT_EQ(r.cpu.pc, 0x3100);
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_I));
    EXPECT_EQ(r.run({0x11, 0x3F}), 20);
    EXPECT_EQ(r.cpu.pc, 0x3200);
}

TEST(M6809, CwaiWaitsWithStateStacked) {
    Rig r;
    r.mem[0xFFF8] = 0x30;
    r.mem[0xFFF9] = 0x00;
    r.cpu.s = 0x8000;
    r.cpu.cc = m6809::FLAG_I;
    EXPECT_EQ(r.run({0x3C, 0xEF}), 17);
    EXPECT_TRUE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.s, 0x8000 - 12);
    EXPECT_EQ(r.cpu.step(), 1);
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 3);
    EXPECT_FALSE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.cpu.s, 0x8000 - 12);
}

// MC6809E Fig. 17: an interrupt already pending gives the manual's minimum of 20.
TEST(M6809, CwaiWithPendingInterruptTakes20) {
    Rig r;
    r.mem[0xFFF8] = 0x30;
    r.mem[0xFFF9] = 0x00;
    r.cpu.s = 0x8000;
    r.cpu.set_irq(true);
    r.cpu.cc = m6809::FLAG_I;
    const int enter = r.run({0x3C, 0xEF});
    EXPECT_EQ(enter + r.cpu.step(), 20);
    EXPECT_EQ(r.cpu.pc, 0x3000);
}

TEST(M6809, SyncResumesOnMaskedInterrupt) {
    Rig r;
    r.load({0x13, 0x12});
    r.cpu.pc = 0x1000;
    EXPECT_EQ(r.cpu.step(), 2);
    EXPECT_TRUE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.step(), 1);
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 2);
    EXPECT_FALSE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0x1002);
}

TEST(M6809, Page3Compares) {
    Rig r;
    r.cpu.u = 0x1000;
    EXPECT_EQ(r.run({0x11, 0x83, 0x10, 0x00}), 5);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    r.cpu.s = 0x0FFF;
    EXPECT_EQ(r.run({0x11, 0xBC, 0x00, 0x00}), 8);
    r.cpu.set_d(0x0001);
    EXPECT_EQ(r.run({0x10, 0x83, 0x00, 0x02}), 5);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
}

TEST(M6809, UndefinedOpcodeCounts) {
    Rig r;
    EXPECT_EQ(r.run({0x01}), 2);
    EXPECT_EQ(r.cpu.illegal_ops, 1u);
}

TEST(M6809, LogicOpsClearOverflow) {
    Rig r;
    r.cpu.a = 0xF0;
    r.cpu.cc |= m6809::FLAG_V;
    EXPECT_EQ(r.run({0x84, 0x3C}), 2);
    EXPECT_EQ(r.cpu.a, 0x30);
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_V));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.b = 0x0F;
    r.run({0xC5, 0xF0});
    EXPECT_EQ(r.cpu.b, 0x0F);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    r.cpu.a = 0xFF;
    r.run({0x88, 0x0F});
    EXPECT_EQ(r.cpu.a, 0xF0);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    r.cpu.b = 0x00;
    r.run({0xCA, 0x81});
    EXPECT_EQ(r.cpu.b, 0x81);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_Z));
}

TEST(M6809, SubIgnoresCarryAndUsesDirectPage) {
    Rig r;
    r.cpu.a = 0x10;
    r.cpu.cc |= m6809::FLAG_C;
    r.run({0x80, 0x01});
    EXPECT_EQ(r.cpu.a, 0x0F);
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_C));
    r.cpu.dp = 0x20;
    r.mem[0x2005] = 0x20;
    r.cpu.b = 0x10;
    EXPECT_EQ(r.run({0xD0, 0x05}), 4);
    EXPECT_EQ(r.cpu.b, 0xF0);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_C));
}

TEST(M6809, DirectAndExtendedLoadsStores) {
    Rig r;
    r.cpu.dp = 0x20;
    r.mem[0x2010] = 0x42;
    EXPECT_EQ(r.run({0x96, 0x10}), 4);
    EXPECT_EQ(r.cpu.a, 0x42);
    EXPECT_EQ(r.run({0x97, 0x11}), 4);
    EXPECT_EQ(r.mem[0x2011], 0x42);
    r.cpu.b = 0x80;
    r.cpu.cc |= m6809::FLAG_V;
    EXPECT_EQ(r.run({0xF7, 0x30, 0x00}), 5);
    EXPECT_EQ(r.mem[0x3000], 0x80);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    EXPECT_FALSE(r.cpu.flag(m6809::FLAG_V));
}

TEST(M6809, IndexRegisterLoadsStores) {
    Rig r;
    EXPECT_EQ(r.run({0xCE, 0x80, 0x00}), 3);
    EXPECT_EQ(r.cpu.u, 0x8000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_N));
    r.mem[0x2000] = 0x12;
    r.mem[0x2001] = 0x34;
    EXPECT_EQ(r.run({0xBE, 0x20, 0x00}), 6);
    EXPECT_EQ(r.cpu.x, 0x1234);
    EXPECT_EQ(r.run({0x9F, 0x40}), 5);
    EXPECT_EQ(r.mem[0x0040], 0x12);
    EXPECT_EQ(r.mem[0x0041], 0x34);
    EXPECT_EQ(r.run({0xFF, 0x30, 0x00}), 6);
    EXPECT_EQ(r.mem[0x3000], 0x80);
    EXPECT_EQ(r.mem[0x3001], 0x00);
    r.cpu.y = 0x1234;
    EXPECT_EQ(r.run({0x10, 0x8C, 0x12, 0x34}), 5);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
}

TEST(M6809, JumpsAndLongBranches) {
    Rig r;
    r.cpu.dp = 0x20;
    EXPECT_EQ(r.run({0x0E, 0x50}), 3);
    EXPECT_EQ(r.cpu.pc, 0x2050);
    EXPECT_EQ(r.run({0x7E, 0x40, 0x00}), 4);
    EXPECT_EQ(r.cpu.pc, 0x4000);
    r.cpu.x = 0x5000;
    EXPECT_EQ(r.run({0x6E, 0x84}), 3);
    EXPECT_EQ(r.cpu.pc, 0x5000);
    EXPECT_EQ(r.run({0x16, 0x01, 0x00}), 5);
    EXPECT_EQ(r.cpu.pc, 0x1103);
    r.run({0x16, 0xFF, 0xFD});
    EXPECT_EQ(r.cpu.pc, 0x1000);
    r.cpu.s = 0x8000;
    EXPECT_EQ(r.run({0x17, 0x00, 0x10}), 9);
    EXPECT_EQ(r.cpu.pc, 0x1013);
    EXPECT_EQ(r.mem[0x7FFE], 0x10);
    EXPECT_EQ(r.mem[0x7FFF], 0x03);
}

TEST(M6809, ConditionalBranchesOnSingleFlags) {
    Rig r;
    r.cpu.cc = 0;
    r.run({0x24, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_C;
    r.run({0x24, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1002);
    r.cpu.cc = 0;
    r.run({0x28, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_V;
    r.run({0x28, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1002);
    r.cpu.cc = 0;
    r.run({0x2A, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1012);
    r.cpu.cc = m6809::FLAG_N;
    r.run({0x2A, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x1002);
}

TEST(M6809, PullsLoadTheOtherStackPointer) {
    Rig r;
    r.cpu.u = 0x2000;
    r.mem[0x2000] = 0x12;
    r.mem[0x2001] = 0x34;
    EXPECT_EQ(r.run({0x37, 0x40}), 7);
    EXPECT_EQ(r.cpu.s, 0x1234);
    EXPECT_EQ(r.cpu.u, 0x2002);
    r.cpu.s = 0x3000;
    r.mem[0x3000] = 0x56;
    r.mem[0x3001] = 0x78;
    r.run({0x35, 0x40});
    EXPECT_EQ(r.cpu.u, 0x5678);
}

TEST(M6809, LeayAndPcRelativeIndexing) {
    Rig r;
    r.cpu.y = 0xFFFF;
    EXPECT_EQ(r.run({0x31, 0x21}), 5);
    EXPECT_EQ(r.cpu.y, 0x0000);
    EXPECT_TRUE(r.cpu.flag(m6809::FLAG_Z));
    r.mem[0x1008] = 0x5A;
    EXPECT_EQ(r.run({0xA6, 0x8C, 0x05}), 5);
    EXPECT_EQ(r.cpu.a, 0x5A);
    r.mem[0x1104] = 0xA5;
    EXPECT_EQ(r.run({0xA6, 0x8D, 0x01, 0x00}), 9);
    EXPECT_EQ(r.cpu.a, 0xA5);
}

TEST(M6809, TransfersAcrossEveryRegister) {
    Rig r;
    r.cpu.set_d(0x1111);
    r.run({0x1F, 0x02});
    EXPECT_EQ(r.cpu.y, 0x1111);
    r.cpu.u = 0x2222;
    r.run({0x1F, 0x34});
    EXPECT_EQ(r.cpu.s, 0x2222);
    r.run({0x1F, 0x40});
    EXPECT_EQ(r.cpu.d(), 0x2222);
    r.cpu.cc = 0x55;
    r.run({0x1F, 0xAB});
    EXPECT_EQ(r.cpu.dp, 0x55);
    r.run({0x1F, 0xB8});
    EXPECT_EQ(r.cpu.a, 0x55);
    r.cpu.u = 0x3333;
    r.run({0x1E, 0x03});
    EXPECT_EQ(r.cpu.u, 0x5522);
    EXPECT_EQ(r.cpu.d(), 0x3333);
    r.cpu.x = 0x4000;
    r.run({0x1F, 0x15});
    EXPECT_EQ(r.cpu.pc, 0x4000);
}

// Mixed sizes and unused codes are undefined (M6809PM); JOUST_REVIEW.md §15 has the choice.
TEST(M6809, TransferUndefinedCombinations) {
    Rig r;
    r.cpu.a = 0x12;
    r.run({0x1F, 0x81});
    EXPECT_EQ(r.cpu.x, 0xFF12);
    r.cpu.x = 0xABCD;
    r.run({0x1F, 0x19});
    EXPECT_EQ(r.cpu.b, 0xCD);
    r.run({0x1F, 0x61});
    EXPECT_EQ(r.cpu.x, 0xFFFF);
    r.cpu.x = 0x1234;
    r.run({0x1F, 0x16});
    EXPECT_EQ(r.cpu.x, 0x1234);
}

TEST(M6809, UndefinedPrefixedOpcodesCount) {
    Rig r;
    EXPECT_EQ(r.run({0x10, 0x00}), 2);
    EXPECT_EQ(r.run({0x10, 0xC3}), 2);
    EXPECT_EQ(r.run({0x10, 0x8F}), 2);
    EXPECT_EQ(r.run({0x11, 0x8E}), 2);
    EXPECT_EQ(r.run({0xA6, 0x87}), 4);
    EXPECT_EQ(r.cpu.illegal_ops, 5u);
}

}  // namespace
