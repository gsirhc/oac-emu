#include <gtest/gtest.h>

#include "hwtest_roms.h"
#include "hwtest_set.h"
#include "machine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using Frame = std::array<uint32_t, dkong::kUprightW * dkong::kUprightH>;

// White for pens 1-3 of colour code 1 in every bank, black elsewhere.
dkong::RomSet palette_set() {
    dkong::RomSet s;
    for (int i = 0; i < 256; i++) {
        const bool lit = ((i >> 2) & 0xF) == 1 && (i & 3);
        s.video.prom_2f[i] = lit ? 0x0 : 0xF;
        s.video.prom_2e[i] = lit ? 0x0 : 0xF;
    }
    return s;
}

int lit_pixels(const dkong::Machine& m) {
    Frame rgb{};
    m.render(rgb.data());
    return int(std::count_if(rgb.begin(), rgb.end(), [](uint32_t p) { return p != 0; }));
}

double peak(const std::vector<float>& v) {
    double p = 0;
    for (float x : v) p = std::max(p, double(std::fabs(x)));
    return p;
}

}  // namespace

TEST(Machine, MapsWorkRamVramAndObjRam) {
    dkong::Machine m;
    m.reset();
    m.mem_write(0x6000, 0xA5);
    EXPECT_EQ(m.mem_read(0x6000), 0xA5);
    EXPECT_EQ(m.work_ram[0], 0xA5);
    m.mem_write(0x6FFF, 0x3C);
    EXPECT_EQ(m.work_ram[0xFFF], 0x3C);
    m.mem_write(0x7000, 0x11);
    EXPECT_EQ(m.video.obj_ram[0], 0x11);
    EXPECT_EQ(m.mem_read(0x7000), 0x11);
    m.mem_write(0x7400, 0x22);
    EXPECT_EQ(m.video.vram[0], 0x22);
    EXPECT_EQ(m.mem_read(0x7400), 0x22);
}

TEST(Machine, ProgramRomIsNotWritable) {
    dkong::RomSet s;
    s.program[0] = 0x76;
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x0000, 0xFF);
    EXPECT_EQ(m.mem_read(0x0000), 0x76);
}

TEST(Machine, NoWatchdogResetsAHaltedCpu) {
    dkong::RomSet s;
    s.program[0] = 0x76;  // HALT
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.work_ram[0x10] = 0xA5;
    m.run_cycles(dkong::kCpuCyclesPerFrame * 20);
    EXPECT_EQ(m.work_ram[0x10], 0xA5);
    EXPECT_EQ(m.frames, 20);
    EXPECT_TRUE(m.cpu.halted);
}

TEST(Machine, VblankNmiOnlyWhileLatchIsSet) {
    // $0066: LD HL,$6000 / INC (HL) / XOR A / LD ($7D84),A / INC A / LD ($7D84),A / RETN
    dkong::RomSet s;
    const uint8_t main[] = {0x31, 0xF0, 0x6F, 0x18, 0xFE};  // LD SP,$6FF0 / JR $
    std::copy(std::begin(main), std::end(main), s.program.begin());
    const uint8_t nmi[] = {0x21, 0x00, 0x60, 0x34, 0xAF, 0x32, 0x84, 0x7D,
                           0x3C, 0x32, 0x84, 0x7D, 0xED, 0x45};
    std::copy(std::begin(nmi), std::end(nmi), s.program.begin() + 0x66);
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(dkong::kCpuCyclesPerFrame * 3);
    EXPECT_EQ(m.work_ram[0], 0);
    m.mem_write(0x7D84, 1);
    m.run_cycles(dkong::kCpuCyclesPerFrame * 5);
    EXPECT_GE(m.work_ram[0], 4);
    EXPECT_LE(m.work_ram[0], 5);
}

TEST(Machine, NmiArrivesAtTheVblankLine) {
    dkong::RomSet s;
    s.program[0] = 0x18;
    s.program[1] = 0xFE;
    s.program[0x66] = 0x76;  // HALT in the handler
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7D84, 1);
    while (!m.cpu.halted) m.run_cycles(4);
    EXPECT_EQ(m.video.line(), dkong::kVblankLine);
    EXPECT_TRUE(m.video.in_vblank());
}

