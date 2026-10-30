// Nintendo Donkey Kong (1981), TKG4-11/14-CPU + TKG4-14-VIDEO (DKONG_REVIEW.md §4).

#ifndef DKONG_MACHINE_H
#define DKONG_MACHINE_H

#include "cpu_z80.h"
#include "sound.h"
#include "video.h"

#include <array>
#include <cstdint>
#include <vector>

namespace dkong {

constexpr int kCpuHz = 3072000;  // 61.44 MHz / 20

struct Inputs {
    uint8_t dip = 0x80;  // SW1 A..H on D0..D7 through LS240 4N, ON = 1
    uint8_t in0 = 0x00;  // 2P: D0 right, D1 left, D2 up, D3 down, D4 jump
    uint8_t in1 = 0x00;  // 4P: second player, same layout
    uint8_t in2 = 0x00;  // 3P: D2 1P start, D3 2P start (D7 is the coin latch)
    bool coin = false;   // P3 coin switch, closed = true
    bool service = false;
};

struct RomSet {
    std::array<uint8_t, 0x4000> program{};  // 5E, 5C, 5B, 5A (2532)
    VideoRoms video;
    std::array<uint8_t, 0x800> sound_prog{};  // 3H (2716)
    std::array<uint8_t, 0x800> sound_data{};  // 3F (2716)
};

class Machine {
public:
    Inputs inputs;
    z80::Cpu cpu;
    Video video;
    Sound sound;
    std::array<uint8_t, 0x4000> program{};
    std::array<uint8_t, 0x1000> work_ram{};  // $6000-$6FFF
    uint8_t latch_5h = 0;  // $7D80-$7D87
    uint8_t latch_6h = 0;  // $7D00-$7D07
    uint8_t latch_3d = 0;  // $7C00, 4 bits
    int frames = 0;
    std::vector<float> audio;
    int audio_hz = 48000;

    Machine();
    void reset();
    void load_roms(const RomSet& set);
    int run_cycles(int n);
    void render(uint32_t* upright) const { video.render(upright); }

    uint8_t mem_read(uint16_t addr);
    void mem_write(uint16_t addr, uint8_t v);
    bool nmi_enabled() const { return latch_5h & 0x10; }
    bool coin_latched() const { return coin_timer_ > 0 || inputs.service; }

private:
    z80::Bus make_bus();
    void tick(int t);
    void latch_write(uint8_t& latch, int bit, bool v);
    void dma_request();

    double audio_acc_ = 0;
    int coin_timer_ = 0;
    bool coin_prev_ = false;
    int dma_stall_ = 0;

    // Intel 8257-5. Both DRQs come from $7D85.
    uint8_t dma_mode_ = 0;
    uint16_t dma_addr_[4]{};
    uint16_t dma_tc_[4]{};
    bool dma_ff_ = false;
};

}  // namespace dkong

#endif
