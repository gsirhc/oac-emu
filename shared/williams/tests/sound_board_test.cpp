#include <gtest/gtest.h>

#include "williams/sound_board.h"

namespace {

using williams::SoundBoard;

// LDS #$7F / LDX #$400 / CB1 rising IRQ / CLI / WAI; IRQ: DAC = PB, RTI.
void load_echo(SoundBoard& s) {
    const uint8_t prog[] = {
        0x8E, 0x00, 0x7F, 0xCE, 0x04, 0x00, 0x6F, 0x01, 0x86, 0xFF, 0xA7, 0x00,
        0x86, 0x04, 0xA7, 0x01, 0x6F, 0x03, 0x6F, 0x02, 0x86, 0x07, 0xA7, 0x03,
        0x0E, 0x3E, 0x20, 0xFD,
        0xB6, 0x04, 0x02, 0xB7, 0x04, 0x00, 0x3B,
    };
    s.rom.fill(0);
    for (size_t i = 0; i < sizeof prog; i++) s.rom[i] = prog[i];
    s.rom[0xFF8] = 0xF0;
    s.rom[0xFF9] = 0x1C;
    s.rom[0xFFE] = 0xF0;
    s.rom[0xFFF] = 0x00;
    s.reset();
}

TEST(SoundBoard, MemoryMap) {
    SoundBoard s;
    s.write(0x0010, 0x5A);
    EXPECT_EQ(s.read(0x0010), 0x5A);
    s.rom[0x123] = 0x77;
    EXPECT_EQ(s.read(0xF123), 0x77);
    EXPECT_EQ(s.read(0x8000), 0xFF);
}

TEST(SoundBoard, SelectLinesRaiseCb1Interrupt) {
    SoundBoard s;
    load_echo(s);
    s.advance(200, 1000000.0);
    EXPECT_TRUE(s.cpu.waiting());
    s.set_select(0x2A);
    s.advance(200, 1000000.0);
    EXPECT_EQ(s.dac(), 0xEA);
}

TEST(SoundBoard, IdleSelectDoesNotInterrupt) {
    SoundBoard s;
    load_echo(s);
    s.advance(200, 1000000.0);
    s.set_select(0x3F);
    s.advance(200, 1000000.0);
    EXPECT_EQ(s.dac(), 0x00);
}

TEST(SoundBoard, CpuRunsAtQuarterCrystal) {
    SoundBoard s;
    load_echo(s);
    const uint64_t before = s.cpu.cycles;
    s.advance(100000, 1000000.0);
    const double ran = double(s.cpu.cycles - before);
    EXPECT_NEAR(ran, williams::kSoundCpuHz / 10.0, 20.0);
}

TEST(SoundBoard, CouplingBlocksDc) {
    SoundBoard s;
    s.pia.write(0, 0xFF);
    s.pia.write(1, 0x04);
    s.pia.write(0, 0xFF);
    float first = s.sample(48000);
    float last = first;
    for (int i = 0; i < 48000; i++) last = s.sample(48000);
    EXPECT_GT(first, 0.9f);
    EXPECT_LT(last, 0.05f);
}

TEST(SoundBoard, PiaCa2ReadStrobeRestoresOnE) {
    SoundBoard s;
    s.rom.fill(0x01);
    s.rom[0xFFE] = 0xF0;
    s.rom[0xFFF] = 0x00;
    s.reset();
    s.write(0x0401, 0x2C);
    s.read(0x0400);
    EXPECT_FALSE(s.pia.ca2());
    s.advance(4, 1000000.0);
    EXPECT_TRUE(s.pia.ca2());
}

}  // namespace
