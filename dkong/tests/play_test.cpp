#include <gtest/gtest.h>

#include "machine.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool read_chip(const fs::path& dir, const char* name, uint8_t* dst, size_t n) {
    std::ifstream in(dir / name, std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(dst), std::streamsize(n));
    return size_t(in.gcount()) == n;
}

std::optional<dkong::RomSet> load_dir(const fs::path& dir) {
    dkong::RomSet s;
    auto& v = s.video;
    const bool ok =
        read_chip(dir, "c_5et_g.bin", s.program.data(), 0x1000) &&
        read_chip(dir, "c_5ct_g.bin", s.program.data() + 0x1000, 0x1000) &&
        read_chip(dir, "c_5bt_g.bin", s.program.data() + 0x2000, 0x1000) &&
        read_chip(dir, "c_5at_g.bin", s.program.data() + 0x3000, 0x1000) &&
        read_chip(dir, "v_3pt.bin", v.char_3p.data(), 0x800) &&
        read_chip(dir, "v_5h_b.bin", v.char_3n.data(), 0x800) &&
        read_chip(dir, "l_4m_b.bin", v.obj[0].data(), 0x800) &&
        read_chip(dir, "l_4n_b.bin", v.obj[1].data(), 0x800) &&
        read_chip(dir, "l_4r_b.bin", v.obj[2].data(), 0x800) &&
        read_chip(dir, "l_4s_b.bin", v.obj[3].data(), 0x800) &&
        read_chip(dir, "c-2j.bpr", v.prom_2f.data(), 0x100) &&
        read_chip(dir, "c-2k.bpr", v.prom_2e.data(), 0x100) &&
        read_chip(dir, "v-5e.bpr", v.prom_2n.data(), 0x100) &&
        read_chip(dir, "s_3i_b.bin", s.sound_prog.data(), 0x800) &&
        read_chip(dir, "s_3j_b.bin", s.sound_data.data(), 0x800);
    if (!ok) return std::nullopt;
    return s;
}

std::optional<dkong::RomSet> find_dump() {
    if (const char* env = std::getenv("DKONG_ROM"); env && *env) return load_dir(env);
    return load_dir(CORE_USER_ROMS);
}

struct Snapshot {
    std::array<uint8_t, 0x400> vram;
    std::array<uint8_t, 0x200> obj;
    bool flip;
};

constexpr int kYOffsets = 256;
constexpr int kXMin = -32, kXMax = 32;
constexpr int kXOffsets = kXMax - kXMin + 1;

using Scores = std::vector<long>;

// DKONG_REVIEW.md §5.2.1: count objects resting on the character layer for every offset pair.
void score(const dkong::VideoRoms& roms, const Snapshot& snap, Scores& out) {
    std::array<bool, 256> visible{};
    for (int l = dkong::kFirstVisibleLine; l < dkong::kVblankLine; l++) {
        const unsigned v = unsigned(dkong::kFirstV + l) & 0xFF;
        visible[snap.flip ? (v ^ 0xFF) : v] = true;
    }

    // Character opacity in (VF, HF), plus 16-dot windows starting at each HF.
    static std::array<std::array<bool, 256>, 256> chr;
    static std::array<std::array<uint16_t, 256>, 256> win;
    for (unsigned vf = 0; vf < 256; vf++) {
        for (unsigned hf = 0; hf < 256; hf++) {
            const uint8_t code = snap.vram[(vf >> 3) * 32 + (hf >> 3)];
            const unsigned a = unsigned(code) * 8 + (vf & 7);
            const int bit = 7 - int(hf & 7);
            chr[vf][hf] = ((roms.char_3p[a] >> bit) & 1) || ((roms.char_3n[a] >> bit) & 1);
        }
        for (unsigned x = 0; x < 256; x++) {
            uint16_t w = 0;
            for (unsigned k = 0; k < 16; k++)
                if (chr[vf][(x + k) & 0xFF]) w = uint16_t(w | (1u << k));
            win[vf][x] = w;
        }
    }

    // Screen-down is +H on the tube; HF runs the other way under FLIP.
    const int down = snap.flip ? -1 : 1;
    for (int i = 0; i < 128; i++) {
        const uint8_t* e = snap.obj.data() + i * 4;
        // The program parks unused entries at Y byte 0.
        if (e[0] == 0) continue;
        std::array<uint16_t, 16> mask{};
        std::array<int, 16> low{};
        bool any = false;
        for (unsigned r = 0; r < 16; r++) {
            const unsigned row = (e[1] & 0x80) ? (15 - r) : r;
            const unsigned a = unsigned(e[1] & 0x7F) * 16 + row;
            const unsigned p0 = (unsigned(roms.obj[0][a]) << 8) | roms.obj[1][a];
            const unsigned p1 = (unsigned(roms.obj[2][a]) << 8) | roms.obj[3][a];
            const bool mirror = (e[2] & 0x80) != 0;
            uint16_t m = 0;
            for (int k = 0; k < 16; k++) {
                const int bit = mirror ? k : 15 - k;
                if (((p0 | p1) >> bit) & 1) m = uint16_t(m | (1u << k));
            }
            mask[r] = m;
            if (!m) continue;
            any = true;
            int lo = -1;
            for (int k = 0; k < 16; k++)
                if ((m >> k) & 1) {
                    if (lo < 0 || (down > 0 ? k > lo : k < lo)) lo = k;
                }
            low[r] = lo;
        }
        if (!any) continue;

        for (int yo = 0; yo < kYOffsets; yo++) {
            for (int xo = kXMin; xo <= kXMax; xo++) {
                const unsigned x0 = unsigned(e[3] + xo) & 0xFF;
                int resting = 0;
                bool overlap = false;
                for (unsigned r = 0; r < 16 && !overlap; r++) {
                    if (!mask[r]) continue;
                    const unsigned vf = (0xF0u + r - e[0] - unsigned(yo)) & 0xFF;
                    if (!visible[vf]) continue;
                    if (win[vf][x0] & mask[r]) overlap = true;
                    else if (chr[vf][(x0 + unsigned(low[r] + down)) & 0xFF]) resting++;
                }
                if (!overlap && resting >= 2) out[size_t(yo * kXOffsets + (xo - kXMin))]++;
            }
        }
    }
}

}  // namespace

