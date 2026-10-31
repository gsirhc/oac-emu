#include "cpu_m6800.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>

using namespace m6800;

namespace {

struct Rig {
    std::array<uint8_t, 65536> ram{};
    Cpu cpu;

    Rig() : cpu(bus()) {
        cpu.pc = 0x0200;
        cpu.sp = 0x01FF;
        cpu.cc = 0;
    }

    Bus bus() {
        Bus b;
        b.read  = [this](uint16_t a) { return ram[a]; };
        b.write = [this](uint16_t a, uint8_t v) { ram[a] = v; };
        return b;
    }

    void load(uint16_t addr, std::initializer_list<uint8_t> bytes) {
        for (uint8_t v : bytes) ram[addr++] = v;
    }

    void prog(std::initializer_list<uint8_t> bytes) {
        load(0x0200, bytes);
        cpu.pc = 0x0200;
    }

    int run(std::initializer_list<uint8_t> bytes) {
        prog(bytes);
        return cpu.step();
    }

    void vec(uint16_t at, uint16_t to) {
        ram[at] = uint8_t(to >> 8);
        ram[at + 1] = uint8_t(to);
    }
};

// Execution times, Fig. 4-1 / App. A of M68PRM(D). 0 = unassigned code.
constexpr uint8_t kCycles[256] = {
    /*0x*/ 0, 2, 0, 0, 0, 0, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,
    /*1x*/ 2, 2, 0, 0, 0, 0, 2, 2, 0, 2, 0, 2, 0, 0, 0, 0,
    /*2x*/ 4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /*3x*/ 4, 4, 4, 4, 4, 4, 4, 4, 0, 5, 0, 10, 0, 0, 9, 12,
    /*4x*/ 2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
    /*5x*/ 2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
    /*6x*/ 7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,
    /*7x*/ 6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,
    /*8x*/ 2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 3, 8, 3, 0,
    /*9x*/ 3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 4, 0, 4, 5,
    /*Ax*/ 5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,
    /*Bx*/ 4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,
    /*Cx*/ 2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 0, 0, 3, 0,
    /*Dx*/ 3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 0, 0, 4, 5,
    /*Ex*/ 5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 0, 0, 6, 7,
    /*Fx*/ 4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 0, 0, 5, 6,
};

}  // namespace

TEST(M6800, ResetLoadsVectorAndMasksInterrupts) {
    Rig r;
    r.vec(0xFFFE, 0xF000);
    r.cpu.cc = 0;
    r.cpu.reset();
    EXPECT_EQ(r.cpu.pc, 0xF000);
    EXPECT_TRUE(r.cpu.flag(FLAG_I));
    EXPECT_FALSE(r.cpu.waiting());
}

TEST(M6800, ValidOpcodeCountIs197) {
    int valid = 0;
    for (int op = 0; op < 256; op++) valid += kCycles[op] != 0;
    EXPECT_EQ(valid, 197);
}

TEST(M6800, EveryOpcodeCycleCount) {
    for (int op = 0; op < 256; op++) {
        Rig r;
        r.ram[0x0200] = uint8_t(op);
        r.vec(0xFFF8, 0x1000);
        r.cpu.sp = 0x0180;
        int n = r.cpu.step();
        if (kCycles[op] == 0) {
            EXPECT_EQ(n, 2) << std::hex << op;
            EXPECT_EQ(r.cpu.undefined_ops, 1u) << std::hex << op;
            EXPECT_EQ(r.cpu.pc, 0x0201) << std::hex << op;
        } else {
            EXPECT_EQ(n, kCycles[op]) << std::hex << op;
            EXPECT_EQ(r.cpu.undefined_ops, 0u) << std::hex << op;
        }
        EXPECT_EQ(r.cpu.cycles, uint64_t(n)) << std::hex << op;
    }
}

