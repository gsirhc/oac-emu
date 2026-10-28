#include <gtest/gtest.h>

#include "wd1003.h"

#include <cmath>
#include <vector>

namespace {

using ibmpcat::Wd1003;

std::vector<uint8_t> MakeImage(long cyl, int heads, int spt) {
    std::vector<uint8_t> img(std::size_t(cyl) * heads * spt * 512, 0);
    for (std::size_t i = 0; i < img.size(); i += 512) img[i] = uint8_t((i / 512) & 0xFF);
    return img;
}

class Wd1003Test : public ::testing::Test {
protected:
    Wd1003 hdd;
    void SetUp() override { hdd.reset(); }
};

TEST_F(Wd1003Test, TaskFileRegistersReadBack) {
    hdd.out(0x1F6, 0xA0);
    hdd.out(0x1F2, 0x55);
    hdd.out(0x1F3, 0xAA);
    EXPECT_EQ(hdd.in(0x1F2), 0x55);
    EXPECT_EQ(hdd.in(0x1F3), 0xAA);
}

TEST_F(Wd1003Test, SoftResetLeavesTheNoErrorDiagnosticCode) {
    hdd.out(0x1F2, 0x55);
    hdd.out(0x1F3, 0xAA);
    hdd.out(0x3F6, 0x04);  // assert SRST
    hdd.out(0x3F6, 0x00);
    EXPECT_EQ(hdd.in(0x1F1), 0x01);  // IBM PC/AT Technical Reference, DISK_RESET
    EXPECT_EQ(hdd.in(0x1F2), 0x01);
    EXPECT_EQ(hdd.in(0x1F3), 0x01);
    EXPECT_EQ(hdd.in(0x1F4), 0x00);
    EXPECT_EQ(hdd.in(0x1F5), 0x00);
    EXPECT_TRUE(hdd.in(0x3F6) & 0x40);
}

TEST_F(Wd1003Test, ReadSectorsAfterPacingReturnsRealBytes) {
    auto img = MakeImage(733, 5, 17);
    hdd.mount(0, img.data(), img.size());
    hdd.out(0x1F6, 0xA0);       // drive 0, head 0
    hdd.out(0x1F2, 1);          // 1 sector
    hdd.out(0x1F3, 3);          // sector 3 (1-based)
    hdd.out(0x1F4, 0); hdd.out(0x1F5, 0);  // cylinder 0
    hdd.out(0x1F7, 0x20);       // READ SECTORS

    EXPECT_TRUE(hdd.in(0x1F7) & 0x80);  // BSY while the paced transfer is in flight
    for (uint64_t c = 0; c < 10'000'000 && (hdd.in(0x3F6) & 0x80); c += 100) hdd.tick(c);
    // Poll alternate status: reading it does not clear the pending interrupt.
    ASSERT_TRUE(hdd.in(0x3F6) & 0x08);  // DRQ: data ready
    EXPECT_TRUE(hdd.irq_pending());

    // Sector 3 (1-based) is the 3rd 512-byte block -> stamped with index 2.
    EXPECT_EQ(hdd.data_in16() & 0xFF, 2);
}

TEST_F(Wd1003Test, RestoreAtTrackZeroAndSetParametersCompleteAtOnce) {
    hdd.out(0x1F7, 0x10);  // RECALIBRATE
    hdd.tick(100);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_TRUE(hdd.in(0x1F7) & 0x40);  // DRDY

    hdd.out(0x1F2, 17);   // sectors per track
    hdd.out(0x1F6, 0xA0 | 0x04);  // heads-1 = 4 (5 heads)
    hdd.out(0x1F7, 0x91);  // INITIALIZE DEVICE PARAMETERS
    EXPECT_TRUE(hdd.irq_pending());
}

TEST_F(Wd1003Test, CommandsToTheMissingDrive1Abort) {
    auto img = MakeImage(733, 5, 17);
    hdd.mount(0, img.data(), img.size());
    hdd.out(0x1F6, 0xB0);
    hdd.out(0x1F2, 1);
    hdd.out(0x1F3, 1);
    hdd.out(0x1F7, 0x20);
    EXPECT_TRUE(hdd.irq_pending());
    uint8_t st = hdd.in(0x1F7);
    EXPECT_EQ(st & 0x01, 0x01);
    EXPECT_EQ(st & 0x40, 0x00) << "drive 1 isn't ready";
    EXPECT_EQ(hdd.in(0x1F1), 0x04);
    EXPECT_FALSE(st & 0x08);

    hdd.out(0x1F6, 0xA0);
    hdd.out(0x1F7, 0x10);
    hdd.tick(100);
    EXPECT_EQ(hdd.in(0x1F7) & 0x41, 0x40);
}

TEST_F(Wd1003Test, BusyReflectsAnInFlightTransfer) {
    // Activity LED helper (wd1003.h).
    auto img = MakeImage(733, 5, 17);
    hdd.mount(0, img.data(), img.size());
    hdd.out(0x1F6, 0xA0);
    hdd.out(0x1F2, 1);
    hdd.out(0x1F3, 3);
    hdd.out(0x1F4, 0); hdd.out(0x1F5, 0);
    EXPECT_FALSE(hdd.busy());  // idle before the command is even issued
    hdd.out(0x1F7, 0x20);      // READ SECTORS
    EXPECT_TRUE(hdd.busy());
    for (uint64_t c = 0; c < 10'000'000 && hdd.busy(); c += 100) hdd.tick(c);
    EXPECT_FALSE(hdd.busy());  // transfer completed
}

TEST_F(Wd1003Test, MountedMediaSurvivesControllerReset) {
    auto img = MakeImage(733, 5, 17);
    hdd.mount(0, img.data(), img.size());
    hdd.reset();
    EXPECT_TRUE(hdd.drives[0].present);
}

class Wd1003CommandTest : public Wd1003Test {
protected:
    std::vector<uint8_t> img = MakeImage(733, 5, 17);
    uint64_t now = 0;
    void SetUp() override {
        Wd1003Test::SetUp();
        hdd.mount(0, img.data(), img.size());
        Chs(0, 0, 1, 1);
        hdd.out(0x1F7, 0x70);  // sets the step rate; no move, so done at cycle 0
        hdd.tick(0);
        hdd.in(0x1F7);
    }
    // SEEK at 35 us steps, which also sets the implied-seek rate
    uint64_t SeekTo(int cyl, uint8_t cmd = 0x70) {
        Chs(cyl, 0, 1, 1);
        hdd.out(0x1F7, cmd);
        uint64_t took = RunWhileBusy();
        hdd.in(0x1F7);
        return took;
    }
    static double Ms(double ms) { return ms * 8000.0; }
    void Chs(int cyl, int head, int sector, int count) {
        hdd.out(0x1F6, uint8_t(0xA0 | head));
        hdd.out(0x1F2, uint8_t(count));
        hdd.out(0x1F3, uint8_t(sector));
        hdd.out(0x1F4, uint8_t(cyl & 0xFF));
        hdd.out(0x1F5, uint8_t(cyl >> 8));
    }
    static constexpr double kRev = 8e6 / 60.0;
    static constexpr double kSlot = kRev / 17.0;
    void RunTo(uint64_t c) { while (now < c) hdd.tick(now += 100); }
    uint64_t RunWhileBusy() {
        uint64_t start = now;
        while ((hdd.in(0x3F6) & 0x80) && now - start < 100'000'000) hdd.tick(now += 100);
        return now - start;
    }
    void WriteWhileDrq(uint16_t w) {
        while (hdd.in(0x3F6) & 0x08) {
            for (int i = 0; i < 256; ++i) hdd.data_out16(w);
            RunWhileBusy();
        }
    }
    void Format(int cyl, int head, const uint8_t (&table)[34]) {
        Chs(cyl, head, 1, 17);
        hdd.out(0x1F7, 0x50);
        for (int i = 0; i < 256; ++i) {
            uint16_t w = i < 17 ? uint16_t(table[i * 2] | (table[i * 2 + 1] << 8)) : 0;
            hdd.data_out16(w);
        }
        RunWhileBusy();
        hdd.in(0x1F7);
    }
};

TEST_F(Wd1003CommandTest, SeekHoldsBsyThenInterrupts) {
    Chs(400, 0, 1, 1);
    hdd.out(0x1F7, 0x7F);
    EXPECT_TRUE(hdd.in(0x3F6) & 0x80);
    EXPECT_FALSE(hdd.irq_pending());
    EXPECT_NEAR(double(RunWhileBusy()), Ms(55.0), 200.0);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F7) & 0xD1, 0x50);
}

