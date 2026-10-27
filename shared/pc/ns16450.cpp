#include "ns16450.h"

namespace pc {

void Ns16450::reset() {
    ier_ = 0;
    iir_ = 0x01;
    lcr_ = 0;
    mcr_ = 0;
    lsr_ = 0x60;
    msr_ = 0;
    scratch_ = 0;
    thr_ = 0;
    rbr_ = 0;
    divisor_ = 1;
    rbr_full_ = false;
    thre_irq_shadow_ = true;
    rx_q_.clear();
    bit_credit_ = 0;
    prev_cycles_ = 0;
    ratio_hz_ = 0;
    bits_per_cpu_cycle_ = 0;
    rx_bits_left_ = 0;
    pending_rx_ = 0;
}

int Ns16450::frame_bits() const {
    int data = 5 + (lcr_ & 3);
    int parity = (lcr_ & 0x08) ? 1 : 0;
    int stop = (lcr_ & 0x04) ? 2 : 1;
    return 1 + data + parity + stop;
}

double Ns16450::baud() const {
    uint32_t div = divisor_ ? divisor_ : 65536u;
    return 1843200.0 / (16.0 * double(div));
}

void Ns16450::recompute_iir() {
    bool rls = (ier_ & 0x04) && (lsr_ & 0x1E);
    bool rda = (ier_ & 0x01) && rbr_full_;
    bool thre = (ier_ & 0x02) && thre_irq_shadow_ && (lsr_ & 0x20);
    bool ms = (ier_ & 0x08) && (msr_ & 0x0F);
    if (rls) {
        iir_ = 0x06;
    } else if (rda) {
        iir_ = 0x04;
    } else if (thre) {
        iir_ = 0x02;
    } else if (ms) {
        iir_ = 0x00;
    } else {
        iir_ = 0x01;
    }
}

bool Ns16450::irq_pending() const {
    return (mcr_ & 0x08) != 0 && (iir_ & 0x01) == 0;
}

void Ns16450::update_msr_from_mcr() {
    if (mcr_ & 0x10) {
        uint8_t next = 0;
        if (mcr_ & 0x01) next |= 0x10;
        if (mcr_ & 0x02) next |= 0x20;
        if (mcr_ & 0x04) next |= 0x40;
        if (mcr_ & 0x08) next |= 0x80;
        uint8_t delta = 0;
        if ((next & 0x10) != (msr_ & 0x10)) delta |= 0x02;
        if ((next & 0x20) != (msr_ & 0x20)) delta |= 0x01;
        if ((next & 0x40) != (msr_ & 0x40)) delta |= 0x04;
        if ((next & 0x80) != (msr_ & 0x80)) delta |= 0x08;
        msr_ = uint8_t((next & 0xF0) | delta);
    }
}

void Ns16450::accept_rx_byte(uint8_t b) {
    if (rbr_full_) lsr_ = uint8_t(lsr_ | 0x02);
    rbr_ = b;
    rbr_full_ = true;
    lsr_ = uint8_t(lsr_ | 0x01);
    recompute_iir();
}

void Ns16450::push_rx(uint8_t b) {
    if (mcr_ & 0x10) return;
    rx_q_.push_back(b);
}

void Ns16450::tick(uint64_t cpu_cycles, double cpu_hz) {
    uint64_t d = cpu_cycles - prev_cycles_;
    prev_cycles_ = cpu_cycles;
    if (cpu_hz != ratio_hz_ || bits_per_cpu_cycle_ == 0.0) {
        ratio_hz_ = cpu_hz;
        bits_per_cpu_cycle_ = (cpu_hz > 0) ? baud() / cpu_hz : 0;
    }
    bit_credit_ += double(d) * bits_per_cpu_cycle_;

    while (true) {
        if (rx_bits_left_ > 0) {
            if (bit_credit_ < 1.0) break;
            int take = int(bit_credit_);
            if (take > rx_bits_left_) take = rx_bits_left_;
            bit_credit_ -= take;
            rx_bits_left_ -= take;
            if (rx_bits_left_ == 0) accept_rx_byte(pending_rx_);
            continue;
        }
        if (rx_q_.empty()) {
            bit_credit_ = 0;  // idle line: no banked character times
            break;
        }
        pending_rx_ = rx_q_.front();
        rx_q_.pop_front();
        rx_bits_left_ = frame_bits();
    }
    recompute_iir();
}

uint8_t Ns16450::in(uint16_t port) {
    switch (port - base_) {
        case 0:
            if (dlab()) return uint8_t(divisor_ & 0xFF);
            rbr_full_ = false;
            lsr_ = uint8_t(lsr_ & ~uint8_t(0x01));
            recompute_iir();
            return rbr_;
        case 1:
            if (dlab()) return uint8_t(divisor_ >> 8);
            return ier_;
        case 2: {
            uint8_t v = iir_;
            if ((iir_ & 0x07) == 0x02) thre_irq_shadow_ = false;
            recompute_iir();
            return v;
        }
        case 3:
            return lcr_;
        case 4:
            return mcr_;
        case 5: {
            // OE/PE/FE/BI clear on LSR read (NS16450 data sheet).
            uint8_t v = lsr_;
            lsr_ = uint8_t(lsr_ & ~uint8_t(0x1E));
            recompute_iir();
            return v;
        }
        case 6: {
            uint8_t v = msr_;
            msr_ = uint8_t(msr_ & 0xF0);
            recompute_iir();
            return v;
        }
        case 7:
            return scratch_;
        default:
            return 0xFF;
    }
}

void Ns16450::out(uint16_t port, uint8_t v) {
    switch (port - base_) {
        case 0:
            if (dlab()) {
                divisor_ = uint16_t((divisor_ & 0xFF00) | v);
                bits_per_cpu_cycle_ = (ratio_hz_ > 0) ? baud() / ratio_hz_ : 0;
                return;
            }
            thr_ = v;
            lsr_ = uint8_t((lsr_ | 0x20) | 0x40);
            thre_irq_shadow_ = true;
            if (mcr_ & 0x10) accept_rx_byte(v);
            recompute_iir();
            return;
        case 1:
            if (dlab()) {
                divisor_ = uint16_t((divisor_ & 0x00FF) | (uint16_t(v) << 8));
                bits_per_cpu_cycle_ = (ratio_hz_ > 0) ? baud() / ratio_hz_ : 0;
                return;
            }
            ier_ = uint8_t(v & 0x0F);
            recompute_iir();
            return;
        case 2:
            return;
        case 3:
            lcr_ = v;
            return;
        case 4:
            mcr_ = uint8_t(v & 0x1F);
            update_msr_from_mcr();
            recompute_iir();
            return;
        case 5:
        case 6:
            return;
        case 7:
            scratch_ = v;
            return;
        default:
            return;
    }
}

}  // namespace pc
