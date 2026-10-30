// Board smoke: the generated self-test ROM boots, writes the RAM signature,
// takes vblank NMIs and paints.

#include <gtest/gtest.h>

#include "hwtest_roms.h"
#include "hwtest_set.h"
#include "machine.h"

#include <array>

TEST(Smoke, HwtestBootsSignatureNmiAndPaints) {
    dkong::Machine m;
    m.load_roms(dkong::hwtest_set());
    m.reset();
    m.run_cycles(dkong::kCpuHz / 10);
    EXPECT_EQ(m.work_ram[0], 'T');
    EXPECT_EQ(m.work_ram[1], 'S');
    EXPECT_EQ(m.work_ram[2], 'T');
    EXPECT_EQ(m.work_ram[3], '1');
    EXPECT_GT(m.frames, 0);
    EXPECT_GT(m.work_ram[8], 0);

    std::array<uint32_t, dkong::kUprightW * dkong::kUprightH> rgb{};
    m.render(rgb.data());
    bool any = false;
    for (uint32_t p : rgb) {
        if (p) { any = true; break; }
    }
    EXPECT_TRUE(any);
}

TEST(Smoke, SoundCpuStaysAlive) {
    dkong::Machine m;
    m.load_roms(dkong::hwtest_set());
    m.reset();
    m.run_cycles(dkong::kCpuHz / 10);
    EXPECT_GT(m.sound.cpu.cycles, 0u);
    EXPECT_EQ(m.sound.dac(), 0x80);
}