TEST_F(Wd1003CommandTest, BufferedSeeksMeetTheSt4038Maximums) {
    EXPECT_NEAR(double(SeekTo(1)), Ms(11.0), 200.0);
    EXPECT_NEAR(double(SeekTo(244)), Ms(40.0), 200.0);
    EXPECT_NEAR(double(SeekTo(1)), Ms(40.0), 200.0) << "outward the same";
    EXPECT_NEAR(double(SeekTo(733)), Ms(85.0), 200.0);
    EXPECT_EQ(SeekTo(733), 100u) << "no move, no wait";
}

TEST_F(Wd1003CommandTest, SlowStepRatesStepAtTheirOwnPace) {
    EXPECT_NEAR(double(SeekTo(10, 0x7D)), Ms(9 * 6.5 + 11.0), 200.0);
    EXPECT_NEAR(double(SeekTo(14, 0x76)), Ms(3 * 3.0 + 11.0), 200.0);
}

TEST_F(Wd1003CommandTest, RestoreWaitsForSeekCompleteOnEveryStep) {
    SeekTo(100);
    hdd.out(0x1F7, 0x10);
    EXPECT_NEAR(double(RunWhileBusy()), Ms(100 * 11.0), 200.0);
    EXPECT_TRUE(hdd.irq_pending());
}

