// Williams Joust (1982): R-8731 CPU board, D-9144 ROM board, C-8783 interface, D-8224 sound (JOUST_REVIEW.md §1).

#ifndef JOUST_MACHINE_H
#define JOUST_MACHINE_H

#include "cpu_m6809.h"
#include "williams/blitter.h"
#include "williams/pia6821.h"
#include "williams/sound_board.h"

#include <array>
#include <cstdint>
#include <vector>

namespace joust {

// CR1 12 MHz, 7K/7F divide to E = 1 MHz.
constexpr int kCpuHz = 1000000;
// 64 E cycles per line (VA0-VA5), 260 lines: $FC-$FF then $00-$FF (5C-5F preload, 4D).
constexpr int kLineCycles = 64;
constexpr int kFrameLines = 260;
constexpr int kFrameCycles = kLineCycles * kFrameLines;
// 4B/5B blank counts 53..2; 6C blanks lines $F9..$00.
constexpr int kFirstVisibleCount = 3;
constexpr int kVisibleCounts = 50;
constexpr int kFirstVisibleLine = 0x01;
constexpr int kScreenW = kVisibleCounts * 6;
constexpr int kScreenH = 0xF8;

// Coin door into ROM PIA PA through 2A 4049s (D-9144); a closed switch reads 1.
// Coin bits follow the slot audit totals the ROM keeps at CMOS $107/$10D/$113.
enum CoinDoor : uint8_t {
    DOOR_AUTO_UP = 0x01,
    DOOR_ADVANCE = 0x02,
    DOOR_RIGHT_COIN = 0x04,
    DOOR_HS_RESET = 0x08,
    DOOR_LEFT_COIN = 0x10,
    DOOR_CENTER_COIN = 0x20,
    DOOR_SLAM = 0x40,
};

// Player panel through the C-8783 4049s and 74LS257s; a closed switch reads 1.
enum Panel : uint8_t {
    PANEL_LEFT = 0x01,
    PANEL_RIGHT = 0x02,
    PANEL_FLAP = 0x04,
    PANEL_2P_START = 0x10,
    PANEL_1P_START = 0x20,
};

struct Inputs {
    uint8_t door = 0;
    uint8_t p1 = 0;      // 3J3 stick and flap
    uint8_t p2 = 0;      // 3J2 stick and flap
    uint8_t starts = 0;  // 3J3-5/3J3-6, read on both mux halves
    // 1J2 memory protect: closed coin door write-protects CMOS $CC00-$CCFF.
    bool door_open = false;
};

struct RomSet {
    std::array<uint8_t, 0x9000> bank{};   // ROMs 1-9, $0000-$8FFF
    std::array<uint8_t, 0x3000> fixed{};  // ROMs 10-12, $D000-$FFFF
    std::array<uint8_t, 0x200> decoder4{};  // 3G, CPU address to DRAM page
    std::array<uint8_t, 0x200> decoder6{};  // 3C, video line to DRAM row
    std::array<uint8_t, 0x1000> sound{};
};

class Machine {
public:
    Inputs inputs;
    m6809::Cpu cpu;
    williams::Pia6821 widget_pia;  // $C804, interface board
    williams::Pia6821 rom_pia;     // $C80C, ROM board
    williams::Blitter blitter;
    williams::SoundBoard sound;

    // Three 16K banks of 4116s, indexed bank * $4000 + page * $100 + low byte.
    std::array<uint8_t, 0xC000> dram{};
    // 1C 5114, low nibble only; battery backed.
    std::array<uint8_t, 0x400> cmos{};
    std::array<uint8_t, 16> palette{};
    bool rom_enable = false;
    bool screen_control = false;
    int frames = 0;
    // Board E cycles, halted or not.
    uint64_t cycles = 0;
    int watchdog_resets = 0;
    std::vector<float> audio;
    int audio_hz = 48000;

    Machine();
    // Power-up: clears DRAM and chips, keeps CMOS.
    void reset();
    void load_roms(const RomSet& set);
    int run_cycles(int n);
    // 0x00RRGGBB, kScreenW x kScreenH, as the last full frame left it.
    void render(uint32_t* out) const;

    uint8_t mem_read(uint16_t addr);
    void mem_write(uint16_t addr, uint8_t v);
    // DRAM offset the 3G PROM gives a CPU address below $C000.
    int dram_index(uint16_t addr) const;
    int line() const;
    int frame_cycle() const { return frame_cycle_; }

private:
    RomSet roms_;
    std::array<uint32_t, kScreenW * kScreenH> frame_{};
    int frame_cycle_ = 0;
    int stall_ = 0;
    int watchdog_ = 0;
    bool release_pending_ = false;
    double audio_acc_ = 0;

    m6809::Bus make_bus();
    void hard_reset();
    void tick(int t);
    void line_start(int line_value);
    void render_line(int line_value);
    void update_irq();
    void blit_write(uint16_t addr, uint8_t v, uint8_t inhibit);
    uint8_t widget_inputs() const;
};

}  // namespace joust

#endif
