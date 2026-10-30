// Donkey Kong video board, TKG4-14-VIDEO (DKONG_REVIEW.md §5).

#ifndef DKONG_VIDEO_H
#define DKONG_VIDEO_H

#include <array>
#include <cstdint>

namespace dkong {

// Native raster: 256 pixels × 224 lines. The tube is mounted on its side,
// so the cabinet shows 224 wide × 256 tall.
constexpr int kNativeW = 256;
constexpr int kNativeH = 224;
constexpr int kUprightW = kNativeH;
constexpr int kUprightH = kNativeW;

// H counts 384 pixels a line, V runs 248..511 (5C/6C preset) for 264 lines.
// One CPU clock is two pixel clocks (61.44 MHz / 20 vs / 10).
constexpr int kPixelsPerLine = 384;
constexpr int kCpuCyclesPerLine = kPixelsPerLine / 2;
constexpr int kLinesPerFrame = 264;
constexpr int kCpuCyclesPerFrame = kLinesPerFrame * kCpuCyclesPerLine;
constexpr int kFirstV = 248;
// Line index of V=$1F0, where 4B drops /VBLK (32V·64V·128V at 16V).
constexpr int kVblankLine = 0x1F0 - kFirstV;
// Line index of V=$110, the first displayed line.
constexpr int kFirstVisibleLine = 0x110 - kFirstV;
// 2K: VRAM busy from 64H·32H·16H·8H in the blank until 256H goes high.
constexpr int kVramFreeStart = 256;
constexpr int kVramFreeEnd = 376;

constexpr int kSpritesPerLine = 16;  // 93419 at 7M is 64 × 9: 16 four-byte entries

struct VideoRoms {
    std::array<uint8_t, 0x800> char_3p{};
    std::array<uint8_t, 0x800> char_3n{};
    std::array<std::array<uint8_t, 0x800>, 4> obj{};  // 7C, 7D, 7E, 7F
    std::array<uint8_t, 256> prom_2n{};  // tile colour code by (row/4, column)
    std::array<uint8_t, 256> prom_2f{};  // R2..R0, G2 (active low)
    std::array<uint8_t, 256> prom_2e{};  // G1, G0, B1, B0 (active low)
};

class Video {
public:
    std::array<uint8_t, 0x400> vram{};     // 2P/2R 2114, $7400
    std::array<uint8_t, 0x400> obj_ram{};  // 6P/6R 2148, $7000
    VideoRoms roms;
    uint8_t cref = 0;       // PROM A7/A6
    bool flip = false;      // FLIP net, after the 5J inverter
    bool obj_bank = false;  // 2PSL: object RAM A9 on the scan side

    void reset();
    void load(const VideoRoms& r);
    // Advances the beam; returns true on the /VBLK falling edge.
    bool advance(int cpu_cycles);
    int line() const { return frame_cycle_ / kCpuCyclesPerLine; }
    int hpos() const { return (frame_cycle_ % kCpuCyclesPerLine) * 2; }
    bool in_vblank() const;
    // CPU clocks until the 2K flip-flop releases VRAM, 0 if free now.
    int vram_wait() const;
    void render(uint32_t* upright) const;
    uint32_t palette(unsigned ix) const { return palette_[ix & 0xFF]; }
    const std::array<uint32_t, kNativeW * kNativeH>& native() const { return native_; }

private:
    int frame_cycle_ = 0;
    std::array<uint32_t, 256> palette_{};
    std::array<uint32_t, kNativeW * kNativeH> native_{};

    void build_palette();
    void draw_line(int line);
};

}  // namespace dkong

#endif
