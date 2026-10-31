// Motorola MC6821 PIA (MC6821 data sheet DS9435-R3, 1983).

#ifndef SHARED_WILLIAMS_PIA6821_H
#define SHARED_WILLIAMS_PIA6821_H

#include <cstdint>
#include <functional>

namespace williams {

class Pia6821 {
public:
    // Pin levels the board drives into each port when a bit is an input.
    std::function<uint8_t()> in_a;
    std::function<uint8_t()> in_b;
    // Called whenever a port's output levels or CA2/CB2 output change.
    std::function<void(uint8_t)> out_a;
    std::function<void(uint8_t)> out_b;
    std::function<void(bool)> out_ca2;
    std::function<void(bool)> out_cb2;

    void reset();
    // RS1:RS0 register select (Table 1).
    uint8_t read(int rs);
    void write(int rs, uint8_t v);

    void set_ca1(bool level);
    void set_cb1(bool level);
    void set_ca2(bool level);
    void set_cb2(bool level);
    // One high-to-low E edge, for the strobe modes that restore on E.
    void clock_e();

    bool irq_a() const;
    bool irq_b() const;
    uint8_t port_a() const { return uint8_t((ora_ & ddra_) | (~ddra_ & pins_a())); }
    uint8_t port_b() const { return uint8_t((orb_ & ddrb_) | (~ddrb_ & pins_b())); }
    bool ca2() const { return ca2_out_; }
    bool cb2() const { return cb2_out_; }
    uint8_t cra() const { return cra_; }
    uint8_t crb() const { return crb_; }

private:
    uint8_t ora_ = 0, orb_ = 0, ddra_ = 0, ddrb_ = 0, cra_ = 0, crb_ = 0;
    bool ca1_ = false, cb1_ = false, ca2_in_ = false, cb2_in_ = false;
    bool ca2_out_ = true, cb2_out_ = true;
    bool ca2_restore_e_ = false, cb2_restore_e_ = false;

    uint8_t pins_a() const { return in_a ? in_a() : 0xFF; }
    uint8_t pins_b() const { return in_b ? in_b() : 0xFF; }
    void write_cr(uint8_t& cr, uint8_t v, bool& c2_out, bool& restore_e, const std::function<void(bool)>& cb);
    static bool c1_edge(uint8_t cr, bool from, bool to);
    static bool c2_edge(uint8_t cr, bool from, bool to);
    void drive_c2(bool& c2_out, bool level, const std::function<void(bool)>& cb);
};

}  // namespace williams

#endif
