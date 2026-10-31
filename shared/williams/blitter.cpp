#include "blitter.h"

namespace williams {

namespace {

// Riddle's timings: ~1 us/byte FAST, ~2 us/byte SLOW, plus the halt handshake.
constexpr int kHaltOverhead = 5;

}  // namespace

int Blitter::start(uint8_t control) {
    const uint8_t solid = regs_[1];
    uint16_t src = uint16_t((regs_[2] << 8) | regs_[3]);
    uint16_t dst = uint16_t((regs_[4] << 8) | regs_[5]);
    int w = sc1 ? (regs_[6] ^ 4) : regs_[6];
    int h = sc1 ? (regs_[7] ^ 4) : regs_[7];
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    const uint16_t sstep = (control & SRC_STRIDE_256) ? 256 : 1;
    const uint16_t dstep = (control & DST_STRIDE_256) ? 256 : 1;

    uint8_t keep = 0;
    if (control & NO_LOWER) keep |= 0x0F;
    if (control & NO_UPPER) keep |= 0xF0;

    // The shifter's held nibble carries across rows; only a new blit clears it.
    uint8_t carry = 0;
    for (int y = 0; y < h; y++) {
        uint16_t s = src, d = dst;
        for (int x = 0; x < w; x++) {
            uint8_t v = read(s);
            if (control & SHIFT) {
                const uint8_t nv = uint8_t((carry << 4) | (v >> 4));
                carry = v & 0x0F;
                v = nv;
            }
            // Each nibble's inhibit is its suppress bit XOR (FORE ONLY and a zero source pixel).
            uint8_t k = keep;
            if (control & FORE_ONLY) {
                if (!(v & 0xF0)) k ^= 0xF0;
                if (!(v & 0x0F)) k ^= 0x0F;
            }
            const uint8_t data = (control & SOLID) ? solid : v;
            write(d, data, k);
            s = uint16_t(s + sstep);
            d = uint16_t(d + dstep);
        }
        src = (sstep == 1) ? s : uint16_t(src + 1);
        dst = (dstep == 1) ? d : uint16_t(dst + 1);
    }
    return w * h * ((control & SLOW) ? 2 : 1) + kHaltOverhead;
}

}  // namespace williams