// DKONG_REVIEW.md §5.2.1. The object adder presets aren't legible, so the
// shipped constants have to be the unique best fit to the attract loop.
TEST(Play, ObjectOffsetsSolveFromAttract) {
    const auto set = find_dump();
    if (!set) GTEST_SKIP() << "no local dkong dump (loose chips) in roms/user/ or DKONG_ROM";

    dkong::Machine m;
    m.audio_hz = 0;
    m.load_roms(*set);
    m.reset();

    constexpr int kFirst = 300, kLast = 1840, kEvery = 5;
    std::vector<Snapshot> snaps;
    while (m.frames < kLast) {
        const int f = m.frames;
        m.run_cycles(dkong::kCpuCyclesPerLine);
        if (m.frames != f && m.frames >= kFirst && (m.frames - kFirst) % kEvery == 0) {
            Snapshot s;
            s.vram = m.video.vram;
            std::copy_n(m.video.obj_ram.begin() + (m.video.obj_bank ? 0x200 : 0), 0x200, s.obj.begin());
            s.flip = m.video.flip;
            snaps.push_back(s);
        }
    }

    Scores scores(size_t(kYOffsets * kXOffsets), 0);
    for (const auto& s : snaps) score(set->video, s, scores);

    size_t best = 0;
    for (size_t i = 1; i < scores.size(); i++)
        if (scores[i] > scores[best]) best = i;
    long runner_up = 0;
    for (size_t i = 0; i < scores.size(); i++)
        if (i != best && scores[i] > runner_up) runner_up = scores[i];

    const int best_y = int(best) / kXOffsets;
    const int best_x = int(best) % kXOffsets + kXMin;
    std::printf("snapshots %zu, best V 0x%02X H %d = %ld, runner-up %ld\n",
                snaps.size(), best_y, best_x, scores[best], runner_up);
    auto at = [&](int y, int x) { return scores[size_t((y & 0xFF) * kXOffsets + (x - kXMin))]; };
    std::printf("V 0x%02X: %ld, V 0x%02X: %ld at H %d\n",
                (best_y - 1) & 0xFF, at(best_y - 1, best_x), (best_y + 1) & 0xFF, at(best_y + 1, best_x), best_x);
    long other_h = 0;
    int other_h_x = 0;
    for (int x = kXMin; x <= kXMax; x++) {
        if (x == best_x) continue;
        for (int y = 0; y < kYOffsets; y++)
            if (at(y, x) > other_h) { other_h = at(y, x); other_h_x = x; }
    }
    std::printf("best at any other H: %ld (H %d)\n", other_h, other_h_x);

    EXPECT_EQ(best_y, dkong::kObjYOffset);
    EXPECT_EQ(best_x, dkong::kObjXOffset);
    EXPECT_GT(scores[best], runner_up);
}