TEST(M6800, ImmediateLoadAndFlags) {
    Rig r;
    EXPECT_EQ(r.run({0x86, 0x80}), 2);
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
    EXPECT_EQ(r.cpu.pc, 0x0202);
    r.cpu.cc |= FLAG_V | FLAG_C;
    r.run({0xC6, 0x00});
    EXPECT_EQ(r.cpu.b, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
}

TEST(M6800, DirectAddressingIsPageZero) {
    Rig r;
    r.ram[0x0042] = 0x5A;
    r.ram[0x0242] = 0xFF;
    EXPECT_EQ(r.run({0x96, 0x42}), 3);
    EXPECT_EQ(r.cpu.a, 0x5A);
    EXPECT_EQ(r.run({0xD6, 0x42}), 3);
    EXPECT_EQ(r.cpu.b, 0x5A);
}

TEST(M6800, ExtendedAddressingIsBigEndian) {
    Rig r;
    r.ram[0x1234] = 0x77;
    EXPECT_EQ(r.run({0xB6, 0x12, 0x34}), 4);
    EXPECT_EQ(r.cpu.a, 0x77);
    EXPECT_EQ(r.cpu.pc, 0x0203);
}

TEST(M6800, IndexedAddsUnsignedOffset) {
    Rig r;
    r.cpu.x = 0x1000;
    r.ram[0x10FF] = 0x99;
    r.ram[0x0F01] = 0x11;
    EXPECT_EQ(r.run({0xA6, 0xFF}), 5);
    EXPECT_EQ(r.cpu.a, 0x99);
    r.cpu.x = 0xFFFF;
    r.ram[0x0000] = 0x31;
    r.run({0xE6, 0x01});
    EXPECT_EQ(r.cpu.b, 0x31);
}

TEST(M6800, StoreAccumulatorModesAndFlags) {
    Rig r;
    r.cpu.a = 0x00;
    r.cpu.cc = FLAG_V | FLAG_C;
    EXPECT_EQ(r.run({0x97, 0x20}), 4);
    EXPECT_EQ(r.ram[0x0020], 0x00);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.b = 0x81;
    r.cpu.x = 0x3000;
    EXPECT_EQ(r.run({0xE7, 0x04}), 6);
    EXPECT_EQ(r.ram[0x3004], 0x81);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_EQ(r.run({0xF7, 0x40, 0x00}), 5);
    EXPECT_EQ(r.ram[0x4000], 0x81);
}

TEST(M6800, LoadIndexAndStackPointer) {
    Rig r;
    EXPECT_EQ(r.run({0xCE, 0x80, 0x00}), 3);
    EXPECT_EQ(r.cpu.x, 0x8000);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    r.ram[0x0030] = 0x00;
    r.ram[0x0031] = 0x00;
    r.cpu.cc |= FLAG_V;
    EXPECT_EQ(r.run({0xDE, 0x30}), 4);
    EXPECT_EQ(r.cpu.x, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_EQ(r.run({0x8E, 0x12, 0x34}), 3);
    EXPECT_EQ(r.cpu.sp, 0x1234);
    r.cpu.x = 0xA55A;
    r.cpu.sp = 0x00FF;
    EXPECT_EQ(r.run({0xFF, 0x30, 0x10}), 6);
    EXPECT_EQ(r.ram[0x3010], 0xA5);
    EXPECT_EQ(r.ram[0x3011], 0x5A);
    EXPECT_EQ(r.run({0x9F, 0x50}), 5);
    EXPECT_EQ(r.ram[0x0050], 0x00);
    EXPECT_EQ(r.ram[0x0051], 0xFF);
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, AddSetsHalfCarryOverflowCarry) {
    Rig r;
    r.cpu.a = 0x0F;
    r.run({0x8B, 0x01});
    EXPECT_EQ(r.cpu.a, 0x10);
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x7F;
    r.run({0x8B, 0x01});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
    r.cpu.a = 0x70;
    r.run({0x8B, 0x10});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_FALSE(r.cpu.flag(FLAG_H));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0xFF;
    r.run({0x8B, 0x01});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
}

TEST(M6800, AdcUsesCarryAndHalfCarry) {
    Rig r;
    r.cpu.b = 0x0E;
    r.cpu.cc = FLAG_C;
    r.run({0xC9, 0x01});
    EXPECT_EQ(r.cpu.b, 0x10);
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    r.cpu.a = 0x80;
    r.cpu.cc = FLAG_C;
    r.run({0x89, 0x7F});
    EXPECT_EQ(r.cpu.a, 0x00);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
}

TEST(M6800, AbaSetsHalfCarry) {
    Rig r;
    r.cpu.a = 0x09;
    r.cpu.b = 0x08;
    EXPECT_EQ(r.run({0x1B}), 2);
    EXPECT_EQ(r.cpu.a, 0x11);
    EXPECT_TRUE(r.cpu.flag(FLAG_H));
    r.cpu.a = 0x40;
    r.cpu.b = 0x40;
    r.run({0x1B});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_FALSE(r.cpu.flag(FLAG_H));
}

TEST(M6800, SubtractBorrowAndOverflow) {
    Rig r;
    r.cpu.a = 0x10;
    r.run({0x80, 0x20});
    EXPECT_EQ(r.cpu.a, 0xF0);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x80;
    r.run({0x80, 0x01});
    EXPECT_EQ(r.cpu.a, 0x7F);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
}

TEST(M6800, SbcSubtractsCarryAndBorrowsOnEqual) {
    Rig r;
    r.cpu.a = 0x05;
    r.cpu.cc = FLAG_C;
    r.run({0x82, 0x05});
    EXPECT_EQ(r.cpu.a, 0xFF);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.b = 0x05;
    r.cpu.cc = 0;
    r.run({0xC2, 0x05});
    EXPECT_EQ(r.cpu.b, 0);
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, CompareLeavesAccumulatorSetsFlags) {
    Rig r;
    r.cpu.a = 0x40;
    r.run({0x81, 0x50});
    EXPECT_EQ(r.cpu.a, 0x40);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    r.run({0x81, 0x40});
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
}

TEST(M6800, SbaAndCba) {
    Rig r;
    r.cpu.a = 0x10;
    r.cpu.b = 0x20;
    EXPECT_EQ(r.run({0x11}), 2);
    EXPECT_EQ(r.cpu.a, 0x10);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_EQ(r.run({0x10}), 2);
    EXPECT_EQ(r.cpu.a, 0xF0);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
}

TEST(M6800, LogicalOpsClearVKeepC) {
    Rig r;
    r.cpu.a = 0xF0;
    r.cpu.cc = FLAG_V | FLAG_C;
    r.run({0x84, 0x0F});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.a = 0x0F;
    r.run({0x8A, 0x80});
    EXPECT_EQ(r.cpu.a, 0x8F);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    r.run({0x88, 0xFF});
    EXPECT_EQ(r.cpu.a, 0x70);
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
}

TEST(M6800, BitTestDoesNotStore) {
    Rig r;
    r.cpu.b = 0x81;
    r.cpu.cc = FLAG_V;
    r.run({0xC5, 0x80});
    EXPECT_EQ(r.cpu.b, 0x81);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.run({0xC5, 0x02});
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, CpxSetsNZVAndNotCarry) {
    Rig r;
    r.cpu.x = 0x1234;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x8C, 0x12, 0x34}), 3);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    r.cpu.cc = FLAG_C;
    r.run({0x8C, 0x12, 0x35});
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_EQ(r.cpu.x, 0x1234);
    r.cpu.cc = 0;
    r.run({0x8C, 0x13, 0x00});
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
}

