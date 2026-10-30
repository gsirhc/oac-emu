// Donkey Kong sound, TKG4-11-CPU: MB8884 (8035) at 6 MHz driving a DAC-08,
// plus the walk, jump and boom circuits (DKONG_REVIEW.md §3).

#ifndef DKONG_SOUND_H
#define DKONG_SOUND_H

#include "cpu_mcs48.h"

#include <array>
#include <cstdint>

namespace dkong {

// NE556 half: astable from Ra, Rb, C with an external CV network.
struct Astable {
    double ra = 0, rb = 0, c = 0;
    double vc = 0;     // timing capacitor
    bool out = true;
    void step(double dt, double vcv);
};

// Trigger -> coupling cap -> NPN that dumps an envelope capacitor
// (Q2/C21/C20 boom, Q5/C18/C17 jump).
struct Envelope {
    double r_src = 0, r_base = 0, c_in = 0, r_pull = 0, r_dis = 0, c_env = 0;
    double v_in = 0;       // coupling cap
    double v_env = 5.0;    // envelope cap, idles charged
    bool on = false;
    void step(double dt, bool trigger);
};

// Emitter follower -> R -> C to ground -> R -> R to ground, into the 47K mix.
struct OutFilter {
    double r_ser = 0, c = 0, r_out = 0, r_gnd = 0;
    double v = 0;
    double step(double dt, double ve);
};

class Sound {
public:
    mcs48::Cpu cpu;

    Sound();
    void reset();
    void load(const std::array<uint8_t, 0x800>& prog, const std::array<uint8_t, 0x800>& data);
    void advance(int z80_t_states);
    float sample() const { return out_; }
    void set_command(uint8_t v) { command_ = v & 0x0F; }
    void set_triggers(uint8_t v) { triggers_ = v; }
    void set_irq(bool v) { irq_ = v; }
    uint8_t dac() const { return p1_; }
    uint8_t p2() const { return p2_; }
    double walk_out() const { return v_walk_; }
    double jump_out() const { return v_jump_; }
    double boom_out() const { return v_boom_; }
    double dac_out() const { return v_dac_; }
    double dac_ref() const { return v_c32_; }

private:
    std::array<uint8_t, 0x800> prog_{};
    std::array<uint8_t, 0x800> data_{};
    int64_t mc_acc_ = 0;
    int analog_acc_ = 0;
    int lfsr_acc_ = 0;
    uint8_t p1_ = 0xFF;
    uint8_t p2_ = 0xFF;
    uint8_t command_ = 0;
    uint8_t triggers_ = 0;
    bool irq_ = false;

    // 6K/5K/4K LS164 noise and the 3J LS161 behind it.
    uint32_t lfsr_ = 0;
    uint8_t div_ = 0;

    Astable osc_a_{47e3, 27e3, 0.033e-6};  // 8N half 1: R42, R43, C28
    Astable osc_b_{47e3, 27e3, 0.047e-6};  // 8N half 2: R40, R41, C27
    Envelope env_boom_{10e3, 10e3, 1e-6, 100e3, 10e3, 3.3e-6};  // R10 R9 C21 R8 R7 C20
    Envelope env_jump_{10e3, 10e3, 1e-6, 100e3, 10e3, 4.7e-6};  // R32 R31 C18 R30 R29 C17
    OutFilter out_boom_{750, 1e-6, 2e3, 5.1e3};    // R5 C19 R4 R3
    OutFilter out_jump_{150, 1e-6, 2e3, 5.1e3};    // R27 C16 R26 R25
    double v_cv1_ = 10.0 / 3.0;  // C29
    double v_cv2_ = 10.0 / 3.0;  // C24
    double v_lfo1_ = 0;          // 8L 9/10/11/12 astable, C30
    bool lfo1_hi_ = false;
    double v_lfo2_ = 0;          // 8L x3 astable, C26
    bool lfo2_hi_ = false;
    double v_c25_ = 0, v_c23_ = 0, v_d_ = 0;  // walk network
    double v_c32_ = 0;                        // DAC reference, C32
    double sk_y_ = 0, sk_u_ = 0;              // Sallen-Key states
    double mix_lp_ = 0, mix_hp_ = 0, mix_hp_in_ = 0, spk_hp_ = 0, spk_in_ = 0;
    double v_walk_ = 0, v_jump_ = 0, v_boom_ = 0, v_dac_ = 0;
    float out_ = 0;

    mcs48::Bus make_bus();
    void analog(double dt);
};

}  // namespace dkong

#endif
