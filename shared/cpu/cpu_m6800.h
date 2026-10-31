// Motorola MC6800 / MC6808 MPU (Williams sound board). M6800 Programming Reference Manual M68PRM(D), Nov 1976.

#ifndef SHARED_CPU_M6800_H
#define SHARED_CPU_M6800_H

#include <cstdint>
#include <functional>

namespace m6800 {

enum Flag : uint8_t {
    FLAG_C = 1 << 0,
    FLAG_V = 1 << 1,
    FLAG_Z = 1 << 2,
    FLAG_N = 1 << 3,
    FLAG_I = 1 << 4,
    FLAG_H = 1 << 5,
};

struct Bus {
    std::function<uint8_t(uint16_t addr)>         read;
    std::function<void(uint16_t addr, uint8_t v)> write;
};

class Cpu {
public:
    uint8_t  a = 0, b = 0;
    uint8_t  cc = FLAG_I;
    uint16_t x = 0, sp = 0, pc = 0;
    uint64_t cycles = 0;
    uint64_t undefined_ops = 0;

    explicit Cpu(Bus bus) : bus_(std::move(bus)) {}
    void rebind_bus(Bus bus) { bus_ = std::move(bus); }

    void reset();
    int step();

    // Active-high "asserted" for both pins; NMI triggers when it becomes asserted.
    void set_irq(bool level) { irq_ = level; }
    void set_nmi(bool level) {
        if (level && !nmi_level_) nmi_pending_ = true;
        nmi_level_ = level;
    }

    bool waiting() const { return waiting_; }
    bool flag(Flag f) const { return (cc & f) != 0; }

private:
    Bus bus_;
    bool irq_ = false;
    bool nmi_level_ = false;
    bool nmi_pending_ = false;
    bool waiting_ = false;

    uint8_t  rb(uint16_t addr)            { return bus_.read(addr); }
    void     wb(uint16_t addr, uint8_t v) { bus_.write(addr, v); }
    uint16_t rw(uint16_t addr) { return uint16_t(rb(addr) << 8) | rb(uint16_t(addr + 1)); }
    void     ww(uint16_t addr, uint16_t v) { wb(addr, uint8_t(v >> 8)); wb(uint16_t(addr + 1), uint8_t(v)); }
    uint8_t  fetch8() { return rb(pc++); }
    uint16_t fetch16() {
        uint16_t v = rw(pc);
        pc += 2;
        return v;
    }

    void    push8(uint8_t v) { wb(sp--, v); }
    uint8_t pull8()          { return rb(++sp); }

    void set_flag(Flag f, bool on) { cc = on ? uint8_t(cc | f) : uint8_t(cc & ~f); }
    void set_nz(uint8_t v) { set_flag(FLAG_Z, v == 0); set_flag(FLAG_N, (v & 0x80) != 0); }
    void set_nz16(uint16_t v) { set_flag(FLAG_Z, v == 0); set_flag(FLAG_N, (v & 0x8000) != 0); }

    void push_state();
    int service_interrupt(uint16_t vec);

    uint16_t ea(int mode);
    uint8_t add8(uint8_t l, uint8_t r, uint8_t carry);
    uint8_t sub8(uint8_t l, uint8_t r, uint8_t borrow);
    void logic_flags(uint8_t v);
    uint8_t unary(int kind, uint8_t v);
    void cpx(uint16_t m);
    void daa();
    bool branch_taken(uint8_t op) const;
    int exec_inherent(uint8_t op);
    int exec_unary(uint8_t op);
    int exec_binary(uint8_t op);
    int undefined();
};

}  // namespace m6800

#endif