TEST(Machine, InputsAreActiveHighThroughTheLs240s) {
    dkong::Machine m;
    m.reset();
    m.inputs.in0 = 0x11;
    m.inputs.in1 = 0x02;
    m.inputs.in2 = 0x0C;
    m.inputs.dip = 0x5A;
    EXPECT_EQ(m.mem_read(0x7C00), 0x11);
    EXPECT_EQ(m.mem_read(0x7C80), 0x02);
    EXPECT_EQ(m.mem_read(0x7D00), 0x0C);
    EXPECT_EQ(m.mem_read(0x7D80), 0x5A);
    EXPECT_EQ(m.mem_read(0x7C7F), 0x11);
    EXPECT_EQ(m.mem_read(0x7DFF), 0x5A);
}

TEST(Machine, FactoryDipIsUprightOnly) {
    // TKG4-UP manual p.5: A-G OFF (3 lives, 7000, 1 coin/1 play), H ON.
    dkong::Machine m;
    EXPECT_EQ(m.inputs.dip, 0x80);
}

TEST(Machine, CoinSwitchFiresTheLs123OneShot) {
    dkong::Machine m;
    m.reset();
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0);
    m.inputs.coin = true;
    m.run_cycles(dkong::kCpuHz / 1000);
    m.inputs.coin = false;
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0x80);
    m.run_cycles(dkong::kCpuHz * 85 / 1000);
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0x80);
    m.run_cycles(dkong::kCpuHz * 15 / 1000);
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0);
}

TEST(Machine, ServiceSwitchSharesTheCoinBit) {
    dkong::Machine m;
    m.reset();
    m.inputs.service = true;
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0x80);
    m.inputs.in2 = 0x80;
    m.inputs.service = false;
    EXPECT_EQ(m.mem_read(0x7D00) & 0x80, 0);
}

TEST(Machine, Dma8257CopiesWorkRamToObjectRamOnGate) {
    // US ROM $0141: mode $53, ch0 $6900 TC $4180, ch1 $7000 TC $8180.
    dkong::Machine m;
    m.reset();
    for (unsigned i = 0; i < 0x181; i++) m.work_ram[0x900 + i] = uint8_t(i * 7);
    m.mem_write(0x7808, 0x53);
    m.mem_write(0x7800, 0x00);
    m.mem_write(0x7800, 0x69);
    m.mem_write(0x7801, 0x80);
    m.mem_write(0x7801, 0x41);
    m.mem_write(0x7802, 0x00);
    m.mem_write(0x7802, 0x70);
    m.mem_write(0x7803, 0x80);
    m.mem_write(0x7803, 0x81);
    m.mem_write(0x7D85, 1);
    for (unsigned i = 0; i < 0x181; i++)
        ASSERT_EQ(m.video.obj_ram[i], uint8_t(i * 7)) << i;
    EXPECT_EQ(m.video.obj_ram[0x181], 0);
}

TEST(Machine, DmaNeedsBothChannelsAndARisingGate) {
    dkong::Machine m;
    m.reset();
    m.work_ram[0x900] = 0x42;
    m.mem_write(0x7808, 0x01);  // ch0 only
    m.mem_write(0x7800, 0x00);
    m.mem_write(0x7800, 0x69);
    m.mem_write(0x7801, 0x00);
    m.mem_write(0x7801, 0x40);
    m.mem_write(0x7802, 0x00);
    m.mem_write(0x7802, 0x70);
    m.mem_write(0x7803, 0x00);
    m.mem_write(0x7803, 0x80);
    m.mem_write(0x7D85, 1);
    EXPECT_EQ(m.video.obj_ram[0], 0);
    m.mem_write(0x7808, 0x03);
    m.mem_write(0x7D85, 1);  // already high: no edge
    EXPECT_EQ(m.video.obj_ram[0], 0);
    m.mem_write(0x7D85, 0);
    m.mem_write(0x7D85, 1);
    EXPECT_EQ(m.video.obj_ram[0], 0x42);
}