TEST(M6800, CpxOverflowFromHighByte) {
    Rig r;
    r.cpu.x = 0x8000;
    r.run({0x8C, 0x00, 0x01});
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
    r.cpu.x = 0x0000;
    r.run({0x8C, 0x00, 0x00});
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
}

TEST(M6800, CpxLowByteDifferenceKeepsZClear) {
    Rig r;
    r.cpu.x = 0x1200;
    r.run({0x8C, 0x12, 0x01});
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, CpxIndexedAndDirectCycles) {
    Rig r;
    r.cpu.x = 0x0010;
    r.ram[0x0020] = 0x00;
    r.ram[0x0021] = 0x10;
    EXPECT_EQ(r.run({0x9C, 0x20}), 4);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    r.cpu.x = 0x0010;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0xAC, 0x10}), 6);
    EXPECT_EQ(r.run({0xBC, 0x00, 0x20}), 5);
}

TEST(M6800, NegateEdgeCases) {
    Rig r;
    r.cpu.a = 0x80;
    r.run({0x40});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.a = 0x00;
    r.run({0x40});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    r.cpu.b = 0x01;
    r.run({0x50});
    EXPECT_EQ(r.cpu.b, 0xFF);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
}

TEST(M6800, ComplementSetsCClearsV) {
    Rig r;
    r.cpu.a = 0x55;
    r.cpu.cc = FLAG_V;
    r.run({0x43});
    EXPECT_EQ(r.cpu.a, 0xAA);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
}

TEST(M6800, IncDecOverflowOnlyAtBoundary) {
    Rig r;
    r.cpu.a = 0x7F;
    r.cpu.cc = FLAG_C;
    r.run({0x4C});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.a = 0xFF;
    r.run({0x4C});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    r.cpu.b = 0x80;
    r.run({0x5A});
    EXPECT_EQ(r.cpu.b, 0x7F);
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.b = 0x01;
    r.run({0x5A});
    EXPECT_EQ(r.cpu.b, 0);
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
}

