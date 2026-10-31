// Motorola MC6809E (Williams Defender/Joust family), MC6809-MC6809E Programming Manual (M6809PM/AD, 1981).

#ifndef SHARED_CPU_M6809_H
#define SHARED_CPU_M6809_H

#include <cstdint>
#include <functional>

namespace m6809 {

enum Flag : uint8_t {
    FLAG_C = 1 << 0,
    FLAG_V = 1 << 1,
    FLAG_Z = 1 << 2,
    FLAG_N = 1 << 3,
    FLAG_I = 1 << 4,
    FLAG_H = 1 << 5,
    FLAG_F = 1 << 6,
    FLAG_E = 1 << 7,
};

struct Bus {
    std::function<uint8_t(uint16_t addr)>         read;
    std::function<void(uint16_t addr, uint8_t v)> write;
};

class Cpu {
public:
    uint8_t  a = 0, b = 0, dp = 0;
    uint8_t  cc = FLAG_I | FLAG_F;
    uint16_t x = 0, y = 0, u = 0, s = 0, pc = 0;
    uint64_t cycles = 0;
    // Opcodes M6809PM leaves undefined; each runs as a 2-cycle no-op.
    uint64_t illegal_ops = 0;

    explicit Cpu(Bus bus) : bus_(std::move(bus)) {}
    void rebind_bus(Bus bus) { bus_ = std::move(bus); }

    void reset();
    // One instruction or one interrupt entry; returns E cycles.
    int step();

    void set_irq(bool on) { irq_ = on; }
    void set_firq(bool on) { firq_ = on; }
    void set_nmi(bool on);

    uint16_t d() const { return uint16_t((a << 8) | b); }
    void set_d(uint16_t v) { a = uint8_t(v >> 8); b = uint8_t(v); }
    bool flag(Flag f) const { return (cc & f) != 0; }
    bool waiting() const { return state_ != State::Run; }

private:
    enum class State { Run, Cwai, Sync };

    Bus bus_;
    State state_ = State::Run;
    bool irq_ = false;
    bool firq_ = false;
    bool nmi_line_ = false;
    bool nmi_pending_ = false;
    // NMI is ignored after reset until the program loads S (M6809PM 3.2).
    bool nmi_armed_ = false;

    uint8_t  rd(uint16_t addr) { return bus_.read(addr); }
    void     wr(uint16_t addr, uint8_t v) { bus_.write(addr, v); }
    uint16_t rd16(uint16_t addr) { return uint16_t((rd(addr) << 8) | rd(uint16_t(addr + 1))); }
    void     wr16(uint16_t addr, uint16_t v) {
        wr(addr, uint8_t(v >> 8));
        wr(uint16_t(addr + 1), uint8_t(v));
    }
    uint8_t  fetch8() { return rd(pc++); }
    uint16_t fetch16() {
        uint16_t v = rd16(pc);
        pc = uint16_t(pc + 2);
        return v;
    }

    void push_s8(uint8_t v) { wr(--s, v); }
    void push_s16(uint16_t v) { push_s8(uint8_t(v)); push_s8(uint8_t(v >> 8)); }
    uint8_t pull_s8() { return rd(s++); }
    uint16_t pull_s16() { uint16_t h = pull_s8(); return uint16_t((h << 8) | pull_s8()); }

    void set_flag(Flag f, bool on) { cc = on ? uint8_t(cc | f) : uint8_t(cc & ~f); }
    void nz8(uint8_t v) { set_flag(FLAG_N, v & 0x80); set_flag(FLAG_Z, v == 0); }
    void nz16(uint16_t v) { set_flag(FLAG_N, v & 0x8000); set_flag(FLAG_Z, v == 0); }

    int interrupts();
    void push_entire();
    void set_s(uint16_t v) { s = v; nmi_armed_ = true; }

    uint16_t ea_direct() { return uint16_t((dp << 8) | fetch8()); }
    uint16_t ea_indexed(int& extra);
    uint16_t& index_reg(uint8_t post);

    uint8_t add8(uint8_t r, uint8_t m, bool carry);
    uint8_t sub8(uint8_t r, uint8_t m, bool carry);
    uint16_t add16(uint16_t r, uint16_t m);
    uint16_t sub16(uint16_t r, uint16_t m);
    uint8_t rmw(uint8_t op, uint8_t v);
    void ld8(uint8_t v) { nz8(v); set_flag(FLAG_V, false); }
    void ld16(uint16_t v) { nz16(v); set_flag(FLAG_V, false); }

    bool branch_cond(uint8_t op) const;
    uint16_t reg16(uint8_t code) const;
    void set_reg(uint8_t code, uint16_t v);
    int psh(bool use_s, uint8_t mask);
    int pul(bool use_s, uint8_t mask);

    int exec_page1(uint8_t op);
    int exec_page2(uint8_t op);
    int exec_page3(uint8_t op);
    int alu_group(uint8_t op, int c);
};

}  // namespace m6809

#endif