TEST(Machine, DmaTcStopDisablesTheChannels) {
    dkong::Machine m;
    m.reset();
    m.work_ram[0x900] = 0x42;
    m.mem_write(0x7808, 0x43);
    m.mem_write(0x7800, 0x00);
    m.mem_write(0x7800, 0x69);
    m.mem_write(0x7801, 0x00);
    m.mem_write(0x7801, 0x40);
    m.mem_write(0x7802, 0x00);
    m.mem_write(0x7802, 0x70);
    m.mem_write(0x7803, 0x00);
    m.mem_write(0x7803, 0x80);
    m.mem_write(0x7D85, 1);
    EXPECT_EQ(m.video.obj_ram[0], 0x42);
    m.video.obj_ram[1] = 0x99;
    m.work_ram[0x901] = 0x55;
    m.mem_write(0x7D85, 0);
    m.mem_write(0x7D85, 1);
    EXPECT_EQ(m.video.obj_ram[1], 0x99);
}

TEST(Machine, DmaHoldsTheCpuOffTheBus) {
    // 64 bytes at 2 x 4 clocks each plus BUSRQ/BUSAK: the NOP run falls behind.
    dkong::RomSet s;
    dkong::Machine plain, held;
    plain.load_roms(s);
    held.load_roms(s);
    plain.reset();
    held.reset();
    held.mem_write(0x7808, 0x03);
    held.mem_write(0x7801, 0x3F);
    held.mem_write(0x7801, 0x40);
    held.mem_write(0x7803, 0x3F);
    held.mem_write(0x7803, 0x80);
    held.mem_write(0x7D85, 1);
    plain.run_cycles(1000);
    held.run_cycles(1000);
    const int lost = (plain.cpu.pc - held.cpu.pc) * 4;
    EXPECT_GE(lost, 64 * 8);
    EXPECT_LE(lost, 64 * 8 + 8);
}

TEST(Machine, VramIsFreeOnlyInHorizontalBlank) {
    dkong::Video v;
    v.reset();
    EXPECT_GT(v.vram_wait(), 0);
    v.advance(dkong::kVramFreeStart / 2);
    EXPECT_EQ(v.vram_wait(), 0);
    v.advance((dkong::kVramFreeEnd - dkong::kVramFreeStart) / 2 - 1);
    EXPECT_EQ(v.vram_wait(), 0);
    v.advance(1);
    EXPECT_GT(v.vram_wait(), 0);
}

TEST(Machine, VramReadsStallTheCpu) {
    // 256 x LD A,($7400) against 256 x LD A,($6000).
    dkong::RomSet a, b;
    for (int i = 0; i < 256; i++) {
        a.program[i * 3] = b.program[i * 3] = 0x3A;
        a.program[i * 3 + 1] = 0x00;
        a.program[i * 3 + 2] = 0x74;
        b.program[i * 3 + 1] = 0x00;
        b.program[i * 3 + 2] = 0x60;
    }
    dkong::Machine ma, mb;
    ma.load_roms(a);
    mb.load_roms(b);
    ma.reset();
    mb.reset();
    ma.run_cycles(dkong::kCpuCyclesPerLine * 8);
    mb.run_cycles(dkong::kCpuCyclesPerLine * 8);
    EXPECT_LT(ma.cpu.pc * 2, mb.cpu.pc);
}

