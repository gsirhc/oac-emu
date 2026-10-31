// Motorola MC6809E. Cycle counts from M6809PM Appendix F (Tables F-1, F-2); flags from Appendix A.

#include "cpu_m6809.h"

namespace m6809 {

namespace {

// Table F-1 base cycles, page 1. 0 = undefined. Indexed adds Table F-2.
constexpr uint8_t kPage1[256] = {
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,
    0, 0, 2, 2, 0, 0, 5, 9, 0, 2, 3, 0, 3, 2, 8, 6,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0, 5, 3, 6, 20, 11, 0, 19,
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,
    7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,
    2, 2, 2, 4, 2, 2, 2, 0, 2, 2, 2, 2, 4, 7, 3, 0,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6,
    2, 2, 2, 4, 2, 2, 2, 0, 2, 2, 2, 2, 3, 0, 3, 0,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,
};

}  // namespace

void Cpu::reset() {
    state_ = State::Run;
    nmi_pending_ = false;
    nmi_armed_ = false;
    dp = 0;
    cc = uint8_t(cc | FLAG_I | FLAG_F);
    pc = rd16(0xFFFE);
}

void Cpu::set_nmi(bool on) {
    if (on && !nmi_line_ && nmi_armed_) nmi_pending_ = true;
    nmi_line_ = on;
}

void Cpu::push_entire() {
    push_s16(pc);
    push_s16(u);
    push_s16(y);
    push_s16(x);
    push_s8(dp);
    push_s8(b);
    push_s8(a);
    push_s8(cc);
}

// Entry 19 cycles IRQ/NMI, 10 FIRQ (MC6809E Figs. 8, 9); out of CWAI, the vector fetch and a dead cycle (Fig. 17).
int Cpu::interrupts() {
    const bool firq = firq_ && !flag(FLAG_F);
    const bool irq = irq_ && !flag(FLAG_I);
    if (state_ == State::Sync) {
        if (!nmi_pending_ && !firq_ && !irq_) return 0;
        state_ = State::Run;
        // A masked line ends SYNC and execution simply continues (M6809PM SYNC).
        if (!nmi_pending_ && !firq && !irq) return 0;
    }
    const bool cwai = state_ == State::Cwai;
    if (nmi_pending_) {
        nmi_pending_ = false;
        if (!cwai) {
            set_flag(FLAG_E, true);
            push_entire();
        }
        state_ = State::Run;
        cc = uint8_t(cc | FLAG_I | FLAG_F);
        pc = rd16(0xFFFC);
        return cwai ? 3 : 19;
    }
    if (firq) {
        if (!cwai) {
            set_flag(FLAG_E, false);
            push_s16(pc);
            push_s8(cc);
        }
        state_ = State::Run;
        cc = uint8_t(cc | FLAG_I | FLAG_F);
        pc = rd16(0xFFF6);
        return cwai ? 3 : 10;
    }
    if (irq) {
        if (!cwai) {
            set_flag(FLAG_E, true);
            push_entire();
        }
        state_ = State::Run;
        cc = uint8_t(cc | FLAG_I);
        pc = rd16(0xFFF8);
        return cwai ? 3 : 19;
    }
    return 0;
}

int Cpu::step() {
    int c = interrupts();
    if (c == 0) {
        if (state_ != State::Run) {
            c = 1;
        } else {
            const uint8_t op = fetch8();
            if (op == 0x10) c = exec_page2(fetch8());
            else if (op == 0x11) c = exec_page3(fetch8());
            else c = exec_page1(op);
        }
    }
    cycles += uint64_t(c);
    return c;
}

// --- addressing ---

uint16_t& Cpu::index_reg(uint8_t post) {
    switch ((post >> 5) & 3) {
        case 0: return x;
        case 1: return y;
        case 2: return u;
        default: return s;
    }
}

// Postbyte decode and Table F-2 extra cycles.
uint16_t Cpu::ea_indexed(int& extra) {
    const uint8_t post = fetch8();
    uint16_t& r = index_reg(post);
    if (!(post & 0x80)) {
        extra += 1;
        int off = post & 0x1F;
        if (off & 0x10) off -= 0x20;
        return uint16_t(r + off);
    }
    uint16_t ea = 0;
    const bool indirect = post & 0x10;
    switch (post & 0x0F) {
        case 0x0: ea = r; r = uint16_t(r + 1); extra += 2; break;
        case 0x1: ea = r; r = uint16_t(r + 2); extra += 3; break;
        case 0x2: r = uint16_t(r - 1); ea = r; extra += 2; break;
        case 0x3: r = uint16_t(r - 2); ea = r; extra += 3; break;
        case 0x4: ea = r; break;
        case 0x5: ea = uint16_t(r + int8_t(b)); extra += 1; break;
        case 0x6: ea = uint16_t(r + int8_t(a)); extra += 1; break;
        case 0x8: ea = uint16_t(r + int8_t(fetch8())); extra += 1; break;
        case 0x9: ea = uint16_t(r + fetch16()); extra += 4; break;
        case 0xB: ea = uint16_t(r + d()); extra += 4; break;
        case 0xC: {
            const int8_t off = int8_t(fetch8());
            ea = uint16_t(pc + off);
            extra += 1;
            break;
        }
        case 0xD: {
            const uint16_t off = fetch16();
            ea = uint16_t(pc + off);
            extra += 5;
            break;
        }
        case 0xF: ea = fetch16(); extra += 2; break;
        default: illegal_ops++; break;
    }
    if (indirect) {
        ea = rd16(ea);
        extra += 3;
    }
    return ea;
}

// --- ALU ---

uint8_t Cpu::add8(uint8_t r, uint8_t m, bool carry) {
    const unsigned sum = unsigned(r) + m + (carry ? 1u : 0u);
    const uint8_t res = uint8_t(sum);
    set_flag(FLAG_H, ((r ^ m ^ res) & 0x10) != 0);
    set_flag(FLAG_V, ((r ^ res) & (m ^ res) & 0x80) != 0);
    set_flag(FLAG_C, sum > 0xFF);
    nz8(res);
    return res;
}

uint8_t Cpu::sub8(uint8_t r, uint8_t m, bool carry) {
    const unsigned diff = unsigned(r) - m - (carry ? 1u : 0u);
    const uint8_t res = uint8_t(diff);
    set_flag(FLAG_V, ((r ^ m) & (r ^ res) & 0x80) != 0);
    set_flag(FLAG_C, diff > 0xFF);
    nz8(res);
    return res;
}

uint16_t Cpu::add16(uint16_t r, uint16_t m) {
    const uint32_t sum = uint32_t(r) + m;
    const uint16_t res = uint16_t(sum);
    set_flag(FLAG_V, ((r ^ res) & (m ^ res) & 0x8000) != 0);
    set_flag(FLAG_C, sum > 0xFFFF);
    nz16(res);
    return res;
}

uint16_t Cpu::sub16(uint16_t r, uint16_t m) {
    const uint32_t diff = uint32_t(r) - m;
    const uint16_t res = uint16_t(diff);
    set_flag(FLAG_V, ((r ^ m) & (r ^ res) & 0x8000) != 0);
    set_flag(FLAG_C, diff > 0xFFFF);
    nz16(res);
    return res;
}

// Low nibble of the 0x/4x/5x/6x/7x rows.
uint8_t Cpu::rmw(uint8_t op, uint8_t v) {
    uint8_t r = v;
    switch (op & 0x0F) {
        case 0x0:
            r = uint8_t(-v);
            set_flag(FLAG_V, v == 0x80);
            set_flag(FLAG_C, v != 0);
            break;
        case 0x3:
            r = uint8_t(~v);
            set_flag(FLAG_V, false);
            set_flag(FLAG_C, true);
            break;
        case 0x4:
            r = uint8_t(v >> 1);
            set_flag(FLAG_C, v & 1);
            break;
        case 0x6:
            r = uint8_t((v >> 1) | (flag(FLAG_C) ? 0x80 : 0));
            set_flag(FLAG_C, v & 1);
            break;
        case 0x7:
            r = uint8_t((v >> 1) | (v & 0x80));
            set_flag(FLAG_C, v & 1);
            break;
        case 0x8:
            r = uint8_t(v << 1);
            set_flag(FLAG_C, v & 0x80);
            set_flag(FLAG_V, ((v ^ (v << 1)) & 0x80) != 0);
            break;
        case 0x9:
            r = uint8_t((v << 1) | (flag(FLAG_C) ? 1 : 0));
            set_flag(FLAG_C, v & 0x80);
            set_flag(FLAG_V, ((v ^ (v << 1)) & 0x80) != 0);
            break;
        case 0xA:
            r = uint8_t(v - 1);
            set_flag(FLAG_V, v == 0x80);
            break;
        case 0xC:
            r = uint8_t(v + 1);
            set_flag(FLAG_V, v == 0x7F);
            break;
        case 0xD:
            set_flag(FLAG_V, false);
            break;
        case 0xF:
            r = 0;
            set_flag(FLAG_V, false);
            set_flag(FLAG_C, false);
            break;
    }
    nz8(r);
    return r;
}

bool Cpu::branch_cond(uint8_t op) const {
    const bool c = flag(FLAG_C), z = flag(FLAG_Z), n = flag(FLAG_N), v = flag(FLAG_V);
    bool t = false;
    switch ((op >> 1) & 7) {
        case 0: t = true; break;
        case 1: t = !c && !z; break;
        case 2: t = !c; break;
        case 3: t = !z; break;
        case 4: t = !v; break;
        case 5: t = !n; break;
        case 6: t = n == v; break;
        case 7: t = !z && n == v; break;
    }
    return (op & 1) ? !t : t;
}

// EXG/TFR register codes (M6809PM Appendix A). Undefined codes read $FF / $FFFF.
uint16_t Cpu::reg16(uint8_t code) const {
    switch (code) {
        case 0x0: return d();
        case 0x1: return x;
        case 0x2: return y;
        case 0x3: return u;
        case 0x4: return s;
        case 0x5: return pc;
        case 0x8: return uint16_t(0xFF00 | a);
        case 0x9: return uint16_t(0xFF00 | b);
        case 0xA: return uint16_t(0xFF00 | cc);
        case 0xB: return uint16_t(0xFF00 | dp);
        default: return 0xFFFF;
    }
}

void Cpu::set_reg(uint8_t code, uint16_t v) {
    switch (code) {
        case 0x0: set_d(v); break;
        case 0x1: x = v; break;
        case 0x2: y = v; break;
        case 0x3: u = v; break;
        case 0x4: set_s(v); break;
        case 0x5: pc = v; break;
        case 0x8: a = uint8_t(v); break;
        case 0x9: b = uint8_t(v); break;
        case 0xA: cc = uint8_t(v); break;
        case 0xB: dp = uint8_t(v); break;
        default: break;
    }
}

// Push order PC, U/S, Y, X, DP, B, A, CC; one cycle per byte (Table F-1 "5+1/byte").
int Cpu::psh(bool use_s, uint8_t mask) {
    uint16_t& sp = use_s ? s : u;
    const uint16_t other = use_s ? u : s;
    int n = 0;
    auto p8 = [&](uint8_t v) { sp = uint16_t(sp - 1); wr(sp, v); n++; };
    auto p16 = [&](uint16_t v) { p8(uint8_t(v)); p8(uint8_t(v >> 8)); };
    if (mask & 0x80) p16(pc);
    if (mask & 0x40) p16(other);
    if (mask & 0x20) p16(y);
    if (mask & 0x10) p16(x);
    if (mask & 0x08) p8(dp);
    if (mask & 0x04) p8(b);
    if (mask & 0x02) p8(a);
    if (mask & 0x01) p8(cc);
    if (use_s) nmi_armed_ = true;
    return n;
}

int Cpu::pul(bool use_s, uint8_t mask) {
    uint16_t& sp = use_s ? s : u;
    int n = 0;
    auto p8 = [&]() { uint8_t v = rd(sp); sp = uint16_t(sp + 1); n++; return v; };
    auto p16 = [&]() { uint16_t h = p8(); return uint16_t((h << 8) | p8()); };
    if (mask & 0x01) cc = p8();
    if (mask & 0x02) a = p8();
    if (mask & 0x04) b = p8();
    if (mask & 0x08) dp = p8();
    if (mask & 0x10) x = p16();
    if (mask & 0x20) y = p16();
    if (mask & 0x40) {
        const uint16_t v = p16();
        if (use_s) u = v;
        else set_s(v);
    }
    if (mask & 0x80) pc = p16();
    if (use_s) nmi_armed_ = true;
    return n;
}

// --- execution ---

int Cpu::exec_page1(uint8_t op) {
    int c = kPage1[op];
    if (c == 0) {
        illegal_ops++;
        return 2;
    }
    const uint8_t hi = op >> 4;

    // Memory read-modify-write rows 0x, 6x, 7x.
    if (hi == 0x0 || hi == 0x6 || hi == 0x7) {
        uint16_t ea;
        if (hi == 0x0) ea = ea_direct();
        else if (hi == 0x6) ea = ea_indexed(c);
        else ea = fetch16();
        if ((op & 0x0F) == 0x0E) {
            pc = ea;
            return c;
        }
        const uint8_t v = (op & 0x0F) == 0x0F ? 0 : rd(ea);
        const uint8_t r = rmw(op, v);
        if ((op & 0x0F) != 0x0D) wr(ea, r);
        return c;
    }
    if (hi == 0x4) {
        a = rmw(op, a);
        return c;
    }
    if (hi == 0x5) {
        b = rmw(op, b);
        return c;
    }
    if (hi == 0x2) {
        const int8_t off = int8_t(fetch8());
        if (branch_cond(op)) pc = uint16_t(pc + off);
        return c;
    }
    if (hi >= 0x8) {
        return alu_group(op, c);
    }

    switch (op) {
        case 0x12: return c;
        case 0x13:
            state_ = State::Sync;
            return c;
        case 0x16: {
            const uint16_t off = fetch16();
            pc = uint16_t(pc + off);
            return c;
        }
        case 0x17: {
            const uint16_t off = fetch16();
            push_s16(pc);
            pc = uint16_t(pc + off);
            return c;
        }
        case 0x19: {
            // CF per nibble, M6809PM Appendix A DAA.
            const uint8_t lsn = a & 0x0F, msn = a >> 4;
            uint8_t cf = 0;
            if (flag(FLAG_H) || lsn > 9) cf |= 0x06;
            if (flag(FLAG_C) || msn > 9 || (msn > 8 && lsn > 9)) cf |= 0x60;
            const unsigned r = unsigned(a) + cf;
            a = uint8_t(r);
            if (r > 0xFF) set_flag(FLAG_C, true);
            nz8(a);
            return c;
        }
        case 0x1A: cc = uint8_t(cc | fetch8()); return c;
        case 0x1C: cc = uint8_t(cc & fetch8()); return c;
        case 0x1D:
            a = (b & 0x80) ? 0xFF : 0x00;
            nz16(d());
            return c;
        case 0x1E: {
            const uint8_t post = fetch8();
            const uint8_t r1 = post >> 4, r2 = post & 0x0F;
            const uint16_t v1 = reg16(r1), v2 = reg16(r2);
            set_reg(r1, v2);
            set_reg(r2, v1);
            return c;
        }
        case 0x1F: {
            const uint8_t post = fetch8();
            set_reg(post & 0x0F, reg16(post >> 4));
            return c;
        }
        case 0x30: x = ea_indexed(c); set_flag(FLAG_Z, x == 0); return c;
        case 0x31: y = ea_indexed(c); set_flag(FLAG_Z, y == 0); return c;
        case 0x32: set_s(ea_indexed(c)); return c;
        case 0x33: u = ea_indexed(c); return c;
        case 0x34: return c + psh(true, fetch8());
        case 0x35: return c + pul(true, fetch8());
        case 0x36: return c + psh(false, fetch8());
        case 0x37: return c + pul(false, fetch8());
        case 0x39: pc = pull_s16(); return c;
        case 0x3A: x = uint16_t(x + b); return c;
        case 0x3B:
            cc = pull_s8();
            if (flag(FLAG_E)) {
                a = pull_s8();
                b = pull_s8();
                dp = pull_s8();
                x = pull_s16();
                y = pull_s16();
                u = pull_s16();
                c = 15;
            }
            pc = pull_s16();
            return c;
        case 0x3C:
            cc = uint8_t(cc & fetch8());
            set_flag(FLAG_E, true);
            push_entire();
            state_ = State::Cwai;
            // 16 to stack, then the first $FFFF wait cycle; 20 with the wake (MC6809E Fig. 17).
            return 17;
        case 0x3D: {
            const uint16_t r = uint16_t(a * b);
            set_d(r);
            set_flag(FLAG_Z, r == 0);
            set_flag(FLAG_C, r & 0x80);
            return c;
        }
        case 0x3F:
            set_flag(FLAG_E, true);
            push_entire();
            cc = uint8_t(cc | FLAG_I | FLAG_F);
            pc = rd16(0xFFFA);
            return c;
    }
    illegal_ops++;
    return 2;
}

// Rows 8x-Fx. Low nibble picks the op; bit 6 picks A or B; bits 4-5 the mode.
int Cpu::alu_group(uint8_t op, int c) {
    const bool bside = op & 0x40;
    const int mode = (op >> 4) & 3;
    const uint8_t lo = op & 0x0F;
    uint8_t& acc = bside ? b : a;

    if (op == 0x8D) {
        const int8_t off = int8_t(fetch8());
        push_s16(pc);
        pc = uint16_t(pc + off);
        return c;
    }

    const bool wide = lo == 0x3 || lo == 0xC || lo == 0xE;
    const bool store = lo == 0x7 || lo == 0xF || (bside && lo == 0xD);
    const bool jsr = !bside && lo == 0xD;

    uint16_t ea = 0;
    uint16_t m16 = 0;
    switch (mode) {
        case 0:
            if (store) {
                illegal_ops++;
                return 2;
            }
            m16 = wide ? fetch16() : fetch8();
            break;
        case 1: ea = ea_direct(); break;
        case 2: ea = ea_indexed(c); break;
        case 3: ea = fetch16(); break;
    }
    if (jsr) {
        push_s16(pc);
        pc = ea;
        return c;
    }
    if (store) {
        if (lo == 0x7) {
            wr(ea, acc);
            ld8(acc);
        } else {
            uint16_t v;
            if (lo == 0xD) v = d();
            else if (bside) v = u;
            else v = x;
            wr16(ea, v);
            ld16(v);
        }
        return c;
    }
    if (mode != 0) m16 = wide ? rd16(ea) : rd(ea);
    const uint8_t m = uint8_t(m16);

    switch (lo) {
        case 0x0: acc = sub8(acc, m, false); break;
        case 0x1: sub8(acc, m, false); break;
        case 0x2: acc = sub8(acc, m, flag(FLAG_C)); break;
        case 0x3:
            if (bside) set_d(add16(d(), m16));
            else set_d(sub16(d(), m16));
            break;
        case 0x4: acc = uint8_t(acc & m); ld8(acc); break;
        case 0x5: ld8(uint8_t(acc & m)); break;
        case 0x6: acc = m; ld8(acc); break;
        case 0x8: acc = uint8_t(acc ^ m); ld8(acc); break;
        case 0x9: acc = add8(acc, m, flag(FLAG_C)); break;
        case 0xA: acc = uint8_t(acc | m); ld8(acc); break;
        case 0xB: acc = add8(acc, m, false); break;
        case 0xC:
            if (bside) {
                set_d(m16);
                ld16(m16);
            } else {
                sub16(x, m16);
            }
            break;
        case 0xE:
            if (bside) u = m16;
            else x = m16;
            ld16(m16);
            break;
    }
    return c;
}

int Cpu::exec_page2(uint8_t op) {
    const uint8_t hi = op >> 4;
    if (hi == 0x2) {
        const uint16_t off = fetch16();
        if (branch_cond(op)) {
            pc = uint16_t(pc + off);
            return op == 0x21 ? 5 : 6;
        }
        return 5;
    }
    if (op == 0x3F) {
        set_flag(FLAG_E, true);
        push_entire();
        pc = rd16(0xFFF4);
        return 20;
    }
    const uint8_t lo = op & 0x0F;
    const int mode = hi & 3;
    const bool sreg = hi >= 0xC;
    if (hi < 0x8 || (lo != 0x3 && lo != 0xC && lo != 0xE && lo != 0xF)) {
        illegal_ops++;
        return 2;
    }
    if (sreg && (lo == 0x3 || lo == 0xC)) {
        illegal_ops++;
        return 2;
    }
    // CMPD/CMPY/LDY/STY (8x-Bx), LDS/STS (Cx-Fx).
    static constexpr int kCmp[4] = {5, 7, 7, 8};
    static constexpr int kLd[4] = {4, 6, 6, 7};
    int c = (lo == 0x3 || lo == 0xC) ? kCmp[mode] : kLd[mode];
    uint16_t ea = 0, m = 0;
    if (mode == 0) {
        if (lo == 0xF) {
            illegal_ops++;
            return 2;
        }
        m = fetch16();
    } else {
        if (mode == 1) ea = ea_direct();
        else if (mode == 2) ea = ea_indexed(c);
        else ea = fetch16();
        if (lo != 0xF) m = rd16(ea);
    }
    switch (lo) {
        case 0x3: sub16(d(), m); break;
        case 0xC: sub16(y, m); break;
        case 0xE:
            if (sreg) set_s(m);
            else y = m;
            ld16(m);
            break;
        case 0xF: {
            const uint16_t v = sreg ? s : y;
            wr16(ea, v);
            ld16(v);
            break;
        }
    }
    return c;
}

int Cpu::exec_page3(uint8_t op) {
    if (op == 0x3F) {
        set_flag(FLAG_E, true);
        push_entire();
        pc = rd16(0xFFF2);
        return 20;
    }
    const uint8_t hi = op >> 4, lo = op & 0x0F;
    if (hi < 0x8 || hi > 0xB || (lo != 0x3 && lo != 0xC)) {
        illegal_ops++;
        return 2;
    }
    static constexpr int kCmp[4] = {5, 7, 7, 8};
    const int mode = hi & 3;
    int c = kCmp[mode];
    uint16_t m;
    if (mode == 0) m = fetch16();
    else if (mode == 1) m = rd16(ea_direct());
    else if (mode == 2) m = rd16(ea_indexed(c));
    else m = rd16(fetch16());
    sub16(lo == 0x3 ? u : s, m);
    return c;
}

}  // namespace m6809
