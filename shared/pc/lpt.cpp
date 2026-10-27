#include "lpt.h"

namespace pc {

void Lpt::reset() {
    data_ = 0;
    control_ = 0xC4;
    strobe_ = false;
    busy_ = false;
    ack_low_ = false;
    irq_ = false;
    busy_until_ = 0;
    ack_until_ = 0;
    now_ = 0;
    hz_ = 0;
    capture_.clear();
}

uint8_t Lpt::status() const {
    // Bit 7 = /BUSY (1 = not busy), 6 = /ACK, 5 = PE, 4 = SELECT, 3 = /ERROR.
    // A selected, online printer with no paper error: PE=0, SELECT=1, /ERROR=1.
    uint8_t v = 0x18;  // SELECT | /ERROR
    if (!busy_) v = uint8_t(v | 0x80);
    if (!ack_low_) v = uint8_t(v | 0x40);
    return v;
}

void Lpt::tick(uint64_t cpu_cycles, double cpu_hz) {
    now_ = cpu_cycles;
    hz_ = cpu_hz;
    if (busy_ && now_ >= busy_until_) {
        busy_ = false;
    }
    if (ack_low_ && now_ >= ack_until_) {
        ack_low_ = false;
    }
}

uint8_t Lpt::in(uint16_t port) {
    switch (port - base_) {
        case 0:
            return data_;
        case 1:
            return status();
        case 2:
            return uint8_t(control_ | 0xC0);
        default:
            return 0xFF;
    }
}

void Lpt::out(uint16_t port, uint8_t v) {
    switch (port - base_) {
        case 0:
            data_ = v;
            return;
        case 1:
            return;  // status is read-only
        case 2: {
            // Bit 2 is /INIT (active low). A low pulse resets the printer path.
            bool init_low = (v & 0x04) == 0;
            bool was_init_low = (control_ & 0x04) == 0;
            if (init_low && !was_init_low) {
                capture_.clear();
                busy_ = false;
                ack_low_ = false;
                irq_ = false;
            }
            bool new_strobe = (v & 0x01) != 0;
            // STROBE bit 0 is inverted onto the cable (IBM PC/AT Tech Ref). Software
            // pulses 0→1→0. Capture on the rising edge. /ACK is asserted immediately
            // so BIOS INT 17h (which polls bit 6 once after the pulse) succeeds;
            // BUSY still holds for one character time and does not block the next byte.
            if (new_strobe && !strobe_) {
                capture_.push_back(data_);
                // Hold /ACK through the busy window. BIOS-bochs-legacy INT 17h
                // samples bit 6 once after helper calls around the strobe, so a
                // 5 µs pulse is gone before it looks.
                ack_low_ = true;
                if (control_ & 0x10) irq_ = true;
                if (cps_ > 0 && hz_ > 0) {
                    busy_ = true;
                    busy_until_ = now_ + uint64_t(hz_ / double(cps_));
                    ack_until_ = busy_until_;
                } else {
                    ack_until_ = now_ + 1;
                }
            }
            strobe_ = new_strobe;
            control_ = uint8_t((v & 0x3F) | 0xC0);
            if ((control_ & 0x10) == 0) irq_ = false;
            return;
        }
        default:
            return;
    }
}

}  // namespace pc