TEST(Machine, Latch5hDrivesFlipBankNmiAndCref) {
    dkong::Machine m;
    m.reset();
    EXPECT_TRUE(m.video.flip);
    m.mem_write(0x7D82, 1);
    EXPECT_FALSE(m.video.flip);
    m.mem_write(0x7D83, 1);
    EXPECT_TRUE(m.video.obj_bank);
    m.mem_write(0x7D84, 1);
    EXPECT_TRUE(m.nmi_enabled());
    m.mem_write(0x7D86, 1);
    m.mem_write(0x7D87, 0);
    EXPECT_EQ(m.video.cref, 1);
    m.mem_write(0x7D86, 0);
    m.mem_write(0x7D87, 1);
    EXPECT_EQ(m.video.cref, 2);
    m.mem_write(0x7D86, 0x01);
    m.mem_write(0x7D86, 0xFE);  // only D0 reaches the LS259
    EXPECT_EQ(m.video.cref, 2);
    EXPECT_EQ(m.latch_5h, 0x9C);
}

TEST(Machine, LatchWritesDoNotChangeInputReads) {
    dkong::Machine m;
    m.reset();
    m.inputs.dip = 0x84;
    m.inputs.in2 = 0x04;
    m.mem_write(0x7D80, 1);
    m.mem_write(0x7D00, 1);
    m.mem_write(0x7C00, 0x0F);
    EXPECT_EQ(m.mem_read(0x7D80), 0x84);
    EXPECT_EQ(m.mem_read(0x7D00), 0x04);
    EXPECT_EQ(m.mem_read(0x7C00), 0x00);
}

TEST(Machine, SoundLatch3dHoldsFourBits) {
    dkong::Machine m;
    m.reset();
    m.mem_write(0x7C00, 0xA5);
    EXPECT_EQ(m.latch_3d, 0x05);
}

TEST(Machine, SoundCpuReadsTheInvertedCommandWhenP26IsHigh) {
    // MOVX A,@R0 / OUTL P1,A / JMP $
    dkong::RomSet s;
    const uint8_t prog[] = {0x80, 0x39, 0x04, 0x03};
    std::copy(std::begin(prog), std::end(prog), s.sound_prog.begin());
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7C00, 0x05);
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0xFA);
}

TEST(Machine, SoundCpuReads3fWhenP26IsLow) {
    // ANL P2,#$BF / MOVX A,@R0 / OUTL P1,A / JMP $  (P20-P22 high: page 7)
    dkong::RomSet s;
    const uint8_t prog[] = {0x9A, 0xBF, 0x80, 0x39, 0x04, 0x05};
    std::copy(std::begin(prog), std::end(prog), s.sound_prog.begin());
    s.sound_data[0x700] = 0x5C;
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0x5C);
}

TEST(Machine, SoundTriggersReachP25) {
    // IN A,P2 / OUTL P1,A / JMP 0
    dkong::RomSet s;
    const uint8_t prog[] = {0x0A, 0x39, 0x04, 0x00};
    std::copy(std::begin(prog), std::end(prog), s.sound_prog.begin());
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac() & 0x20, 0x20);
    m.mem_write(0x7D03, 1);
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac() & 0x20, 0);
}

TEST(Machine, SoundTriggersReachT1AndT0) {
    // MOV A,#0 / JT1 +2 / ORL A,#1 / JT0 +2 / ORL A,#2 / OUTL P1,A / JMP 0
    dkong::RomSet s;
    const uint8_t prog[] = {0x23, 0x00, 0x56, 0x06, 0x43, 0x01, 0x36, 0x0A,
                            0x43, 0x02, 0x39, 0x04, 0x00};
    std::copy(std::begin(prog), std::end(prog), s.sound_prog.begin());
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0x00);
    m.mem_write(0x7D04, 1);
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0x01);
    m.mem_write(0x7D05, 1);
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0x03);
}

TEST(Machine, SoundIrqFollows7d80) {
    // $00 JMP $10 / $03 MOV A,#$77 / OUTL P1,A / JMP $ / $10 EN I / JMP $
    dkong::RomSet s;
    const uint8_t lo[] = {0x04, 0x10, 0x00, 0x23, 0x77, 0x39, 0x04, 0x06};
    std::copy(std::begin(lo), std::end(lo), s.sound_prog.begin());
    s.sound_prog[0x10] = 0x05;
    s.sound_prog[0x11] = 0x04;
    s.sound_prog[0x12] = 0x11;
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0xFF);
    m.mem_write(0x7D80, 1);
    m.run_cycles(2000);
    EXPECT_EQ(m.sound.dac(), 0x77);
}