TEST_F(Wd1003CommandTest, ReadsSeekToTheirCylinderFirst) {
    Chs(243, 0, 1, 1);
    hdd.out(0x1F7, 0x20);
    uint64_t took = RunWhileBusy();
    EXPECT_GE(double(took), Ms(40.0));
    EXPECT_LE(double(took), Ms(40.0) + kRev + kSlot + 100);
}

TEST_F(Wd1003CommandTest, ATransferBeforeAnyStepRateRecalibratesFirst) {
    SeekTo(50);
    hdd.out(0x3F6, 0x04);
    hdd.out(0x3F6, 0x00);
    Chs(0, 0, 1, 1);
    hdd.out(0x1F7, 0x20);
    uint64_t took = RunWhileBusy();
    EXPECT_GE(double(took), Ms(50 * 11.0));
    EXPECT_LE(double(took), Ms(50 * 11.0) + kRev + kSlot + 100);
}

TEST_F(Wd1003CommandTest, WritePrecompDoesNotLandInTheErrorRegister) {
    hdd.out(0x1F1, 0x20);
    EXPECT_EQ(hdd.in(0x1F1), 0x00);
}

TEST_F(Wd1003CommandTest, DiagnoseReportsNoErrorsAndClearsTheTaskFile) {
    Chs(300, 3, 9, 5);
    hdd.out(0x1F7, 0x90);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F1), 0x01);
    EXPECT_FALSE(hdd.in(0x1F7) & 0x01);
    EXPECT_EQ(hdd.in(0x1F2), 1);
    EXPECT_EQ(hdd.in(0x1F4), 0);
    EXPECT_EQ(hdd.in(0x1F5), 0);
    EXPECT_EQ(hdd.in(0x1F6), 0);
}

TEST_F(Wd1003CommandTest, ReadVerifyPastTheEndReportsIdnf) {
    Chs(732, 4, 17, 2);
    hdd.out(0x1F7, 0x41);
    RunWhileBusy();
    EXPECT_TRUE(hdd.in(0x1F7) & 0x01);
    EXPECT_EQ(hdd.in(0x1F1), 0x10);
}

TEST_F(Wd1003CommandTest, MultiSectorReadInterruptsPerSectorAndNotAtTheEnd) {
    Chs(0, 0, 1, 3);
    hdd.out(0x1F7, 0x20);
    for (int s = 0; s < 3; ++s) {
        RunWhileBusy();
        ASSERT_TRUE(hdd.irq_pending()) << "sector " << s;
        EXPECT_EQ(hdd.in(0x1F7) & 0x88, 0x08);
        EXPECT_FALSE(hdd.irq_pending());
        EXPECT_EQ(hdd.data_in16() & 0xFF, s);
        for (int i = 1; i < 256; ++i) hdd.data_in16();
    }
    EXPECT_FALSE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F7) & 0xC8, 0x40);
}

