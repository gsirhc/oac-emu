#include "video.h"

#include <cmath>

namespace dkong {

namespace {

// CPU board 2F/2E ladders: 220/470/1K per gun, open-collector lows pull
// toward black, so a 0 bit adds light (TKG4-11-CPU R69-R79).
constexpr double kG220 = 1.0 / 220.0;
constexpr double kG470 = 1.0 / 470.0;
constexpr double kG1k = 1.0 / 1000.0;

uint8_t gun(unsigned bits, const double* g, int n) {
    double on = 0, all = 0;
    for (int i = 0; i < n; i++) {
        all += g[i];
        if (!((bits >> (n - 1 - i)) & 1)) on += g[i];
    }
    return uint8_t(std::lround(255.0 * on / all));
}

}  // namespace

void Video::reset() {
    frame_cycle_ = 0;
    cref = 0;
    flip = false;
    obj_bank = false;
}

void Video::load(const VideoRoms& r) {
    roms = r;
    build_palette();
}

void Video::build_palette() {
    static const double g3[3] = {kG220, kG470, kG1k};
    static const double g2[2] = {kG220, kG470};
    for (unsigned i = 0; i < 256; i++) {
        const unsigned f = roms.prom_2f[i] & 0xF;
        const unsigned e = roms.prom_2e[i] & 0xF;
        const uint8_t r = gun(f >> 1, g3, 3);
        const uint8_t g = gun(((f & 1) << 2) | (e >> 2), g3, 3);
        const uint8_t b = gun(e & 3, g2, 2);
        palette_[i] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }
}

bool Video::in_vblank() const {
    const int l = line();
    return l >= kVblankLine || l < kFirstVisibleLine;
}

int Video::vram_wait() const {
    const int h = hpos();
    if (h >= kVramFreeStart && h < kVramFreeEnd) return 0;
    const int px = h < kVramFreeStart ? kVramFreeStart - h : kPixelsPerLine - h + kVramFreeStart;
    return (px + 1) / 2;
}

bool Video::advance(int cpu_cycles) {
    bool edge = false;
    while (cpu_cycles > 0) {
        const int in_line = frame_cycle_ % kCpuCyclesPerLine;
        // Each line is composed when the beam leaves its active pixels.
        const int mark = kNativeW / 2;
        int step = in_line < mark ? mark - in_line : kCpuCyclesPerLine - in_line;
        if (step > cpu_cycles) step = cpu_cycles;
        frame_cycle_ += step;
        cpu_cycles -= step;
        if (frame_cycle_ % kCpuCyclesPerLine == mark) {
            const int l = line();
            if (l >= kFirstVisibleLine && l < kVblankLine) draw_line(l);
        }
        if (frame_cycle_ % kCpuCyclesPerLine == 0) {
            if (frame_cycle_ >= kCpuCyclesPerFrame) frame_cycle_ = 0;
            if (line() == kVblankLine) edge = true;
        }
    }
    return edge;
}

void Video::draw_line(int l) {
    const unsigned v = unsigned(kFirstV + l) & 0xFF;
    const unsigned vf = flip ? (v ^ 0xFF) : v;

    // 7M collects up to 16 hits in object RAM order; the rest are lost.
    uint8_t buf[kNativeW] = {};
    const uint8_t* bank = obj_ram.data() + (obj_bank ? 0x200 : 0);
    int hits = 0;
    for (int i = 0; i < 128 && hits < kSpritesPerLine; i++) {
        const uint8_t* e = bank + i * 4;
        const unsigned s = (unsigned(e[0]) + kObjYOffset + vf) & 0xFF;
        if ((s & 0xF0) != 0xF0) continue;
        hits++;
        const unsigned row = (e[1] & 0x80) ? (~s & 0xF) : (s & 0xF);
        const unsigned a = unsigned(e[1] & 0x7F) * 16 + row;
        const unsigned p0 = (unsigned(roms.obj[0][a]) << 8) | roms.obj[1][a];
        const unsigned p1 = (unsigned(roms.obj[2][a]) << 8) | roms.obj[3][a];
        const bool mirror = (e[2] & 0x80) != 0;
        const uint8_t col = uint8_t((e[2] & 0x0F) << 2);
        const int x0 = int(e[3]) + kObjXOffset;
        for (int k = 0; k < 16; k++) {
            const int bit = mirror ? k : 15 - k;
            const unsigned pix = (((p1 >> bit) & 1) << 1) | ((p0 >> bit) & 1);
            if (!pix) continue;
            buf[(x0 + k) & 0xFF] = uint8_t(0x80 | col | pix);
        }
    }

    uint32_t* out = native_.data() + (l - kFirstVisibleLine) * kNativeW;
    const unsigned row = vf >> 3;
    const unsigned fine = vf & 7;
    const unsigned bank_ix = unsigned(cref & 3) << 6;
    for (unsigned h = 0; h < kNativeW; h++) {
        const unsigned hf = flip ? (h ^ 0xFF) : h;
        unsigned ix;
        const uint8_t sp = buf[hf];
        if (sp) {
            ix = bank_ix | (sp & 0x3F);
        } else {
            const unsigned col = hf >> 3;
            const uint8_t code = vram[row * 32 + col];
            const unsigned a = unsigned(code) * 8 + fine;
            const int bit = 7 - int(hf & 7);
            const unsigned pix = (((roms.char_3p[a] >> bit) & 1) << 1) | ((roms.char_3n[a] >> bit) & 1);
            const unsigned cc = roms.prom_2n[((row >> 2) << 5) | col] & 0xF;
            ix = bank_ix | (cc << 2) | pix;
        }
        out[h] = palette_[ix];
    }
}

void Video::render(uint32_t* upright) const {
    // Native line n is upright column n; native pixel h runs down the tube.
    for (int n = 0; n < kNativeH; n++) {
        const uint32_t* src = native_.data() + n * kNativeW;
        for (int h = 0; h < kNativeW; h++)
            upright[h * kUprightW + (kUprightW - 1 - n)] = src[h];
    }
}

}  // namespace dkong
