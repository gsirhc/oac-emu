// MC6800 / MC6808. Opcode map, cycle counts and flag rules from M68PRM(D) Fig. 3-1, Fig. 4-1 and App. A.

#include "cpu_m6800.h"

namespace m6800 {

namespace {

constexpr uint16_t kVecIrq = 0xFFF8;
constexpr uint16_t kVecSwi = 0xFFFA;
constexpr uint16_t kVecNmi = 0xFFFC;
constexpr uint16_t kVecReset = 0xFFFE;

// imm, dir, idx, ext
constexpr int kAluCycles[4]   = {2, 3, 5, 4};
constexpr int kWordCycles[4]  = {3, 4, 6, 5};
constexpr int kStoreCycles[4] = {0, 4, 6, 5};
constexpr int kStoreWordCycles[4] = {0, 5, 7, 6};

}  // namespace

void Cpu::reset() {
    cc |= FLAG_I;
    waiting_ = false;
    nmi_pending_ = false;
    pc = rw(kVecReset);
}

void Cpu::push_state() {
    push8(uint8_t(pc));
    push8(uint8_t(pc >> 8));
    push8(uint8_t(x));
    push8(uint8_t(x >> 8));
    push8(a);
    push8(b);
    push8(uint8_t(cc | 0xC0));
}

// WAI has already stacked the state; the manual gives 4 cycles for the vector fetch after it.
int Cpu::service_interrupt(uint16_t vec) {
    int n = 4;
    if (waiting_) {
        waiting_ = false;
    } else {
        push_state();
        n = 12;
    }
    set_flag(FLAG_I, true);
    pc = rw(vec);
    return n;
}

int Cpu::step() {
    int n;
    if (nmi_pending_) {
        nmi_pending_ = false;
        n = service_interrupt(kVecNmi);
    } else if (irq_ && !flag(FLAG_I)) {
        n = service_interrupt(kVecIrq);
    } else if (waiting_) {
        n = 1;
    } else {
        uint8_t op = fetch8();
        if (op >= 0x80) n = exec_binary(op);
        else if (op >= 0x40) n = exec_unary(op);
        else if (op >= 0x20 && op < 0x30) {
            if (op == 0x21) {
                n = undefined();
            } else {
                int8_t rel = int8_t(fetch8());
                if (branch_taken(op)) pc = uint16_t(pc + rel);
                n = 4;
            }
        } else n = exec_inherent(op);
    }
    cycles += uint64_t(n);
    return n;
}

int Cpu::undefined() {
    undefined_ops++;
    return 2;
}

bool Cpu::branch_taken(uint8_t op) const {
    bool c = flag(FLAG_C), z = flag(FLAG_Z), n = flag(FLAG_N), v = flag(FLAG_V);
    switch (op & 0x0F) {
    case 0x0: return true;
    case 0x2: return !(c || z);
    case 0x3: return c || z;
    case 0x4: return !c;
    case 0x5: return c;
    case 0x6: return !z;
    case 0x7: return z;
    case 0x8: return !v;
    case 0x9: return v;
    case 0xA: return !n;
    case 0xB: return n;
    case 0xC: return n == v;
    case 0xD: return n != v;
    case 0xE: return !z && n == v;
    default:  return z || n != v;
    }
}

uint16_t Cpu::ea(int mode) {
    switch (mode) {
    case 0:  return pc++;
    case 1:  return fetch8();
    case 2:  return uint16_t(x + fetch8());
    default: return fetch16();
    }
}

uint8_t Cpu::add8(uint8_t l, uint8_t r, uint8_t carry) {
    unsigned sum = unsigned(l) + r + carry;
    uint8_t res = uint8_t(sum);
    set_flag(FLAG_H, ((l & 0x0F) + (r & 0x0F) + carry) > 0x0F);
    set_flag(FLAG_C, sum > 0xFF);
    set_flag(FLAG_V, ((~(l ^ r)) & (l ^ res) & 0x80) != 0);
    set_nz(res);
    return res;
}

uint8_t Cpu::sub8(uint8_t l, uint8_t r, uint8_t borrow) {
    uint8_t res = uint8_t(l - r - borrow);
    set_flag(FLAG_C, unsigned(l) < unsigned(r) + borrow);
    set_flag(FLAG_V, ((l ^ r) & (l ^ res) & 0x80) != 0);
    set_nz(res);
    return res;
}

void Cpu::logic_flags(uint8_t v) {
    set_nz(v);
    set_flag(FLAG_V, false);
}

// kind is the low nibble of the opcode.
uint8_t Cpu::unary(int kind, uint8_t v) {
    uint8_t r = v;
    bool shifted = false;
    switch (kind) {
    case 0x0:
        r = uint8_t(0 - v);
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
        set_flag(FLAG_C, (v & 1) != 0);
        shifted = true;
        break;
    case 0x6:
        r = uint8_t((v >> 1) | (flag(FLAG_C) ? 0x80 : 0));
        set_flag(FLAG_C, (v & 1) != 0);
        shifted = true;
        break;
    case 0x7:
        r = uint8_t((v >> 1) | (v & 0x80));
        set_flag(FLAG_C, (v & 1) != 0);
        shifted = true;
        break;
    case 0x8:
        r = uint8_t(v << 1);
        set_flag(FLAG_C, (v & 0x80) != 0);
        shifted = true;
        break;
    case 0x9:
        r = uint8_t((v << 1) | (flag(FLAG_C) ? 1 : 0));
        set_flag(FLAG_C, (v & 0x80) != 0);
        shifted = true;
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
        set_flag(FLAG_C, false);
        break;
    default:
        r = 0;
        set_flag(FLAG_V, false);
        set_flag(FLAG_C, false);
        break;
    }
    set_nz(r);
    if (shifted) set_flag(FLAG_V, flag(FLAG_N) != flag(FLAG_C));
    return r;
}

// Z over the full 16-bit difference, N and V from the high-byte subtraction, C untouched.
void Cpu::cpx(uint16_t m) {
    uint16_t r = uint16_t(x - m);
    set_flag(FLAG_Z, r == 0);
    set_flag(FLAG_N, (r & 0x8000) != 0);
    set_flag(FLAG_V, ((x ^ m) & (x ^ r) & 0x8000) != 0);
}

// M68PRM(D) DAA table; V is undefined and left alone.
void Cpu::daa() {
    unsigned lo = a & 0x0F, hi = a >> 4;
    unsigned corr = 0;
    if (flag(FLAG_H) || lo > 9) corr |= 0x06;
    if (flag(FLAG_C) || hi > 9 || (hi > 8 && lo > 9)) corr |= 0x60;
    unsigned sum = unsigned(a) + corr;
    a = uint8_t(sum);
    if (corr & 0x60) set_flag(FLAG_C, true);
    set_nz(a);
}

int Cpu::exec_inherent(uint8_t op) {
    switch (op) {
    case 0x01: return 2;
    case 0x06: cc = a & 0x3F; return 2;
    case 0x07: a = uint8_t(cc | 0xC0); return 2;
    case 0x08: x++; set_flag(FLAG_Z, x == 0); return 4;
    case 0x09: x--; set_flag(FLAG_Z, x == 0); return 4;
    case 0x0A: set_flag(FLAG_V, false); return 2;
    case 0x0B: set_flag(FLAG_V, true); return 2;
    case 0x0C: set_flag(FLAG_C, false); return 2;
    case 0x0D: set_flag(FLAG_C, true); return 2;
    case 0x0E: set_flag(FLAG_I, false); return 2;
    case 0x0F: set_flag(FLAG_I, true); return 2;
    case 0x10: a = sub8(a, b, 0); return 2;
    case 0x11: sub8(a, b, 0); return 2;
    case 0x16: b = a; logic_flags(b); return 2;
    case 0x17: a = b; logic_flags(a); return 2;
    case 0x19: daa(); return 2;
    case 0x1B: a = add8(a, b, 0); return 2;
    case 0x30: x = uint16_t(sp + 1); return 4;
    case 0x31: sp++; return 4;
    case 0x32: a = pull8(); return 4;
    case 0x33: b = pull8(); return 4;
    case 0x34: sp--; return 4;
    case 0x35: sp = uint16_t(x - 1); return 4;
    case 0x36: push8(a); return 4;
    case 0x37: push8(b); return 4;
    case 0x39: {
        uint8_t hi = pull8();
        uint8_t lo = pull8();
        pc = uint16_t((hi << 8) | lo);
        return 5;
    }
    case 0x3B: {
        cc = pull8() & 0x3F;
        b = pull8();
        a = pull8();
        uint8_t xh = pull8();
        uint8_t xl = pull8();
        x = uint16_t((xh << 8) | xl);
        uint8_t hi = pull8();
        uint8_t lo = pull8();
        pc = uint16_t((hi << 8) | lo);
        return 10;
    }
    case 0x3E:
        push_state();
        waiting_ = true;
        return 9;
    case 0x3F:
        push_state();
        set_flag(FLAG_I, true);
        pc = rw(kVecSwi);
        return 12;
    default: return undefined();
    }
}

int Cpu::exec_unary(uint8_t op) {
    int hi = op >> 4, lo = op & 0x0F;
    bool mem = hi >= 6;
    if (lo == 0xE) {
        if (!mem) return undefined();
        pc = ea(hi - 4);
        return hi == 6 ? 4 : 3;
    }
    switch (lo) {
    case 0x0: case 0x3: case 0x4: case 0x6: case 0x7: case 0x8:
    case 0x9: case 0xA: case 0xC: case 0xD: case 0xF:
        break;
    default:
        return undefined();
    }
    if (!mem) {
        uint8_t& r = (hi == 4) ? a : b;
        uint8_t v = unary(lo, r);
        if (lo != 0xD) r = v;
        return 2;
    }
    uint16_t addr = ea(hi - 4);
    uint8_t v = unary(lo, lo == 0xF ? 0 : rb(addr));
    if (lo != 0xD) wb(addr, v);
    return hi == 6 ? 7 : 6;
}

int Cpu::exec_binary(uint8_t op) {
    int hi = op >> 4, lo = op & 0x0F;
    int mode = hi & 3;
    bool use_b = hi >= 0xC;
    uint8_t& acc = use_b ? b : a;
    switch (lo) {
    case 0x0: case 0x1: case 0x2: case 0x4: case 0x5: case 0x6:
    case 0x8: case 0x9: case 0xA: case 0xB: {
        uint8_t m = rb(ea(mode));
        switch (lo) {
        case 0x0: acc = sub8(acc, m, 0); break;
        case 0x1: sub8(acc, m, 0); break;
        case 0x2: acc = sub8(acc, m, flag(FLAG_C) ? 1 : 0); break;
        case 0x4: acc &= m; logic_flags(acc); break;
        case 0x5: logic_flags(uint8_t(acc & m)); break;
        case 0x6: acc = m; logic_flags(acc); break;
        case 0x8: acc ^= m; logic_flags(acc); break;
        case 0x9: acc = add8(acc, m, flag(FLAG_C) ? 1 : 0); break;
        case 0xA: acc |= m; logic_flags(acc); break;
        default:  acc = add8(acc, m, 0); break;
        }
        return kAluCycles[mode];
    }
    case 0x7:
        if (mode == 0) return undefined();
        wb(ea(mode), acc);
        logic_flags(acc);
        return kStoreCycles[mode];
    case 0xC: {
        if (use_b) return undefined();
        uint16_t m = mode == 0 ? fetch16() : rw(ea(mode));
        cpx(m);
        return kWordCycles[mode];
    }
    case 0xD: {
        if (hi == 0x8) {
            int8_t rel = int8_t(fetch8());
            push8(uint8_t(pc));
            push8(uint8_t(pc >> 8));
            pc = uint16_t(pc + rel);
            return 8;
        }
        if (hi != 0xA && hi != 0xB) return undefined();
        uint16_t target = ea(mode);
        push8(uint8_t(pc));
        push8(uint8_t(pc >> 8));
        pc = target;
        return mode == 2 ? 8 : 9;
    }
    case 0xE: {
        uint16_t m = mode == 0 ? fetch16() : rw(ea(mode));
        set_nz16(m);
        set_flag(FLAG_V, false);
        if (use_b) x = m;
        else sp = m;
        return kWordCycles[mode];
    }
    case 0xF: {
        if (mode == 0) return undefined();
        uint16_t v = use_b ? x : sp;
        ww(ea(mode), v);
        set_nz16(v);
        set_flag(FLAG_V, false);
        return kStoreWordCycles[mode];
    }
    default:
        return undefined();
    }
}

}  // namespace m6800