TEST(M6800, ShiftsSetVFromNxorC) {
    Rig r;
    r.cpu.a = 0xC0;
    r.run({0x48});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x40;
    r.run({0x48});
    EXPECT_EQ(r.cpu.a, 0x80);
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x81;
    r.run({0x44});
    EXPECT_EQ(r.cpu.a, 0x40);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x80;
    r.run({0x47});
    EXPECT_EQ(r.cpu.a, 0xC0);
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.b = 0x01;
    r.run({0x57});
    EXPECT_EQ(r.cpu.b, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
}

TEST(M6800, RotatesThroughCarry) {
    Rig r;
    r.cpu.a = 0x80;
    r.cpu.cc = FLAG_C;
    r.run({0x49});
    EXPECT_EQ(r.cpu.a, 0x01);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_FALSE(r.cpu.flag(FLAG_N));
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.cpu.b = 0x01;
    r.cpu.cc = FLAG_C;
    r.run({0x56});
    EXPECT_EQ(r.cpu.b, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.cpu.a = 0x00;
    r.cpu.cc = 0;
    r.run({0x46});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
}

TEST(M6800, MemoryReadModifyWriteModes) {
    Rig r;
    r.ram[0x2000] = 0x81;
    EXPECT_EQ(r.run({0x78, 0x20, 0x00}), 6);
    EXPECT_EQ(r.ram[0x2000], 0x02);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.x = 0x2000;
    r.ram[0x2005] = 0x00;
    EXPECT_EQ(r.run({0x6C, 0x05}), 7);
    EXPECT_EQ(r.ram[0x2005], 0x01);
    EXPECT_EQ(r.run({0x6F, 0x05}), 7);
    EXPECT_EQ(r.ram[0x2005], 0x00);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_EQ(r.run({0x7F, 0x20, 0x00}), 6);
    EXPECT_EQ(r.ram[0x2000], 0x00);
}

TEST(M6800, TestClearsVAndC) {
    Rig r;
    r.cpu.a = 0x80;
    r.cpu.cc = FLAG_V | FLAG_C;
    EXPECT_EQ(r.run({0x4D}), 2);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    r.ram[0x3000] = 0x00;
    r.ram[0x3000 + 1] = 0xAA;
    r.cpu.cc = FLAG_C;
    EXPECT_EQ(r.run({0x7D, 0x30, 0x00}), 6);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    EXPECT_EQ(r.ram[0x3000], 0x00);
}

TEST(M6800, ClearSetsZClearsRest) {
    Rig r;
    r.cpu.a = 0xFF;
    r.cpu.cc = FLAG_N | FLAG_V | FLAG_C;
    r.run({0x4F});
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_EQ(r.cpu.cc, FLAG_Z);
}

TEST(M6800, DaaTableCases) {
    struct Case { uint8_t a; uint8_t cc; uint8_t want; bool c; };
    const Case cases[] = {
        {0x2B, 0,                 0x31, false},
        {0x1A, 0,                 0x20, false},
        {0x2F, FLAG_H,            0x35, false},
        {0xA3, 0,                 0x03, true},
        {0x9B, 0,                 0x01, true},
        {0xA0, FLAG_H,            0x06, true},
        {0x11, FLAG_C,            0x71, true},
        {0x1C, FLAG_C,            0x82, true},
        {0x30, FLAG_C | FLAG_H,   0x96, true},
        {0x45, 0,                 0x45, false},
    };
    for (const Case& c : cases) {
        Rig r;
        r.cpu.a = c.a;
        r.cpu.cc = c.cc;
        EXPECT_EQ(r.run({0x19}), 2);
        EXPECT_EQ(r.cpu.a, c.want) << std::hex << int(c.a);
        EXPECT_EQ(r.cpu.flag(FLAG_C), c.c) << std::hex << int(c.a);
        EXPECT_EQ(r.cpu.flag(FLAG_Z), c.want == 0);
        EXPECT_EQ(r.cpu.flag(FLAG_N), (c.want & 0x80) != 0);
    }
}

TEST(M6800, DaaLeavesVUnchanged) {
    Rig r;
    r.cpu.a = 0x2B;
    r.cpu.cc = FLAG_V;
    r.run({0x19});
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
}

TEST(M6800, DaaAfterBcdAddition) {
    Rig r;
    r.cpu.a = 0x19;
    r.cpu.b = 0x28;
    r.run({0x1B});
    r.run({0x19});
    EXPECT_EQ(r.cpu.a, 0x47);
    r.cpu.a = 0x99;
    r.cpu.b = 0x01;
    r.run({0x1B});
    r.run({0x19});
    EXPECT_EQ(r.cpu.a, 0x00);
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, TransferAccumulators) {
    Rig r;
    r.cpu.a = 0x80;
    r.cpu.cc = FLAG_V | FLAG_C;
    EXPECT_EQ(r.run({0x16}), 2);
    EXPECT_EQ(r.cpu.b, 0x80);
    EXPECT_TRUE(r.cpu.flag(FLAG_N));
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.cpu.b = 0x00;
    EXPECT_EQ(r.run({0x17}), 2);
    EXPECT_EQ(r.cpu.a, 0);
    EXPECT_TRUE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, TapAndTpa) {
    Rig r;
    r.cpu.a = 0xFF;
    r.run({0x06});
    EXPECT_EQ(r.cpu.cc, 0x3F);
    r.cpu.a = 0x00;
    r.run({0x07});
    EXPECT_EQ(r.cpu.a, 0xFF);
    r.cpu.a = 0x00;
    r.run({0x06});
    r.run({0x07});
    EXPECT_EQ(r.cpu.a, 0xC0);
}

TEST(M6800, ConditionCodeInstructions) {
    Rig r;
    r.run({0x0D});
    EXPECT_TRUE(r.cpu.flag(FLAG_C));
    r.run({0x0C});
    EXPECT_FALSE(r.cpu.flag(FLAG_C));
    r.run({0x0B});
    EXPECT_TRUE(r.cpu.flag(FLAG_V));
    r.run({0x0A});
    EXPECT_FALSE(r.cpu.flag(FLAG_V));
    r.run({0x0F});
    EXPECT_TRUE(r.cpu.flag(FLAG_I));
    r.run({0x0E});
    EXPECT_FALSE(r.cpu.flag(FLAG_I));
}

TEST(M6800, IndexOpsAffectOnlyZ) {
    Rig r;
    r.cpu.x = 0xFFFF;
    r.cpu.cc = FLAG_N | FLAG_V | FLAG_C;
    EXPECT_EQ(r.run({0x08}), 4);
    EXPECT_EQ(r.cpu.x, 0);
    EXPECT_EQ(r.cpu.cc, FLAG_N | FLAG_V | FLAG_C | FLAG_Z);
    EXPECT_EQ(r.run({0x09}), 4);
    EXPECT_EQ(r.cpu.x, 0xFFFF);
    EXPECT_EQ(r.cpu.cc, FLAG_N | FLAG_V | FLAG_C);
    r.cpu.x = 0x0100;
    r.run({0x09});
    EXPECT_EQ(r.cpu.x, 0x00FF);
    EXPECT_FALSE(r.cpu.flag(FLAG_Z));
}

TEST(M6800, StackPointerOpsLeaveFlags) {
    Rig r;
    r.cpu.cc = FLAG_N | FLAG_Z;
    r.cpu.sp = 0x0100;
    EXPECT_EQ(r.run({0x31}), 4);
    EXPECT_EQ(r.cpu.sp, 0x0101);
    EXPECT_EQ(r.run({0x34}), 4);
    EXPECT_EQ(r.cpu.sp, 0x0100);
    EXPECT_EQ(r.cpu.cc, FLAG_N | FLAG_Z);
}

TEST(M6800, TsxPlusOneTxsMinusOne) {
    Rig r;
    r.cpu.sp = 0x01FF;
    r.cpu.cc = FLAG_Z;
    EXPECT_EQ(r.run({0x30}), 4);
    EXPECT_EQ(r.cpu.x, 0x0200);
    EXPECT_EQ(r.cpu.sp, 0x01FF);
    EXPECT_EQ(r.cpu.cc, FLAG_Z);
    r.cpu.x = 0x0300;
    EXPECT_EQ(r.run({0x35}), 4);
    EXPECT_EQ(r.cpu.sp, 0x02FF);
    EXPECT_EQ(r.cpu.x, 0x0300);
    r.cpu.x = 0;
    r.run({0x35});
    EXPECT_EQ(r.cpu.sp, 0xFFFF);
}

TEST(M6800, PushPullAccumulators) {
    Rig r;
    r.cpu.a = 0x11;
    r.cpu.b = 0x22;
    EXPECT_EQ(r.run({0x36}), 4);
    EXPECT_EQ(r.run({0x37}), 4);
    EXPECT_EQ(r.cpu.sp, 0x01FD);
    EXPECT_EQ(r.ram[0x01FF], 0x11);
    EXPECT_EQ(r.ram[0x01FE], 0x22);
    r.cpu.a = r.cpu.b = 0;
    EXPECT_EQ(r.run({0x33}), 4);
    EXPECT_EQ(r.run({0x32}), 4);
    EXPECT_EQ(r.cpu.b, 0x22);
    EXPECT_EQ(r.cpu.a, 0x11);
    EXPECT_EQ(r.cpu.sp, 0x01FF);
}

TEST(M6800, JmpExtendedAndIndexed) {
    Rig r;
    EXPECT_EQ(r.run({0x7E, 0x40, 0x00}), 3);
    EXPECT_EQ(r.cpu.pc, 0x4000);
    r.cpu.x = 0x5000;
    EXPECT_EQ(r.run({0x6E, 0x10}), 4);
    EXPECT_EQ(r.cpu.pc, 0x5010);
}

TEST(M6800, JsrExtendedPushesReturnLowFirst) {
    Rig r;
    EXPECT_EQ(r.run({0xBD, 0x30, 0x00}), 9);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_EQ(r.cpu.sp, 0x01FD);
    EXPECT_EQ(r.ram[0x01FF], 0x03);
    EXPECT_EQ(r.ram[0x01FE], 0x02);
}

TEST(M6800, JsrIndexedAndRts) {
    Rig r;
    r.cpu.x = 0x4000;
    EXPECT_EQ(r.run({0xAD, 0x08}), 8);
    EXPECT_EQ(r.cpu.pc, 0x4008);
    EXPECT_EQ(r.ram[0x01FF], 0x02);
    EXPECT_EQ(r.ram[0x01FE], 0x02);
    r.ram[0x4008] = 0x39;
    EXPECT_EQ(r.cpu.step(), 5);
    EXPECT_EQ(r.cpu.pc, 0x0202);
    EXPECT_EQ(r.cpu.sp, 0x01FF);
}

TEST(M6800, BsrPushesAndBranches) {
    Rig r;
    EXPECT_EQ(r.run({0x8D, 0x10}), 8);
    EXPECT_EQ(r.cpu.pc, 0x0212);
    EXPECT_EQ(r.ram[0x01FF], 0x02);
    EXPECT_EQ(r.ram[0x01FE], 0x02);
    r.run({0x8D, 0xFE});
    EXPECT_EQ(r.cpu.pc, 0x0200);
}

TEST(M6800, BranchesCostFourTakenOrNot) {
    Rig r;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x26, 0x10}), 4);
    EXPECT_EQ(r.cpu.pc, 0x0212);
    r.cpu.cc = FLAG_Z;
    EXPECT_EQ(r.run({0x26, 0x10}), 4);
    EXPECT_EQ(r.cpu.pc, 0x0202);
}

