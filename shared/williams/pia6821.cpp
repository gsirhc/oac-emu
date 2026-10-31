#include "pia6821.h"

namespace williams {

void Pia6821::reset() {
    ora_ = orb_ = ddra_ = ddrb_ = cra_ = crb_ = 0;
    ca2_restore_e_ = cb2_restore_e_ = false;
    drive_c2(ca2_out_, true, out_ca2);
    drive_c2(cb2_out_, true, out_cb2);
    if (out_a) out_a(port_a());
    if (out_b) out_b(port_b());
}

// b0 enables the pin, b7 is the CA1/CB1 flag, b6 the CA2/CB2 flag while C2 is an input.
bool Pia6821::irq_a() const {
    return ((cra_ & 0x81) == 0x81) || ((cra_ & 0x68) == 0x48);
}

bool Pia6821::irq_b() const {
    return ((crb_ & 0x81) == 0x81) || ((crb_ & 0x68) == 0x48);
}

// b1 = 1 sets the flag on a low-to-high transition, 0 on high-to-low.
bool Pia6821::c1_edge(uint8_t cr, bool from, bool to) {
    if (from == to) return false;
    return (cr & 0x02) ? to : !to;
}

bool Pia6821::c2_edge(uint8_t cr, bool from, bool to) {
    if (cr & 0x20 || from == to) return false;
    return (cr & 0x10) ? to : !to;
}

void Pia6821::drive_c2(bool& c2_out, bool level, const std::function<void(bool)>& cb) {
    c2_out = level;
    if (cb) cb(level);
}

void Pia6821::write_cr(uint8_t& cr, uint8_t v, bool& c2_out, bool& restore_e,
                       const std::function<void(bool)>& cb) {
    cr = uint8_t((cr & 0xC0) | (v & 0x3F));
    if (cr & 0x20) {
        cr = uint8_t(cr & ~0x40);
        restore_e = false;
        // b4 = 1: C2 follows b3. b4 = 0: strobe mode idles high.
        drive_c2(c2_out, (cr & 0x10) ? (cr & 0x08) != 0 : true, cb);
    }
}

uint8_t Pia6821::read(int rs) {
    switch (rs & 3) {
        case 0:
            if (!(cra_ & 0x04)) return ddra_;
            {
                const uint8_t v = port_a();
                cra_ = uint8_t(cra_ & 0x3F);
                // Read strobe: CA2 drops after a read of the A data register.
                if ((cra_ & 0x30) == 0x20) {
                    drive_c2(ca2_out_, false, out_ca2);
                    ca2_restore_e_ = (cra_ & 0x08) != 0;
                }
                return v;
            }
        case 1: return cra_;
        case 2:
            if (!(crb_ & 0x04)) return ddrb_;
            {
                const uint8_t v = port_b();
                crb_ = uint8_t(crb_ & 0x3F);
                return v;
            }
        default: return crb_;
    }
}

void Pia6821::write(int rs, uint8_t v) {
    switch (rs & 3) {
        case 0:
            if (cra_ & 0x04) ora_ = v;
            else ddra_ = v;
            if (out_a) out_a(port_a());
            break;
        case 1: write_cr(cra_, v, ca2_out_, ca2_restore_e_, out_ca2); break;
        case 2:
            if (crb_ & 0x04) {
                orb_ = v;
                // Write strobe: CB2 drops after a write of the B data register.
                if ((crb_ & 0x30) == 0x20) {
                    drive_c2(cb2_out_, false, out_cb2);
                    cb2_restore_e_ = (crb_ & 0x08) != 0;
                }
            } else {
                ddrb_ = v;
            }
            if (out_b) out_b(port_b());
            break;
        default: write_cr(crb_, v, cb2_out_, cb2_restore_e_, out_cb2); break;
    }
}

void Pia6821::set_ca1(bool level) {
    if (c1_edge(cra_, ca1_, level)) {
        cra_ = uint8_t(cra_ | 0x80);
        if ((cra_ & 0x38) == 0x20 && !ca2_out_) drive_c2(ca2_out_, true, out_ca2);
    }
    ca1_ = level;
}

void Pia6821::set_cb1(bool level) {
    if (c1_edge(crb_, cb1_, level)) {
        crb_ = uint8_t(crb_ | 0x80);
        if ((crb_ & 0x38) == 0x20 && !cb2_out_) drive_c2(cb2_out_, true, out_cb2);
    }
    cb1_ = level;
}

void Pia6821::set_ca2(bool level) {
    if (c2_edge(cra_, ca2_in_, level)) cra_ = uint8_t(cra_ | 0x40);
    ca2_in_ = level;
}

void Pia6821::set_cb2(bool level) {
    if (c2_edge(crb_, cb2_in_, level)) crb_ = uint8_t(crb_ | 0x40);
    cb2_in_ = level;
}

void Pia6821::clock_e() {
    if (ca2_restore_e_ && !ca2_out_) {
        ca2_restore_e_ = false;
        drive_c2(ca2_out_, true, out_ca2);
    }
    if (cb2_restore_e_ && !cb2_out_) {
        cb2_restore_e_ = false;
        drive_c2(cb2_out_, true, out_cb2);
    }
}

}  // namespace williams
