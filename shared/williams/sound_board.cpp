#include "sound_board.h"

namespace williams {

namespace {

// C14 1000 uF into the 8 ohm speaker sets the low corner.
constexpr double kCouplingHz = 1.0 / (2.0 * 3.14159265358979 * 1000e-6 * 8.0);

}  // namespace

SoundBoard::SoundBoard()
    : cpu(m6800::Bus{[this](uint16_t a) { return read(a); },
                     [this](uint16_t a, uint8_t v) { write(a, v); }}) {
    // PB0-PB5 are the select lines; W12/W13 leave PB6/PB7 high.
    pia.in_b = [this] { return uint8_t(select_ | 0xC0); };
    pia.in_a = [] { return uint8_t(0xFF); };
    reset();
}

void SoundBoard::reset() {
    ram.fill(0);
    pia.reset();
    pia.set_cb1(select_ != 0x3F);
    cpu.reset();
    cpu.set_irq(false);
    debt_ = 0;
    hp_in_ = hp_out_ = 0;
}

// IC6 4068 NANDs the select lines into CB1: any low line raises it.
void SoundBoard::set_select(uint8_t lines) {
    select_ = uint8_t(lines & 0x3F);
    pia.set_cb1(select_ != 0x3F);
    cpu.set_irq(pia.irq_a() || pia.irq_b());
}

// IC2 7442 on A12-A14: $Fxxx is the ROM; in $0xxx A10 picks the PIA, otherwise the 6810.
uint8_t SoundBoard::read(uint16_t addr) {
    if ((addr & 0xF000) == 0xF000) return rom[addr & 0x0FFF];
    if ((addr & 0xF000) == 0x0000) {
        if (addr & 0x0400) {
            const uint8_t v = pia.read(addr & 3);
            cpu.set_irq(pia.irq_a() || pia.irq_b());
            return v;
        }
        if (!(addr & 0x0080)) return ram[addr & 0x7F];
    }
    return 0xFF;
}

void SoundBoard::write(uint16_t addr, uint8_t v) {
    if ((addr & 0xF000) != 0x0000) return;
    if (addr & 0x0400) {
        pia.write(addr & 3, v);
        cpu.set_irq(pia.irq_a() || pia.irq_b());
        return;
    }
    if (!(addr & 0x0080)) ram[addr & 0x7F] = v;
}

void SoundBoard::advance(int main_cycles, double main_hz) {
    debt_ += main_cycles * (kSoundCpuHz / main_hz);
    while (debt_ > 0) {
        debt_ -= cpu.step();
        pia.clock_e();
    }
}

// MC1408: output current is linear in PA, PA7 the MSB (IC13 pin 5 = A1).
float SoundBoard::sample(int out_hz) {
    if (out_hz != sample_hz_) {
        sample_hz_ = out_hz;
        hp_coeff_ = 1.0 / (1.0 + 2.0 * 3.14159265358979 * kCouplingHz / out_hz);
    }
    const float in = (float(dac()) - 128.0f) / 128.0f;
    hp_out_ = float(hp_coeff_ * (hp_out_ + in - hp_in_));
    hp_in_ = in;
    return hp_out_;
}

}  // namespace williams