TEST(M6800, BackwardBranchOffset) {
    Rig r;
    r.run({0x20, 0xFE});
    EXPECT_EQ(r.cpu.pc, 0x0200);
    r.run({0x20, 0x80});
    EXPECT_EQ(r.cpu.pc, 0x0182);
    r.run({0x20, 0x7F});
    EXPECT_EQ(r.cpu.pc, 0x0281);
}

TEST(M6800, BranchConditionTruthTable) {
    struct Case { uint8_t op; uint8_t cc; bool taken; };
    const Case cases[] = {
        {0x20, 0, true},
        {0x22, 0, true},               {0x22, FLAG_C, false},        {0x22, FLAG_Z, false},
        {0x23, 0, false},              {0x23, FLAG_C, true},         {0x23, FLAG_Z, true},
        {0x24, 0, true},               {0x24, FLAG_C, false},
        {0x25, 0, false},              {0x25, FLAG_C, true},
        {0x26, 0, true},               {0x26, FLAG_Z, false},
        {0x27, 0, false},              {0x27, FLAG_Z, true},
        {0x28, 0, true},               {0x28, FLAG_V, false},
        {0x29, 0, false},              {0x29, FLAG_V, true},
        {0x2A, 0, true},               {0x2A, FLAG_N, false},
        {0x2B, 0, false},              {0x2B, FLAG_N, true},
        {0x2C, 0, true},               {0x2C, FLAG_N | FLAG_V, true},
        {0x2C, FLAG_N, false},         {0x2C, FLAG_V, false},
        {0x2D, 0, false},              {0x2D, FLAG_N | FLAG_V, false},
        {0x2D, FLAG_N, true},          {0x2D, FLAG_V, true},
        {0x2E, 0, true},               {0x2E, FLAG_Z, false},        {0x2E, FLAG_N, false},
        {0x2E, FLAG_N | FLAG_V, true}, {0x2E, FLAG_V, false},
        {0x2F, 0, false},              {0x2F, FLAG_Z, true},         {0x2F, FLAG_N, true},
        {0x2F, FLAG_V, true},          {0x2F, FLAG_N | FLAG_V, false},
    };
    for (const Case& c : cases) {
        Rig r;
        r.cpu.cc = c.cc;
        r.run({c.op, 0x10});
        EXPECT_EQ(r.cpu.pc, c.taken ? 0x0212 : 0x0202) << std::hex << int(c.op) << " cc=" << int(c.cc);
    }
}

