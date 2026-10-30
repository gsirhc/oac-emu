#include "sound.h"

#include <algorithm>
#include <cmath>

namespace dkong {

namespace {

// 6 MHz / 15 per 8035 machine cycle against the 3.072 MHz Z80.
constexpr int64_t kMcNum = 400000;
constexpr int64_t kMcDen = 3072000;
constexpr int kAnalogCycles = 32;  // analog step, 96 kHz
constexpr double kDt = kAnalogCycles / 3072000.0;
// LS164 clock is 2VF inverted: one shift every four lines.
constexpr int kLfsrCycles = 4 * 192;

constexpr double kVcc = 5.0;
constexpr double kTtlHi = 3.4, kTtlLo = 0.2;
constexpr double kTimerHi = kVcc - 1.7, kTimerLo = 0.1;  // NE556 output swing
constexpr double kVbe = 0.6, kVd = 0.6;
constexpr double kBeta = 200;
// NE556 CV pin: internal 5K/5K/5K divider, Thevenin 2/3 Vcc through 3.33K.
constexpr double kCvR = 5e3 * 10e3 / 15e3;
constexpr double kCvV = kVcc * 2.0 / 3.0;

double settle(double v, double target, double tau, double dt) {
    return target + (v - target) * std::exp(-dt / tau);
}

}  // namespace

void Astable::step(double dt, double vcv) {
    const double hi = std::clamp(vcv, 0.5, kVcc - 0.2);
    const double lo = hi / 2;
    while (dt > 0) {
        if (out) {
            const double tau = (ra + rb) * c;
            if (vc >= hi) { out = false; continue; }
            const double t = tau * std::log((kVcc - vc) / (kVcc - hi));
            if (t > dt) { vc = settle(vc, kVcc, tau, dt); return; }
            vc = hi;
            dt -= t;
            out = false;
        } else {
            const double tau = rb * c;
            if (vc <= lo) { out = true; continue; }
            const double t = tau * std::log(vc / lo);
            if (t > dt) { vc = settle(vc, 0, tau, dt); return; }
            vc = lo;
            dt -= t;
            out = true;
        }
    }
}

void Envelope::step(double dt, bool trigger) {
    // LS05 open collector: off lets the pull-up lift the node, on sinks it.
    const double vt = trigger ? kVcc : kTtlLo;
    double ib = 0;
    if (vt - v_in > kVbe) {
        ib = (vt - v_in - kVbe) / (r_src + r_base);
        v_in += ib * dt / c_in;
    }
    // A falling trigger dumps the coupling cap through the base clamp diode.
    v_in = std::min(v_in, vt + kVd);
    on = ib * kBeta > v_env / r_dis + kVcc / r_pull;
    v_env = on ? settle(v_env, 0, r_dis * c_env, dt) : settle(v_env, kVcc, (r_pull + r_dis) * c_env, dt);
}

double OutFilter::step(double dt, double ve) {
    const double rg = r_gnd * 47e3 / (r_gnd + 47e3);
    const double g = 1 / r_ser + 1 / (r_out + rg);
    v = settle(v, (ve / r_ser) / g, c / g, dt);
    return v * rg / (r_out + rg);
}

Sound::Sound() : cpu(make_bus()) { reset(); }

mcs48::Bus Sound::make_bus() {
    mcs48::Bus b;
    b.read_program = [this](uint16_t a) { return prog_[a & 0x7FF]; };
    // P26 high: LS367 4E puts the inverted 3D latch on DB0-3 (DB4-7 float
    // high). P26 low: 2716 3F, A8-A10 from P20-P22.
    b.read_data_ext = [this](uint8_t a) {
        if (p2_ & 0x40) return uint8_t(0xF0 | (~command_ & 0x0F));
        return data_[((p2_ & 7u) << 8) | a];
    };
    b.write_data_ext = [](uint8_t, uint8_t) {};
    b.read_bus = [this] {
        if (p2_ & 0x40) return uint8_t(0xF0 | (~command_ & 0x0F));
        return data_[(p2_ & 7u) << 8];
    };
    b.read_p1 = [this] { return p1_; };
    b.write_p1 = [this](uint8_t v) { p1_ = v; };
    // 6J pulls P25 low while $7D03 is set.
    b.read_p2 = [this] { return uint8_t(p2_ & ((triggers_ & 0x08) ? ~0x20 : 0xFF)); };
    b.write_p2 = [this](uint8_t v) { p2_ = v; };
    b.read_t0 = [this] { return !(triggers_ & 0x20); };  // $7D05 through 6J
    b.read_t1 = [this] { return !(triggers_ & 0x10); };  // $7D04 through 6J
    b.int_asserted = [this] { return irq_; };            // $7D80 through 5J
    return b;
}

void Sound::reset() {
    mc_acc_ = 0;
    analog_acc_ = 0;
    lfsr_acc_ = 0;
    command_ = 0;
    triggers_ = 0;
    irq_ = false;
    p1_ = 0xFF;
    p2_ = 0xFF;
    lfsr_ = 0;
    div_ = 0;
    cpu.reset();
}

void Sound::load(const std::array<uint8_t, 0x800>& prog, const std::array<uint8_t, 0x800>& data) {
    prog_ = prog;
    data_ = data;
}

void Sound::advance(int t) {
    mc_acc_ += int64_t(t) * kMcNum;
    while (mc_acc_ >= kMcDen) mc_acc_ -= int64_t(cpu.step()) * kMcDen;

    lfsr_acc_ += t;
    while (lfsr_acc_ >= kLfsrCycles) {
        lfsr_acc_ -= kLfsrCycles;
        // 3K XOR of 5K QC and 4K QH, inverted by 6L into 6K A/B.
        const unsigned x0 = ((lfsr_ >> 10) ^ (lfsr_ >> 23)) & 1;
        lfsr_ = ((lfsr_ << 1) | (x0 ^ 1)) & 0xFFFFFF;
        const unsigned x1 = ((lfsr_ >> 10) ^ (lfsr_ >> 23)) & 1;
        if (!x0 && x1) div_ = uint8_t((div_ + 1) & 15);  // 3J counts XOR rises
    }

    analog_acc_ += t;
    while (analog_acc_ >= kAnalogCycles) {
        analog_acc_ -= kAnalogCycles;
        analog(kDt);
    }
}

void Sound::analog(double dt) {
    // 8L 9-12 astable, R47/R48 43K and C30 10u: input node swings ±Vcc/2
    // past the threshold and relaxes through 43K.
    {
        const double tau = 43e3 * 10e-6;
        v_lfo1_ = settle(v_lfo1_, lfo1_hi_ ? 0 : kVcc, tau, dt);
        if (lfo1_hi_ && v_lfo1_ <= kVcc / 2) { lfo1_hi_ = false; v_lfo1_ -= kVcc; }
        else if (!lfo1_hi_ && v_lfo1_ >= kVcc / 2) { lfo1_hi_ = true; v_lfo1_ += kVcc; }
    }
    // 8L x3 astable, R38 18K / C26 3.3u (R39 3.3M only isolates the input).
    {
        const double tau = 18e3 * 3.3e-6;
        v_lfo2_ = settle(v_lfo2_, lfo2_hi_ ? 0 : kVcc, tau, dt);
        if (lfo2_hi_ && v_lfo2_ <= kVcc / 2) { lfo2_hi_ = false; v_lfo2_ -= kVcc; }
        else if (!lfo2_hi_ && v_lfo2_ >= kVcc / 2) { lfo2_hi_ = true; v_lfo2_ += kVcc; }
    }

    const bool walk = triggers_ & 0x01;
    const bool jump = triggers_ & 0x02;
    const bool boom = triggers_ & 0x04;

    // CV1: C29 at the pin, R44 1.2K to R46 12K (8L pin 9) and R45 10K (P24 via 5J).
    {
        const double vp24 = (p2_ & 0x10) ? kTtlLo : kTtlHi;
        const double gn = 1 / 12e3 + 1 / 10e3;
        const double vn = (v_lfo1_ / 12e3 + vp24 / 10e3) / gn;
        const double rn = 1.2e3 + 1 / gn;
        const double g = 1 / kCvR + 1 / rn;
        v_cv1_ = settle(v_cv1_, (kCvV / kCvR + vn / rn) / g, 3.3e-6 / g, dt);
    }
    // CV2: C24 at the pin, R49 1.2K to R51 10K (8L pin 2) and R50 10K (jump trigger).
    {
        const double vlfo = lfo2_hi_ ? kVcc : 0;
        const double vtrig = jump ? kVcc : kTtlLo;
        const double vw = (vlfo + vtrig) / 2;
        const double rw = 1.2e3 + 5e3;
        const double g = 1 / kCvR + 1 / rw;
        v_cv2_ = settle(v_cv2_, (kCvV / kCvR + vw / rw) / g, 10e-6 / g, dt);
    }
    osc_a_.step(dt, v_cv1_);
    osc_b_.step(dt, v_cv2_);

    // Walk: 6L pin 8 (R36 1K up) -> R18 4.7K -> C25 -> D5/R17 10K -> R35 1K,
    // shorted by Q6 while 8N pin 5 is high -> C23 -> R16 5.6K -> C22 || R15.
    {
        const double va = walk ? kVcc : kTtlLo;
        const bool q6 = osc_a_.out;
        const double r_path = 1e3 + 5.6e3;
        double g1, v1;
        if (q6) {
            g1 = 1 / 10e3 + 1 / 1e3;
            v1 = 0;
        } else {
            g1 = 1 / 10e3 + 1 / r_path;
            v1 = ((v_c23_ + v_d_) / r_path) / g1;
        }
        double vb = ((va - v_c25_) / 4.7e3 + v1 * g1) / (1 / 4.7e3 + g1);
        vb = std::max(vb, -kVd);
        v_c25_ += (va - v_c25_ - vb) / 4.7e3 * dt / 3.3e-6;
        const double i2 = q6 ? (0 - v_c23_ - v_d_) / 5.6e3 : (vb - v_c23_ - v_d_) / r_path;
        v_c23_ += i2 * dt / 4.7e-6;
        v_d_ += (i2 - v_d_ / 5.6e3) * dt / 0.047e-6;
        v_walk_ = v_d_;
    }

    // Boom: Q2 dumps C20; D1 or D2/D3 from 3J QC sets Q1's base.
    {
        env_boom_.step(dt, boom);
        const double noise = (div_ & 4) ? kTtlHi : kTtlLo;
        const double vbase = std::max(env_boom_.v_env - kVd, noise - 2 * kVd);
        v_boom_ = out_boom_.step(dt, std::max(vbase - kVbe, 0.0));
    }
    // Jump: Q5 dumps C17; D6 or D8/D7 from 8N pin 9 sets Q4's base.
    {
        env_jump_.step(dt, jump);
        const double tone = osc_b_.out ? kTimerHi : kTimerLo;
        const double vbase = std::max(env_jump_.v_env - kVd, tone - 2 * kVd);
        v_jump_ = out_jump_.step(dt, std::max(vbase - kVbe, 0.0));
    }

    // DAC-08: Iref from C32 through R20 10K. Q7 follows P27 and charges C32;
    // with P27 low C32 drains into the reference input.
    {
        v_c32_ = (p2_ & 0x80) ? settle(v_c32_, kVcc - kVbe, 0.5e-3, dt) : settle(v_c32_, 0, 10e3 * 10e-6, dt);
        const double iout = v_c32_ / 10e3 * (p1_ / 256.0);
        const double vin = iout * 5.6e3;  // MB3614, R23 5.6K
        // Sallen-Key: R21 = R22 = 5.6K, C33 0.022u feedback, C34 0.01u to ground.
        const double h = dt / 2;
        for (int k = 0; k < 2; k++) {
            const double du = ((vin - sk_y_ - sk_u_) / 5.6e3 - sk_u_ / 5.6e3) / 0.022e-6;
            const double dy = sk_u_ / 5.6e3 / 0.01e-6;
            sk_u_ += du * h;
            sk_y_ += dy * h;
        }
        v_dac_ = sk_y_;
    }

    // R1/R2/R14/R24 47K into Q3's base; C159 0.1u shunts ~780 ohms there,
    // C15 1u couples it, C8 220u feeds the 8 ohm speaker.
    const double mix = (v_walk_ + v_jump_ + v_boom_ + v_dac_) / 4;
    mix_lp_ = settle(mix_lp_, mix, 780 * 0.1e-6, dt);
    const double a_c15 = std::exp(-dt / (12.6e3 * 1e-6));
    mix_hp_ = a_c15 * (mix_hp_ + mix_lp_ - mix_hp_in_);
    mix_hp_in_ = mix_lp_;
    const double a_spk = std::exp(-dt / (8 * 220e-6));
    spk_hp_ = a_spk * (spk_hp_ + mix_hp_ - spk_in_);
    spk_in_ = mix_hp_;
    out_ = float(std::clamp(spk_hp_ * 1.2, -1.0, 1.0));
}

}  // namespace dkong