TEST(Machine, HwtestStaysQuiet) {
    dkong::Machine m;
    m.load_roms(dkong::hwtest_set());
    m.reset();
    m.run_cycles(dkong::kCpuHz);
    m.audio.clear();
    m.run_cycles(dkong::kCpuHz);
    ASSERT_FALSE(m.audio.empty());
    EXPECT_LT(peak(m.audio), 0.01);
}

TEST(Machine, DiscreteTriggersMakeSound) {
    for (uint16_t addr : {0x7D00, 0x7D01, 0x7D02}) {
        dkong::Machine m;
        m.load_roms(dkong::hwtest_set());
        m.reset();
        m.run_cycles(dkong::kCpuHz);
        m.audio.clear();
        m.mem_write(addr, 1);
        m.run_cycles(dkong::kCpuHz / 20);
        m.mem_write(addr, 0);
        m.run_cycles(dkong::kCpuHz / 2);
        EXPECT_GT(peak(m.audio), 0.05) << std::hex << addr;
    }
}

TEST(Machine, DacSwingMakesSound) {
    // ORL P2,#$80 (Iref on) / loop: MOV A,#$FF / OUTL P1 / CLR A / OUTL P1 / JMP loop
    dkong::RomSet s;
    const uint8_t prog[] = {0x8A, 0x80, 0x23, 0xFF, 0x39, 0x27, 0x39, 0x04, 0x02};
    std::copy(std::begin(prog), std::end(prog), s.sound_prog.begin());
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.run_cycles(dkong::kCpuHz / 2);
    EXPECT_GT(peak(m.audio), 0.05);
}

TEST(Machine, PaletteLadderBitsAreActiveLow) {
    dkong::RomSet s;
    s.video.prom_2f[0] = 0x0;
    s.video.prom_2e[0] = 0x0;
    s.video.prom_2f[1] = 0xF;
    s.video.prom_2e[1] = 0xF;
    s.video.prom_2f[2] = 0x1;  // R full, G2 off
    s.video.prom_2e[2] = 0xF;
    dkong::Machine m;
    m.load_roms(s);
    EXPECT_EQ(m.video.palette(0), 0xFFFFFFu & 0xFFFFFF);
    EXPECT_EQ(m.video.palette(1), 0u);
    EXPECT_EQ(m.video.palette(2) >> 16, 0xFFu);
    EXPECT_EQ(m.video.palette(2) & 0xFFFF, 0u);
}

TEST(Machine, TilePixelUsesBothPlanesAndThe2nColourCode) {
    dkong::RomSet s = palette_set();
    s.video.char_3n[1 * 8 + 0] = 0x80;  // code 1, line 0, leftmost dot
    s.video.prom_2n[0] = 0x1;           // tile column 0, row group 0
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7D82, 1);  // unflipped
    m.video.vram[2 * 32] = 1;  // rows 0-1 fall in vblank
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(lit_pixels(m), 1);
    s.video.prom_2n[0] = 0x2;
    m.load_roms(s);
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(lit_pixels(m), 0);
}

TEST(Machine, SpriteDrawsSixteenPixelsFromFourRoms) {
    dkong::RomSet s = palette_set();
    for (int r = 0; r < 16; r++) {
        s.video.obj[0][r] = 0xFF;  // 7C: left 8 dots, low plane
        s.video.obj[1][r] = 0xFF;  // 7D: right 8 dots, low plane
    }
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7D82, 1);
    m.video.obj_ram[0] = 100;
    m.video.obj_ram[1] = 0;
    m.video.obj_ram[2] = 1;
    m.video.obj_ram[3] = 100;
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(lit_pixels(m), 256);
    m.mem_write(0x7D83, 1);  // other object bank: empty
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(lit_pixels(m), 0);
}

