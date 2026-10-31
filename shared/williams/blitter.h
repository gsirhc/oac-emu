// Williams Special Chip pair (VTI VL2001 "SC1"), from Sean Riddle's real-hardware blit tests.

#ifndef SHARED_WILLIAMS_BLITTER_H
#define SHARED_WILLIAMS_BLITTER_H

#include <array>
#include <cstdint>
#include <functional>

namespace williams {

class Blitter {
public:
    enum Control : uint8_t {
        SRC_STRIDE_256 = 0x01,
        DST_STRIDE_256 = 0x02,
        SLOW = 0x04,
        FORE_ONLY = 0x08,
        SOLID = 0x10,
        SHIFT = 0x20,
        NO_LOWER = 0x40,
        NO_UPPER = 0x80,
    };

    // The chips own the CPU bus while they run: reads see the ROM bank, writes reach I/O.
    std::function<uint8_t(uint16_t)> read;
    // inhibit holds /WINHU (0xF0) and /WINHL (0x0F): DRAM keeps those bits.
    std::function<void(uint16_t, uint8_t value, uint8_t inhibit)> write;
    // SC1 inverts bit 2 of width and height; SC2 does not.
    bool sc1 = true;

    // $CA01-$CA07; the registers survive reset (Riddle).
    void write_reg(int reg, uint8_t v) { regs_[reg & 7] = v; }
    uint8_t reg(int reg) const { return regs_[reg & 7]; }
    // A $CA00 write: runs the whole blit and returns the E cycles the CPU is halted.
    int start(uint8_t control);

private:
    std::array<uint8_t, 8> regs_{};
};

}  // namespace williams

#endif