TEST_F(Wd1003CommandTest, WriteHoldsBsyUntilEachSectorPassesTheHeadThenInterrupts) {
    const auto& d = hdd.drives[0];
    std::size_t s1 = std::size_t(d.offset_for(3, 0, 1)), s2 = std::size_t(d.offset_for(3, 0, 2));
    SeekTo(3);
    Chs(3, 0, 1, 2);
    hdd.out(0x1F7, 0x30);
    EXPECT_EQ(hdd.in(0x3F6) & 0x88, 0x08);
    EXPECT_FALSE(hdd.irq_pending());
    for (int i = 0; i < 256; ++i) hdd.data_out16(0xABCD);
    EXPECT_EQ(hdd.in(0x3F6) & 0x88, 0x80);
    EXPECT_NE(d.image[s1], 0xCD);
    uint64_t took = RunWhileBusy();
    EXPECT_GT(took, 0u);
    EXPECT_LE(double(took), kRev + kSlot + 100);
    EXPECT_EQ(d.image[s1], 0xCD);
    EXPECT_NE(d.image[s2], 0xCD);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F7) & 0x88, 0x08);
    EXPECT_FALSE(hdd.irq_pending());
    for (int i = 0; i < 256; ++i) hdd.data_out16(0xABCD);
    RunWhileBusy();
    EXPECT_EQ(d.image[s2], 0xCD);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F7) & 0xC9, 0x40);
}

TEST_F(Wd1003CommandTest, BadBlockMarkFailsReadVerifyAndWrite) {
    uint8_t table[34] = {};
    for (int i = 0; i < 17; ++i) table[i * 2 + 1] = uint8_t(i + 1);
    table[4 * 2] = 0x80;  // sector 5
    Format(10, 2, table);

    Chs(10, 2, 3, 4);
    hdd.out(0x1F7, 0x20);
    for (int s = 0; s < 2; ++s) {
        RunWhileBusy();
        ASSERT_TRUE(hdd.in(0x1F7) & 0x08);
        for (int i = 0; i < 256; ++i) hdd.data_in16();
    }
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F7) & 0x09, 0x01);
    EXPECT_EQ(hdd.in(0x1F1), 0x80);

    Chs(10, 2, 1, 17);
    hdd.out(0x1F7, 0x40);
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F1), 0x80);

    Chs(10, 2, 4, 2);
    hdd.out(0x1F7, 0x30);
    WriteWhileDrq(0x7777);
    EXPECT_EQ(hdd.in(0x1F1), 0x80);
    const auto& d = hdd.drives[0];
    EXPECT_EQ(d.image[std::size_t(d.offset_for(10, 2, 4))], 0x77);
    EXPECT_EQ(d.image[std::size_t(d.offset_for(10, 2, 5))], 0x00);
}

TEST_F(Wd1003CommandTest, ReformattingClearsABadBlockAndRemountForgetsThem) {
    uint8_t table[34] = {};
    for (int i = 0; i < 17; ++i) table[i * 2 + 1] = uint8_t(i + 1);
    table[0] = 0x80;
    Format(0, 0, table);
    EXPECT_EQ(hdd.drives[0].bad_sectors.size(), 1u);
    table[0] = 0x00;
    Format(0, 0, table);
    EXPECT_TRUE(hdd.drives[0].bad_sectors.empty());

    table[0] = 0x80;
    Format(0, 0, table);
    hdd.reset();
    EXPECT_EQ(hdd.drives[0].bad_sectors.size(), 1u);
    hdd.mount(0, img.data(), img.size());
    EXPECT_TRUE(hdd.drives[0].bad_sectors.empty());
}

TEST_F(Wd1003CommandTest, InterruptHeldWhileIenIsOffFiresWhenEnabled) {
    hdd.out(0x3F6, 0x02);
    hdd.out(0x1F7, 0x10);
    RunWhileBusy();
    EXPECT_FALSE(hdd.irq_pending());
    hdd.out(0x3F6, 0x00);
    EXPECT_TRUE(hdd.irq_pending());
}

TEST_F(Wd1003CommandTest, ReadVerifyFollowsTheFactoryInterleaveAndInterruptsOnce) {
    // sectors 1-4 at 3:1 sit in slots 0, 3, 6 and 9
    Chs(0, 0, 1, 4);
    hdd.out(0x1F7, 0x40);
    EXPECT_FALSE(hdd.irq_pending());
    EXPECT_NEAR(double(RunWhileBusy()), 10 * kSlot, 200.0);
    EXPECT_TRUE(hdd.irq_pending());
    EXPECT_EQ(hdd.in(0x1F7) & 0x09, 0x00);
}

