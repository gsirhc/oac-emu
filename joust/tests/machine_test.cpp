#include <gtest/gtest.h>

#include "hwtest_set.h"
#include "machine.h"

#include <memory>
#include <vector>

namespace {

using joust::Machine;

// The self-test program would repaint everything, so park the 6809 on BRA * in DRAM.
std::unique_ptr<Machine> board() {
    auto m = std::make_unique<Machine>();
    m->load_roms(joust::hwtest_set());
    m->reset();
    m->audio_hz = 0;
    m->mem_write(0x9000, 0x20);
    m->mem_write(0x9001, 0xFE);
    m->cpu.pc = 0x9000;
    return m;
}

TEST(Machine, BankRomReadsOnlyWhenEnabled) {
    auto m = board();
    m->mem_write(0x0040, 0x99);
    EXPECT_EQ(m->mem_read(0x0040), 0x99);
    m->mem_write(0xC900, 0x01);
    EXPECT_TRUE(m->rom_enable);
    EXPECT_EQ(m->mem_read(0x0040), joust::hwtest::bank[0x40]);
    m->mem_write(0x0040, 0x11);
    m->mem_write(0xC900, 0x00);
    EXPECT_EQ(m->mem_read(0x0040), 0x11);
    EXPECT_EQ(m->mem_read(0x9800), m->dram[size_t(m->dram_index(0x9800))]);
}

TEST(Machine, FixedRomAtD000) {
    auto m = board();
    EXPECT_EQ(m->mem_read(0xFFFE), joust::hwtest::fixed[0x2FFE]);
    m->mem_write(0xE000, 0x12);
    EXPECT_EQ(m->mem_read(0xE000), joust::hwtest::fixed[0x1000]);
}

TEST(Machine, ThreeGInterleavesColumnsAcrossBanks) {
    auto m = board();
    // Slot (column + 5) mod 192: three columns per page, one per bank.
    EXPECT_EQ(m->dram_index(0x0000), 2 * 0x4000 + 1 * 0x100);
    EXPECT_EQ(m->dram_index(0x0100), 0 * 0x4000 + 2 * 0x100);
    EXPECT_EQ(m->dram_index(0x0234), 1 * 0x4000 + 2 * 0x100 + 0x34);
    EXPECT_EQ(m->dram_index(0xC000), -1);
}

TEST(Machine, CmosLowNibbleAndMemoryProtect) {
    auto m = board();
    m->mem_write(0xCD10, 0xA7);
    EXPECT_EQ(m->mem_read(0xCD10), 0xF7);
    m->mem_write(0xCC10, 0x05);
    EXPECT_EQ(m->cmos[0x010], 0);
    m->inputs.door_open = true;
    m->mem_write(0xCC10, 0x05);
    EXPECT_EQ(m->cmos[0x010], 5);
}

TEST(Machine, CmosSurvivesReset) {
    auto m = board();
    m->mem_write(0xCE00, 0x03);
    m->reset();
    EXPECT_EQ(m->cmos[0x200], 3);
}

TEST(Machine, PaletteWritesAndScreenControlLatch) {
    auto m = board();
    m->mem_write(0xC005, 0xC7);
    EXPECT_EQ(m->palette[5], 0xC7);
    m->mem_write(0xC900, 0x02);
    EXPECT_TRUE(m->screen_control);
    EXPECT_FALSE(m->rom_enable);
}

TEST(Machine, VideoCountReadsLineBits2To7) {
    auto m = board();
    while (m->line() != 0x41) m->run_cycles(1);
    EXPECT_EQ(m->mem_read(0xCB00), 0x40);
}

TEST(Machine, FrameIs260LinesOf64Cycles) {
    auto m = board();
    m->run_cycles(joust::kFrameCycles * 3);
    EXPECT_EQ(m->frames, 3);
    EXPECT_EQ(joust::kFrameCycles, 16640);
}

TEST(Machine, Count240AndFourMsReachRomPia) {
    auto m = board();
    m->mem_write(0xC80D, 0x07);
    m->mem_write(0xC80F, 0x07);
    m->mem_read(0xC80C);
    m->mem_read(0xC80E);
    while (m->line() != 0xF0) m->run_cycles(1);
    EXPECT_EQ(m->mem_read(0xC80D) & 0x80, 0x80);
    m->mem_read(0xC80C);
    while (m->line() != 0x20) m->run_cycles(1);
    EXPECT_EQ(m->mem_read(0xC80F) & 0x80, 0x80);
}

TEST(Machine, RomPiaIrqReachesCpu) {
    auto m = board();
    // Park the CPU on BRA * in DRAM with interrupts open; IRQ vector is the fixed ROM's.
    m->mem_write(0x9000, 0x1C);
    m->mem_write(0x9001, 0xEF);
    m->mem_write(0x9002, 0x20);
    m->mem_write(0x9003, 0xFE);
    m->cpu.pc = 0x9000;
    m->cpu.s = 0xBF00;
    m->mem_write(0xC80F, 0x07);
    m->run_cycles(joust::kFrameCycles);
    EXPECT_EQ(m->mem_read(0xBEFE), 0x90);
    EXPECT_EQ(m->mem_read(0xBEFF), 0x02);
}

TEST(Machine, WatchdogResetsAfterEightFrames) {
    auto m = board();
    m->run_cycles(joust::kFrameCycles * 7);
    EXPECT_EQ(m->watchdog_resets, 0);
    m->run_cycles(joust::kFrameCycles * 2);
    EXPECT_EQ(m->watchdog_resets, 1);
}

TEST(Machine, WatchdogNeedsExactly39) {
    auto m = board();
    for (int i = 0; i < 12; i++) {
        m->mem_write(0xCBFF, 0x38);
        m->run_cycles(joust::kFrameCycles);
    }
    EXPECT_EQ(m->watchdog_resets, 1);
    auto n = board();
    for (int i = 0; i < 12; i++) {
        n->mem_write(0xCBFF, 0x39);
        n->run_cycles(joust::kFrameCycles);
    }
    EXPECT_EQ(n->watchdog_resets, 0);
}

TEST(Machine, WidgetMuxFollowsCb2) {
    auto m = board();
    m->inputs.p1 = joust::PANEL_FLAP;
    m->inputs.p2 = joust::PANEL_LEFT;
    m->inputs.starts = joust::PANEL_1P_START;
    m->mem_write(0xC805, 0x04);
    m->mem_write(0xC807, 0x3C);
    EXPECT_EQ(m->mem_read(0xC804), 0x24);
    m->mem_write(0xC807, 0x34);
    EXPECT_EQ(m->mem_read(0xC804), 0x21);
}

TEST(Machine, CoinDoorOnRomPiaPortA) {
    auto m = board();
    m->inputs.door = joust::DOOR_LEFT_COIN | joust::DOOR_ADVANCE;
    m->mem_write(0xC80D, 0x04);
    EXPECT_EQ(m->mem_read(0xC80C), 0x12);
}

TEST(Machine, RomPiaPortBDrivesSoundSelect) {
    auto m = board();
    m->mem_write(0xC80E, 0xFF);
    m->mem_write(0xC80F, 0x04);
    m->mem_write(0xC80E, 0x3E);
    EXPECT_EQ(m->sound.pia.port_b() & 0x3F, 0x3E);
}

TEST(Machine, BlitHaltsCpuAndHonoursInhibitOnDram) {
    auto m = board();
    const int at = m->dram_index(0x1020);
    m->dram[size_t(at)] = 0xF0;
    m->mem_write(0xCA01, 0x00);
    m->mem_write(0xCA02, 0x00);
    m->mem_write(0xCA03, 0x00);
    m->mem_write(0xCA04, 0x10);
    m->mem_write(0xCA05, 0x20);
    m->mem_write(0xCA06, 0x05);
    m->mem_write(0xCA07, 0x05);
    m->mem_write(0x0000, 0x0A);
    m->mem_write(0xCA00, 0x08);
    EXPECT_EQ(m->dram[size_t(at)], 0xFA);
    const uint64_t c0 = m->cpu.cycles;
    m->run_cycles(4);
    EXPECT_EQ(m->cpu.cycles, c0);
}

TEST(Machine, BoardCyclesRunThroughHaltAndWatchdogHold) {
    auto m = board();
    m->mem_write(0xCA06, 0x14);
    m->mem_write(0xCA07, 0x14);
    m->mem_write(0xCA00, 0x00);
    const uint64_t cpu0 = m->cpu.cycles, c0 = m->cycles;
    const int halted = m->run_cycles(200);
    EXPECT_EQ(m->cpu.cycles, cpu0);
    EXPECT_EQ(m->cycles - c0, uint64_t(halted));
    const int held = m->run_cycles(joust::kFrameCycles * 9);
    EXPECT_EQ(m->watchdog_resets, 1);
    EXPECT_EQ(m->cycles - c0, uint64_t(halted + held));
}

TEST(Machine, RomPiaCb2WriteStrobeRestoresOnE) {
    auto m = board();
    m->mem_write(0xC80F, 0x2C);
    m->mem_write(0xC80E, 0x00);
    EXPECT_FALSE(m->rom_pia.cb2());
    m->run_cycles(1);
    EXPECT_TRUE(m->rom_pia.cb2());
}

TEST(Machine, PaletteLadderLevels) {
    auto m = board();
    m->palette.fill(0);
    m->palette[1] = 0xFF;
    m->palette[2] = 0x07;
    m->palette[3] = 0x40;
    for (uint16_t c = 0; c < 0x98; c++)
        for (int r = 0; r < 0xF8; r++) m->mem_write(uint16_t((c << 8) | r), 0x12);
    m->mem_write(0x0510, 0x33);
    m->run_cycles(joust::kFrameCycles);
    std::vector<uint32_t> px(joust::kScreenW * joust::kScreenH);
    m->render(px.data());
    EXPECT_EQ(px[size_t(20 * joust::kScreenW + 2)], 0xFFFFFFu);
    EXPECT_EQ(px[size_t(20 * joust::kScreenW + 3)], 0xFF0000u);
    // Column 5 row $10 shows at x = 2 * (5 - 1), y = $10.
    EXPECT_EQ(px[size_t(0x10 * joust::kScreenW + 8)], uint32_t(255.0 * (1 / 560.0) / (1 / 560.0 + 1 / 330.0) + 0.5));
}

}  // namespace
