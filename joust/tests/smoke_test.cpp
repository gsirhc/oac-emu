#include <gtest/gtest.h>

#include "hwtest_set.h"
#include "machine.h"

#include <memory>
#include <vector>

namespace {

using joust::Machine;

int count_rgb(const std::vector<uint32_t>& px, uint32_t rgb, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < joust::kScreenW; x++) n += px[size_t(y * joust::kScreenW + x)] == rgb;
    return n;
}

TEST(Smoke, HwtestBootsBarsThenHelpScreenAndBeeps) {
    auto m = std::make_unique<Machine>();
    m->load_roms(joust::hwtest_set());
    m->reset();
    std::vector<uint32_t> px(joust::kScreenW * joust::kScreenH);

    for (int f = 0; f < 40; f++) m->run_cycles(joust::kFrameCycles);
    m->render(px.data());
    // Sixteen bars: the white one is a full-height column.
    EXPECT_GT(count_rgb(px, 0xFFFFFF, 0, joust::kScreenH), 18 * 200);

    float peak = 0;
    for (int f = 40; f < 300; f++) {
        m->run_cycles(joust::kFrameCycles);
        for (float s : m->audio) peak = std::max(peak, s < 0 ? -s : s);
        m->audio.clear();
    }
    m->render(px.data());
    // SELF TEST OK in green on text row 5 (lines 40-47); title in red on row 2.
    EXPECT_GT(count_rgb(px, 0x00FF00, 40, 48), 60);
    EXPECT_GT(count_rgb(px, 0xFF0000, 16, 24), 60);
    EXPECT_EQ(count_rgb(px, 0x00FF00, 0, 40), 0);
    EXPECT_GT(peak, 0.5f);
    EXPECT_EQ(m->watchdog_resets, 0);
    EXPECT_EQ(m->cpu.illegal_ops, 0u);
    EXPECT_EQ(m->sound.cpu.undefined_ops, 0u);
}

}  // namespace