TEST(Machine, SpriteHMirrorReversesDotOrder) {
    dkong::RomSet s = palette_set();
    for (int r = 0; r < 16; r++) s.video.obj[0][r] = 0x80;  // one dot, far left
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7D82, 1);
    m.video.obj_ram[0] = 100;
    m.video.obj_ram[2] = 1;
    m.video.obj_ram[3] = 100;
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    auto first_h = [&] {
        const auto& nat = m.video.native();
        for (int i = 0; i < int(nat.size()); i++)
            if (nat[i]) return i % dkong::kNativeW;
        return -1;
    };
    const int plain = first_h();
    m.video.obj_ram[2] = 0x81;
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(first_h(), plain + 15);
}

TEST(Machine, OnlySixteenSpritesPerLine) {
    dkong::RomSet s = palette_set();
    for (int r = 0; r < 16; r++) s.video.obj[0][r] = 0x80;
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.mem_write(0x7D82, 1);
    for (int i = 0; i < 20; i++) {
        m.video.obj_ram[i * 4 + 0] = 100;
        m.video.obj_ram[i * 4 + 2] = 1;
        m.video.obj_ram[i * 4 + 3] = uint8_t(16 + i * 10);
    }
    m.run_cycles(dkong::kCpuCyclesPerFrame);
    EXPECT_EQ(lit_pixels(m), 16 * 16);
}

TEST(Machine, FlipMirrorsBothAxes) {
    dkong::RomSet s = palette_set();
    s.video.char_3n[8] = 0x80;
    for (int i = 0; i < 32; i++) s.video.prom_2n[i] = 1;
    dkong::Machine m;
    m.load_roms(s);
    m.reset();
    m.video.vram[2 * 32] = 1;  // tile row 2, column 0
    auto where = [&] {
        m.run_cycles(dkong::kCpuCyclesPerFrame);
        const auto& nat = m.video.native();
        for (int i = 0; i < int(nat.size()); i++)
            if (nat[i]) return i;
        return -1;
    };
    m.mem_write(0x7D82, 1);
    const int plain = where();
    m.mem_write(0x7D82, 0);
    const int flipped = where();
    ASSERT_GE(plain, 0);
    ASSERT_GE(flipped, 0);
    EXPECT_EQ(flipped % dkong::kNativeW, 255 - plain % dkong::kNativeW);
    EXPECT_NE(flipped / dkong::kNativeW, plain / dkong::kNativeW);
}

TEST(Machine, HwtestHelpScreenShowsCopyrightPrompt) {
    dkong::Machine m;
    m.load_roms(dkong::hwtest_set());
    m.reset();
    m.run_cycles(dkong::kCpuHz * 4);
    Frame rgb{};
    m.render(rgb.data());
    int lit = 0, yellow = 0, white = 0, red = 0;
    for (uint32_t p : rgb) {
        const int r = int(p >> 16), g = int(p >> 8) & 0xFF, b = int(p & 0xFF);
        if (r | g | b) lit++;
        if (r > 180 && g > 180 && b < 40) yellow++;
        if (r > 180 && g > 180 && b > 180) white++;
        if (r > 180 && g < 40 && b < 40) red++;
    }
    EXPECT_GT(yellow, 40);
    EXPECT_GT(white, 80);
    EXPECT_GT(red, 40);
    EXPECT_LT(lit, dkong::kUprightW * dkong::kUprightH * 20 / 100);
    EXPECT_EQ(m.video.cref, 2);
}

TEST(Machine, HwtestColourBarsShowDmaSprites) {
    dkong::Machine m;
    m.load_roms(dkong::hwtest_set());
    m.reset();
    m.run_cycles(dkong::kCpuCyclesPerFrame * 120);
    ASSERT_EQ(m.video.cref, 1);
    EXPECT_NE(m.video.obj_ram[0], 0);
    Frame rgb{};
    m.render(rgb.data());
    // Test sprite 1 is solid at upright (72,120), white over the bars.
    const uint32_t p = rgb[(120 + 8) * dkong::kUprightW + 72 + 8];
    EXPECT_EQ(p & 0xE0E0E0, 0xE0E0E0u);
}