TEST(M6800, Opcode21IsNotBrn) {
    Rig r;
    r.run({0x21, 0x10});
    EXPECT_EQ(r.cpu.pc, 0x0201);
    EXPECT_EQ(r.cpu.undefined_ops, 1u);
}

TEST(M6800, UndefinedOpcodesCountAndTakeTwoCycles) {
    Rig r;
    EXPECT_EQ(r.run({0x02}), 2);
    EXPECT_EQ(r.run({0x87, 0x55}), 2);
    EXPECT_EQ(r.cpu.undefined_ops, 2u);
    EXPECT_EQ(r.cpu.pc, 0x0201);
}

TEST(M6800, IrqIgnoredWhileMasked) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.cpu.cc = FLAG_I;
    r.cpu.set_irq(true);
    EXPECT_EQ(r.run({0x01}), 2);
    EXPECT_EQ(r.cpu.pc, 0x0201);
}

TEST(M6800, IrqStacksSevenBytesAndVectors) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.cpu.a = 0xAA;
    r.cpu.b = 0xBB;
    r.cpu.x = 0x1234;
    r.cpu.cc = FLAG_C | FLAG_N;
    r.cpu.pc = 0x0300;
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 12);
    EXPECT_EQ(r.cpu.pc, 0x1000);
    EXPECT_EQ(r.cpu.sp, 0x01F8);
    EXPECT_TRUE(r.cpu.flag(FLAG_I));
    EXPECT_EQ(r.ram[0x01FF], 0x00);
    EXPECT_EQ(r.ram[0x01FE], 0x03);
    EXPECT_EQ(r.ram[0x01FD], 0x34);
    EXPECT_EQ(r.ram[0x01FC], 0x12);
    EXPECT_EQ(r.ram[0x01FB], 0xAA);
    EXPECT_EQ(r.ram[0x01FA], 0xBB);
    EXPECT_EQ(r.ram[0x01F9], 0xC0 | FLAG_C | FLAG_N);
    EXPECT_EQ(r.cpu.cycles, 12u);
}

