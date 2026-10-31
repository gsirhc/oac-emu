#include <gtest/gtest.h>

#include "williams/blitter.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using williams::Blitter;

struct Bench {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x10000, 0);
    Blitter b;

    Bench() {
        b.read = [this](uint16_t a) { return mem[a]; };
        b.write = [this](uint16_t a, uint8_t v, uint8_t k) { mem[a] = uint8_t((mem[a] & k) | (v & ~k)); };
    }

    int blit(uint8_t ctl, uint16_t src, uint16_t dst, int w, int h, uint8_t solid = 0) {
        b.write_reg(1, solid);
        b.write_reg(2, uint8_t(src >> 8));
        b.write_reg(3, uint8_t(src));
        b.write_reg(4, uint8_t(dst >> 8));
        b.write_reg(5, uint8_t(dst));
        b.write_reg(6, uint8_t(w ^ 4));
        b.write_reg(7, uint8_t(h ^ 4));
        return b.start(ctl);
    }
};

TEST(Blitter, LinearCopyCountsWidthTimesHeight) {
    Bench t;
    for (int i = 0; i < 16; i++) t.mem[0x8000 + i] = uint8_t(0x10 + i);
    const int c = t.blit(0x00, 0x8000, 0x1000, 4, 4);
    for (int i = 0; i < 16; i++) EXPECT_EQ(t.mem[0x1000 + i], 0x10 + i);
    EXPECT_EQ(t.mem[0x1010], 0);
    EXPECT_EQ(c, 16 + 5);
}

TEST(Blitter, Sc1InvertsBit2OfWidthAndHeight) {
    Bench t;
    t.mem[0x8000] = 0x77;
    t.mem[0x8001] = 0x88;
    t.b.write_reg(2, 0x80);
    t.b.write_reg(3, 0x00);
    t.b.write_reg(4, 0x10);
    t.b.write_reg(5, 0x00);
    t.b.write_reg(6, 0x05);
    t.b.write_reg(7, 0x04);
    t.b.start(0x00);
    EXPECT_EQ(t.mem[0x1000], 0x77);
    EXPECT_EQ(t.mem[0x1001], 0x00);
    t.b.sc1 = false;
    t.b.start(0x00);
    EXPECT_EQ(t.mem[0x1001], 0x88);
}

TEST(Blitter, ZeroSizeActsAsOne) {
    Bench t;
    t.mem[0x8000] = 0x42;
    t.blit(0x00, 0x8000, 0x1000, 0, 0);
    EXPECT_EQ(t.mem[0x1000], 0x42);
    EXPECT_EQ(t.mem[0x1001], 0x00);
}

TEST(Blitter, ScreenDestinationStepsColumnsThenRows) {
    Bench t;
    for (int i = 0; i < 6; i++) t.mem[0x8000 + i] = uint8_t(1 + i);
    t.blit(0x02, 0x8000, 0x2010, 3, 2);
    EXPECT_EQ(t.mem[0x2010], 1);
    EXPECT_EQ(t.mem[0x2110], 2);
    EXPECT_EQ(t.mem[0x2210], 3);
    EXPECT_EQ(t.mem[0x2011], 4);
    EXPECT_EQ(t.mem[0x2211], 6);
}

TEST(Blitter, SolidReplacesSourceData) {
    Bench t;
    t.mem[0x8000] = 0x12;
    t.blit(0x10, 0x8000, 0x1000, 1, 1, 0xAB);
    EXPECT_EQ(t.mem[0x1000], 0xAB);
}

TEST(Blitter, ForeOnlyInhibitsZeroPixels) {
    Bench t;
    t.mem[0x8000] = 0x30;
    t.mem[0x1000] = 0xCD;
    t.blit(0x08, 0x8000, 0x1000, 1, 1);
    EXPECT_EQ(t.mem[0x1000], 0x3D);
}