TEST_F(Wd1003CommandTest, ReadWaitsForTheSectorToComeRound) {
    RunTo(uint64_t(5 * kSlot));
    Chs(0, 0, 1, 1);
    hdd.out(0x1F7, 0x20);
    EXPECT_NEAR(double(RunWhileBusy()), kRev - 4 * kSlot, 200.0);
}

TEST_F(Wd1003CommandTest, NextSectorIsBusyUntilItsSlotPasses) {
    Chs(0, 0, 1, 2);
    hdd.out(0x1F7, 0x20);
    RunWhileBusy();
    for (int i = 0; i < 256; ++i) hdd.data_in16();
    EXPECT_TRUE(hdd.in(0x3F6) & 0x80);
    RunWhileBusy();
    EXPECT_NEAR(double(now), 4 * kSlot, 200.0);
}

TEST_F(Wd1003CommandTest, FormatTrackStartsAtIndexAndZeroesTheTrack) {
    img.assign(img.size(), 0xE5);
    hdd.mount(0, img.data(), img.size());
    SeekTo(2);
    RunTo(now + 50'000);
    Chs(2, 1, 1, 17);
    hdd.out(0x1F7, 0x50);
    EXPECT_TRUE(hdd.in(0x1F7) & 0x08);
    for (int i = 0; i < 256; ++i) hdd.data_out16(i < 17 ? uint16_t((i + 1) << 8) : 0);
    double start = double(now);
    RunWhileBusy();
    EXPECT_NEAR(double(now), (std::floor(start / kRev) + 2) * kRev, 200.0) << "next index, then one turn";
    EXPECT_TRUE(hdd.irq_pending());
    const auto& d = hdd.drives[0];
    for (int r = 1; r <= 17; ++r) EXPECT_EQ(d.image[std::size_t(d.offset_for(2, 1, r)) + 100], 0) << r;
    EXPECT_EQ(d.image[std::size_t(d.offset_for(2, 2, 1))], 0xE5);
    EXPECT_EQ(d.image[std::size_t(d.offset_for(2, 0, 17))], 0xE5);
    EXPECT_TRUE(hdd.dirty(0));
}

TEST_F(Wd1003CommandTest, DirtyRangesCoverOnlyTheWrittenSectorsUntilCleared) {
    EXPECT_TRUE(hdd.dirty_ranges(0).empty());
    Chs(10, 2, 4, 2);
    hdd.out(0x1F7, 0x30);
    WriteWhileDrq(0x7777);
    auto ranges = hdd.dirty_ranges(0);
    ASSERT_EQ(ranges.size(), 1u);
    long off = hdd.drives[0].offset_for(10, 2, 4);
    EXPECT_LE(long(ranges[0].offset), off);
    EXPECT_GE(long(ranges[0].offset + ranges[0].length), off + 1024);
    EXPECT_LE(ranges[0].length, 2u * Wd1003::Drive::kDirtyPageSize);
    hdd.clear_dirty(0);
    EXPECT_FALSE(hdd.dirty(0));
    EXPECT_TRUE(hdd.dirty_ranges(0).empty());
}

TEST_F(Wd1003CommandTest, FormatTrackMarksTheZeroedSectorsDirty) {
    uint8_t table[34] = {};
    for (int i = 0; i < 17; ++i) table[i * 2 + 1] = uint8_t(i + 1);
    Format(3, 1, table);
    long first = hdd.drives[0].offset_for(3, 1, 1), last = hdd.drives[0].offset_for(3, 1, 17) + 512;
    uint32_t covered = 0;
    for (const auto& r : hdd.dirty_ranges(0))
        if (long(r.offset) < last && long(r.offset + r.length) > first) covered += r.length;
    EXPECT_GE(long(covered), last - first);
}

TEST_F(Wd1003CommandTest, OneToOneInterleaveMissesTheNextSectorWhileTheHostDrains) {
    uint8_t table[34] = {};
    for (int i = 0; i < 16; ++i) table[i * 2 + 1] = uint8_t(i + 1);
    Format(0, 0, table);
    uint64_t start = uint64_t(std::ceil(double(now) / kRev) * kRev);
    RunTo(start - 150);
    Chs(0, 0, 1, 2);
    hdd.out(0x1F7, 0x20);
    RunWhileBusy();
    for (int i = 0; i < 256; ++i) hdd.data_in16();
    RunWhileBusy();
    EXPECT_NEAR(double(now - start), kRev + 2 * kSlot, 200.0);
    for (int i = 0; i < 256; ++i) hdd.data_in16();

    Chs(0, 0, 17, 1);
    uint64_t t0 = now;
    hdd.out(0x1F7, 0x21);
    RunWhileBusy();
    EXPECT_NEAR(double(now - t0), 2 * kRev, 200.0);
    EXPECT_EQ(hdd.in(0x1F1), 0x10);
}

TEST_F(Wd1003CommandTest, MissingIdIsSearchedForTenRevolutions) {
    Chs(733, 0, 1, 1);
    hdd.out(0x1F7, 0x20);
    EXPECT_NEAR(double(RunWhileBusy()), 10 * kRev, 200.0);
    EXPECT_EQ(hdd.in(0x1F7) & 0x01, 0x01);
    EXPECT_EQ(hdd.in(0x1F1), 0x10);
}

TEST_F(Wd1003CommandTest, EccMatchesTheWd11c00Generator) {
    std::vector<uint8_t> zeros(512, 0);
    EXPECT_EQ(Wd1003::data_ecc(zeros.data()), 0x15CFE3A9u);
}

TEST_F(Wd1003CommandTest, ReadLongSendsTheDataThenItsFourEccBytes) {
    Chs(0, 0, 3, 1);
    hdd.out(0x1F7, 0x22);
    RunWhileBusy();
    ASSERT_TRUE(hdd.in(0x1F7) & 0x08);
    EXPECT_EQ(hdd.data_in16(), 2);
    for (int i = 1; i < 256; ++i) hdd.data_in16();
    EXPECT_TRUE(hdd.in(0x1F7) & 0x08);
    uint8_t ecc[4];
    for (auto& b : ecc) b = hdd.in(0x1F0);
    EXPECT_EQ(ecc[0], 0x8A);
    EXPECT_EQ(ecc[1], 0xC2);
    EXPECT_EQ(ecc[2], 0xDF);
    EXPECT_EQ(ecc[3], 0x03);
    EXPECT_FALSE(hdd.in(0x1F7) & 0x08);
}

class Wd1003EccTest : public Wd1003CommandTest {
protected:
    // WRITE LONG sector 1 of cylinder 5 head 0 with this data and ECC.
    void WriteLong(const std::vector<uint8_t>& data, uint32_t ecc) {
        Chs(5, 0, 1, 1);
        hdd.out(0x1F7, 0x32);
        for (std::size_t i = 0; i < 512; i += 2) hdd.data_out16(uint16_t(data[i] | (data[i + 1] << 8)));
        for (int i = 3; i >= 0; --i) hdd.out(0x1F0, uint8_t(ecc >> (8 * i)));
        RunWhileBusy();
        hdd.in(0x1F7);
    }
    std::vector<uint8_t> Pattern() {
        std::vector<uint8_t> d(512);
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(i * 7 + 3);
        return d;
    }
    std::vector<uint8_t> ReadBack(uint8_t* status, uint64_t* took) {
        Chs(5, 0, 1, 1);
        uint64_t t0 = now;
        hdd.out(0x1F7, 0x20);
        RunWhileBusy();
        *took = now - t0;
        *status = hdd.in(0x1F7);
        std::vector<uint8_t> out;
        for (int i = 0; i < 256; ++i) {
            uint16_t w = hdd.data_in16();
            out.push_back(uint8_t(w));
            out.push_back(uint8_t(w >> 8));
        }
        return out;
    }
};

TEST_F(Wd1003EccTest, WriteLongWithTheRightEccReadsClean) {
    auto data = Pattern();
    WriteLong(data, Wd1003::data_ecc(data.data()));
    EXPECT_TRUE(hdd.drives[0].ecc_override.empty());
    uint8_t st;
    uint64_t took;
    EXPECT_EQ(ReadBack(&st, &took), data);
    EXPECT_EQ(st & 0x05, 0x00);
    EXPECT_LT(double(took), kRev + 200);
}

TEST_F(Wd1003EccTest, AFiveBitBurstIsCorrectedAfterOneReread) {
    auto good = Pattern();
    uint32_t ecc = Wd1003::data_ecc(good.data());
    auto bad = good;
    bad[100] ^= 0x0E;
    bad[101] ^= 0x80;
    WriteLong(bad, ecc);
    uint8_t st;
    uint64_t took;
    EXPECT_EQ(ReadBack(&st, &took), good);
    EXPECT_EQ(st & 0x0D, 0x0C);
    EXPECT_GT(double(took), kRev);
    EXPECT_LT(double(took), 2 * kRev + 200);
    EXPECT_EQ(hdd.drives[0].image[std::size_t(hdd.drives[0].offset_for(5, 0, 1)) + 100], bad[100]);
}

TEST_F(Wd1003EccTest, AWiderErrorIsUncorrectableButTheDataStillComes) {
    auto good = Pattern();
    uint32_t ecc = Wd1003::data_ecc(good.data());
    auto bad = good;
    bad[10] ^= 0x01;
    bad[300] ^= 0x80;
    WriteLong(bad, ecc);
    uint8_t st;
    uint64_t took;
    EXPECT_EQ(ReadBack(&st, &took), bad);
    EXPECT_EQ(st & 0x09, 0x09);
    EXPECT_EQ(hdd.in(0x1F1), 0x40);
    EXPECT_GT(double(took), 8 * kRev);
    EXPECT_LT(double(took), 9 * kRev + 200);
    EXPECT_EQ(hdd.in(0x1F7) & 0x89, 0x01);
}

TEST_F(Wd1003EccTest, AMultiSectorReadStopsAfterTheUncorrectableSector) {
    auto bad = Pattern();
    WriteLong(bad, ~Wd1003::data_ecc(bad.data()));
    Chs(5, 0, 1, 3);
    hdd.out(0x1F7, 0x20);
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F1), 0x40);
    for (int i = 0; i < 256; ++i) hdd.data_in16();
    EXPECT_EQ(hdd.in(0x1F7) & 0x89, 0x01);
    EXPECT_FALSE(hdd.busy());
}

