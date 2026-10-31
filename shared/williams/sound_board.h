// Williams D-8224 sound board: MC6808, MC6821, MCM6810 RAM, 4K ROM, MC1408 DAC (Joust drawing set).

#ifndef SHARED_WILLIAMS_SOUND_BOARD_H
#define SHARED_WILLIAMS_SOUND_BOARD_H

#include "cpu_m6800.h"
#include "pia6821.h"

#include <array>
#include <cstdint>

namespace williams {

// Y1 3.58 MHz; the 6808 divides its crystal by four.
constexpr double kSoundCpuHz = 3579545.0 / 4.0;

class SoundBoard {
public:
    m6800::Cpu cpu;
    Pia6821 pia;
    std::array<uint8_t, 0x80> ram{};
    std::array<uint8_t, 0x1000> rom{};

    SoundBoard();
    void reset();
    // Sound select lines J3 into PB0-PB5; idle is all high.
    void set_select(uint8_t lines);
    // Advance by main-board E cycles (1 MHz) and run the 6808 to match.
    void advance(int main_cycles, double main_hz);
    uint8_t dac() const { return pia.port_a(); }
    // DAC level after the coupling capacitors, about -1..1.
    float sample(int out_hz);

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t v);

private:
    uint8_t select_ = 0x3F;
    double debt_ = 0;
    float hp_in_ = 0, hp_out_ = 0;
    double hp_coeff_ = 0;
    int sample_hz_ = 0;
};

}  // namespace williams

#endif