TEST(M6800, RtiRestoresEverything) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.ram[0x1000] = 0x3B;
    r.cpu.a = 0xAA;
    r.cpu.b = 0xBB;
    r.cpu.x = 0x1234;
    r.cpu.cc = FLAG_H | FLAG_Z;
    r.cpu.pc = 0x0300;
    r.cpu.set_irq(true);
    r.cpu.step();
    r.cpu.set_irq(false);
    r.cpu.a = r.cpu.b = 0;
    r.cpu.x = 0;
    EXPECT_EQ(r.cpu.step(), 10);
    EXPECT_EQ(r.cpu.pc, 0x0300);
    EXPECT_EQ(r.cpu.sp, 0x01FF);
    EXPECT_EQ(r.cpu.a, 0xAA);
    EXPECT_EQ(r.cpu.b, 0xBB);
    EXPECT_EQ(r.cpu.x, 0x1234);
    EXPECT_EQ(r.cpu.cc, FLAG_H | FLAG_Z);
}

TEST(M6800, IrqLevelRetriggersAfterRti) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.ram[0x1000] = 0x3B;
    r.cpu.set_irq(true);
    r.cpu.pc = 0x0300;
    EXPECT_EQ(r.cpu.step(), 12);
    EXPECT_EQ(r.cpu.step(), 10);
    EXPECT_EQ(r.cpu.step(), 12);
}

TEST(M6800, CliAllowsPendingIrqAfterNextBoundary) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.cpu.cc = FLAG_I;
    r.cpu.set_irq(true);
    EXPECT_EQ(r.run({0x0E}), 2);
    EXPECT_EQ(r.cpu.pc, 0x0201);
    EXPECT_EQ(r.cpu.step(), 12);
    EXPECT_EQ(r.cpu.pc, 0x1000);
}

TEST(M6800, NmiIsEdgeTriggeredAndUnmaskable) {
    Rig r;
    r.vec(0xFFFC, 0x2000);
    r.ram[0x2000] = 0x01;
    r.cpu.cc = FLAG_I;
    r.cpu.set_nmi(true);
    EXPECT_EQ(r.cpu.step(), 12);
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.cpu.step(), 2);
    EXPECT_EQ(r.cpu.pc, 0x2001);
    r.cpu.set_nmi(true);
    EXPECT_EQ(r.cpu.step(), 2);
    r.cpu.set_nmi(false);
    r.cpu.set_nmi(true);
    EXPECT_EQ(r.cpu.step(), 12);
    EXPECT_EQ(r.cpu.pc, 0x2000);
}

TEST(M6800, NmiBeatsIrq) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.vec(0xFFFC, 0x2000);
    r.cpu.set_irq(true);
    r.cpu.set_nmi(true);
    r.cpu.step();
    EXPECT_EQ(r.cpu.pc, 0x2000);
}

TEST(M6800, SwiStacksPcPlusOneAndVectors) {
    Rig r;
    r.vec(0xFFFA, 0x3000);
    r.cpu.a = 0x12;
    r.cpu.cc = FLAG_Z;
    EXPECT_EQ(r.run({0x3F}), 12);
    EXPECT_EQ(r.cpu.pc, 0x3000);
    EXPECT_TRUE(r.cpu.flag(FLAG_I));
    EXPECT_EQ(r.cpu.sp, 0x01F8);
    EXPECT_EQ(r.ram[0x01FF], 0x01);
    EXPECT_EQ(r.ram[0x01FE], 0x02);
    EXPECT_EQ(r.ram[0x01FB], 0x12);
    EXPECT_EQ(r.ram[0x01F9], 0xC0 | FLAG_Z);
}