TEST_F(Wd1003EccTest, ReadVerifyCorrectsOrReportsTheSameWay) {
    auto data = Pattern();
    uint32_t ecc = Wd1003::data_ecc(data.data());
    WriteLong(data, ecc ^ 0x00000300);
    Chs(5, 0, 1, 1);
    hdd.out(0x1F7, 0x40);
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F7) & 0x05, 0x04);
    WriteLong(data, ~ecc);
    Chs(5, 0, 1, 1);
    hdd.out(0x1F7, 0x40);
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F7) & 0x01, 0x01);
    EXPECT_EQ(hdd.in(0x1F1), 0x40);
}

TEST_F(Wd1003EccTest, ReadLongReturnsTheWrittenEccAndANormalWriteReplacesIt) {
    auto data = Pattern();
    WriteLong(data, 0x12345678);
    Chs(5, 0, 1, 1);
    hdd.out(0x1F7, 0x22);
    RunWhileBusy();
    EXPECT_EQ(hdd.in(0x1F7) & 0x01, 0x00);
    for (int i = 0; i < 256; ++i) hdd.data_in16();
    EXPECT_EQ(hdd.in(0x1F0), 0x12);
    EXPECT_EQ(hdd.in(0x1F0), 0x34);
    EXPECT_EQ(hdd.in(0x1F0), 0x56);
    EXPECT_EQ(hdd.in(0x1F0), 0x78);

    Chs(5, 0, 1, 1);
    hdd.out(0x1F7, 0x30);
    WriteWhileDrq(0);
    EXPECT_TRUE(hdd.drives[0].ecc_override.empty());
    hdd.mount(0, img.data(), img.size());
    WriteLong(data, 0x12345678);
    hdd.mount(0, img.data(), img.size());
    EXPECT_TRUE(hdd.drives[0].ecc_override.empty());
}

TEST_F(Wd1003CommandTest, UnknownCommandsAbort) {
    for (uint8_t cmd : {0xEC, 0xE0, 0x00, 0xA0}) {
        Chs(0, 0, 1, 1);
        hdd.out(0x1F7, cmd);
        EXPECT_EQ(hdd.in(0x1F7) & 0x01, 0x01) << int(cmd);
        EXPECT_EQ(hdd.in(0x1F1), 0x04) << int(cmd);
    }
}

}  // namespace