TEST(Blitter, SuppressBitsInhibitNibbles) {
    Bench t;
    t.mem[0x8000] = 0x12;
    t.mem[0x1000] = 0xFF;
    t.blit(0x40, 0x8000, 0x1000, 1, 1);
    EXPECT_EQ(t.mem[0x1000], 0x1F);
    t.mem[0x1000] = 0xFF;
    t.blit(0x80, 0x8000, 0x1000, 1, 1);
    EXPECT_EQ(t.mem[0x1000], 0xF2);
}

// Riddle: FORE ONLY still writes a suppressed nibble when its source pixel is zero.
TEST(Blitter, ForeOnlyXorsWithSuppress) {
    Bench t;
    t.mem[0x8000] = 0x05;
    t.mem[0x1000] = 0xFF;
    t.blit(0x88, 0x8000, 0x1000, 1, 1);
    EXPECT_EQ(t.mem[0x1000], 0x05);
}

TEST(Blitter, ShiftCarriesAcrossRows) {
    Bench t;
    t.mem[0x8000] = 0x12;
    t.mem[0x8001] = 0x34;
    t.blit(0x20, 0x8000, 0x1000, 1, 2);
    EXPECT_EQ(t.mem[0x1000], 0x01);
    EXPECT_EQ(t.mem[0x1001], 0x23);
}

TEST(Blitter, SlowDoublesTheHalt) {
    Bench t;
    EXPECT_EQ(t.blit(0x04, 0x8000, 0x1000, 4, 4), 32 + 5);
}

TEST(Blitter, RegistersReadBack) {
    Bench t;
    t.b.write_reg(5, 0x9A);
    EXPECT_EQ(t.b.reg(5), 0x9A);
}

// Sean Riddle's 256 blits on real Joust boards, each run 1000 times, RAM dumped after
// (blitdump.bin, `make -C joust riddle`). His board's bank ROMs read back as the
// zero-filled RAM, so the bank is left unpopulated here.
TEST(Riddle, HardwareDumpsMatch) {
    const char* env = std::getenv("JOUST_BLITDUMP");
    const std::string path = env ? env : std::string(JOUST_RIDDLE_DIR) + "/blitdump.bin";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) GTEST_SKIP() << "no " << path << " (make -C joust riddle)";
    std::vector<uint8_t> dump(256 * 49168);
    const size_t got = std::fread(dump.data(), 1, dump.size(), f);
    std::fclose(f);
    ASSERT_EQ(got, dump.size());

    int checked = 0;
    for (int i = 0; i < 256; i++) {
        const size_t base = size_t(i) * 49168;
        auto h = [&](int k) { return dump[base + size_t(k)]; };
        if (h(0) == 0xFF) continue;
        std::vector<uint8_t> ram(0xC000);
        for (int a = 0; a < 0xC000; a++)
            ram[a] = a < 0x3000 ? 0x00 : a < 0x6000 ? 0xFF : a < 0x9000 ? 0xA5 : 0x5A;
        Blitter b;
        // His source table at $FCF5 counts 150..255 then 0..149.
        b.read = [&](uint16_t a) -> uint8_t {
            if (a < 0xC000) return ram[a];
            if (a >= 0xFCF5 && a < 0xFCF5 + 256) {
                const int k = a - 0xFCF5;
                return uint8_t(k < 106 ? 150 + k : k - 106);
            }
            return 0xFF;
        };
        b.write = [&](uint16_t a, uint8_t v, uint8_t k) {
            if (a < 0xC000) ram[a] = uint8_t((ram[a] & k) | (v & ~k));
        };
        b.write_reg(1, h(5));
        b.write_reg(2, h(3));
        b.write_reg(3, h(4));
        b.write_reg(4, h(6));
        b.write_reg(5, h(7));
        b.write_reg(6, h(8));
        b.write_reg(7, h(9));
        for (int k = 0; k < 1000; k++) b.start(h(10));
        int diff = 0;
        for (int a = 0; a < 0xC000; a++) diff += ram[size_t(a)] != h(16 + a);
        EXPECT_EQ(diff, 0) << "blit " << i << " control $" << std::hex << int(h(10));
        checked++;
    }
    EXPECT_EQ(checked, 255);
}

}  // namespace