TEST(M6800, SwiThenRtiRoundTrip) {
    Rig r;
    r.vec(0xFFFA, 0x3000);
    r.ram[0x3000] = 0x3B;
    r.cpu.cc = FLAG_C;
    r.run({0x3F});
    EXPECT_EQ(r.cpu.step(), 10);
    EXPECT_EQ(r.cpu.pc, 0x0201);
    EXPECT_EQ(r.cpu.cc, FLAG_C);
    EXPECT_EQ(r.cpu.sp, 0x01FF);
}

TEST(M6800, RtiClearsMaskWhenStackedBitIsZero) {
    Rig r;
    r.cpu.sp = 0x01F8;
    r.ram[0x01F9] = 0xC0;
    r.cpu.cc = FLAG_I;
    r.run({0x3B});
    EXPECT_FALSE(r.cpu.flag(FLAG_I));
}

TEST(M6800, WaiStacksThenIdlesOneCyclePerStep) {
    Rig r;
    r.cpu.a = 0x55;
    r.cpu.cc = 0;
    EXPECT_EQ(r.run({0x3E}), 9);
    EXPECT_TRUE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.sp, 0x01F8);
    EXPECT_EQ(r.ram[0x01FF], 0x01);
    EXPECT_EQ(r.ram[0x01FE], 0x02);
    EXPECT_EQ(r.ram[0x01FB], 0x55);
    EXPECT_EQ(r.cpu.step(), 1);
    EXPECT_EQ(r.cpu.step(), 1);
    EXPECT_TRUE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0x0201);
}

TEST(M6800, WaiWokenByIrqWithoutRestacking) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.ram[0x1000] = 0x3B;
    r.run({0x3E, 0x01});
    r.cpu.step();
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 4);
    EXPECT_FALSE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0x1000);
    EXPECT_EQ(r.cpu.sp, 0x01F8);
    EXPECT_TRUE(r.cpu.flag(FLAG_I));
    r.cpu.set_irq(false);
    EXPECT_EQ(r.cpu.step(), 10);
    EXPECT_EQ(r.cpu.pc, 0x0201);
    EXPECT_FALSE(r.cpu.flag(FLAG_I));
    EXPECT_EQ(r.cpu.sp, 0x01FF);
}

TEST(M6800, WaiWithMaskedIrqOnlyNmiWakes) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.vec(0xFFFC, 0x2000);
    r.cpu.cc = FLAG_I;
    r.run({0x3E});
    r.cpu.set_irq(true);
    EXPECT_EQ(r.cpu.step(), 1);
    EXPECT_TRUE(r.cpu.waiting());
    r.cpu.set_nmi(true);
    EXPECT_EQ(r.cpu.step(), 4);
    EXPECT_FALSE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0x2000);
    EXPECT_EQ(r.cpu.sp, 0x01F8);
}

TEST(M6800, WaiWithIrqAssertedAfterwardWakesNextStep) {
    Rig r;
    r.vec(0xFFF8, 0x1000);
    r.cpu.cc = 0;
    r.run({0x3E});
    r.cpu.set_irq(true);
    EXPECT_TRUE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.step(), 4);
    EXPECT_EQ(r.cpu.pc, 0x1000);
}

TEST(M6800, ResetClearsWaiting) {
    Rig r;
    r.vec(0xFFFE, 0xF000);
    r.run({0x3E});
    r.cpu.reset();
    EXPECT_FALSE(r.cpu.waiting());
    EXPECT_EQ(r.cpu.pc, 0xF000);
}

TEST(M6800, StackWrapsAtPageBoundaryOfAddressSpace) {
    Rig r;
    r.cpu.sp = 0x0000;
    r.cpu.a = 0x9C;
    r.run({0x36});
    EXPECT_EQ(r.ram[0x0000], 0x9C);
    EXPECT_EQ(r.cpu.sp, 0xFFFF);
    r.run({0x32});
    EXPECT_EQ(r.cpu.sp, 0x0000);
    EXPECT_EQ(r.cpu.a, 0x9C);
}

TEST(M6800, CyclesAccumulate) {
    Rig r;
    r.prog({0x01, 0x01, 0x08, 0x7E, 0x02, 0x00});
    r.cpu.step();
    r.cpu.step();
    r.cpu.step();
    r.cpu.step();
    EXPECT_EQ(r.cpu.cycles, 2u + 2u + 4u + 3u);
}
