// Intel 80286 CPU core implementation. Semantics from the Intel iAPX 286
// Programmer's Reference Manual (1987), or the 8086/8088 User's Manual where identical.
//
// Preserved quirks:
//   - PUSH SP pushes SP from before the push (286+; the 8086 pushes the decremented value).
//   - Shift/rotate counts are masked mod 32 (the 8086 used the raw count).
//   - OF after a multi-bit shift/rotate is undefined per Intel and is left untouched.
#include "cpu80286.h"

#include <algorithm>

namespace cpu80286 {

void Cpu::reset() {
    ax = bx = cx = dx = sp = bp = si = di = 0;
    ds = es = ss = 0;
    cs = 0xF000;
    ip = 0xFFF0;
    flags = FLAG_R1;
    halted = false;
    cycles = 0;
    seg_override_ = -1;
    rep_ = REP_NONE;
    opsize32_ = false;
    extra_cycles_ = 0;
    msw_ = 0xFFF0;
    gdt_base_ = idt_base_ = 0;
    gdt_limit_ = 0xFFFF;
    idt_limit_ = 0x03FF;
    fault_ = false;
    ext_ = 0;
    shadow_ = vectored_ = rep_resume_ = rep_resumed_ = step_each_ = false;
    cpl_ = 0;
    ldtr_sel_ = tr_sel_ = 0;
    ldt_base_ = tr_base_ = 0;
    ldt_limit_ = tr_limit_ = 0;
    shutdown_ = false;
    for (int i = 0; i < 4; ++i) sc_[i] = SegCache{};
    sc_[SEG_CS].sel = 0xF000;
    sc_[SEG_CS].access = 0x9B;
    // CS's hidden base is FF0000h until the first far transfer, so the first
    // fetch is FFFFF0h (Intel iAPX 286 PRM, "Reset").
    sc_[SEG_CS].base = 0xFF0000u;
    queue_ = fetch_progress_ = 0;
    flushed_ = true;
}

void Cpu::real_interrupt(uint8_t vector) {
    // Exception 8 in real mode is "interrupt table limit too small" (Intel iAPX 286 PRM).
    if (uint32_t(vector) * 4 + 3 > idt_limit_) { raise(EXC_DF); return; }
    in_interrupt_ = true;
    push16(flags);
    push16(cs);
    push16(ip);
    in_interrupt_ = false;
    set_flag(FLAG_IF, false);
    set_flag(FLAG_TF, false);
    // Real mode indexes the IDT from IDTR's base, which LIDT can move.
    uint32_t vec = (idt_base_ + uint32_t(vector) * 4) & 0xFFFFFF;
    charge_word(vec);
    charge_word(vec + 2);
    uint16_t new_ip = bus_.read(vec) | (uint16_t(bus_.read(vec + 1)) << 8);
    uint16_t new_cs = bus_.read(vec + 2) | (uint16_t(bus_.read(vec + 3)) << 8);
    ip = new_ip;
    load_seg_real(SEG_CS, new_cs);
}

int Cpu::deliver(uint8_t vector, Kind kind, uint16_t err) {
    halted = false;
    ext_ = kind == Kind::Soft ? 0 : 1;
    int extra = 0;
    if (pe()) extra = pm_interrupt(vector, kind, err);
    else real_interrupt(vector);
    ext_ = 0;
    vectored_ = true;
    rep_resume_ = false;
    return extra;
}

// Two contributory faults make #DF; a fault delivering #DF is shutdown
// (Intel iAPX 286 PRM, exception 8).
int Cpu::raise_event(uint8_t vector, Kind kind, uint16_t err) {
    int extra = deliver(vector, kind, err);
    while (fault_) {
        uint8_t next = fault_vec_;
        uint16_t next_err = fault_err_;
        fault_ = false;
        restore_regs();
        ip = instr_start_ip_;
        bool first_exc = kind == Kind::Exception;
        if (first_exc && vector == EXC_DF) { shutdown_ = true; halted = true; return extra; }
        auto contributory = [](uint8_t v) { return v == EXC_DE || (v >= EXC_TS && v <= EXC_GP); };
        if (first_exc && contributory(vector) && contributory(next)) { next = EXC_DF; next_err = 0; }
        vector = next;
        err = next_err;
        kind = Kind::Exception;
        extra += deliver(vector, kind, err);
    }
    return extra;
}

// INT n is 23+m (80C286 data sheet, instruction timing); INTR adds the two
// INTA bus cycles that fetch the vector from the 8259s.
int Cpu::hardware_interrupt(uint8_t vector) {
    bus_busy_ = bus_extra_ = 0;
    if (!pe()) refresh_real_bases();
    instr_start_ip_ = ip;
    save_regs();
    int delivery = raise_event(vector, Kind::Hard);
    int ioclk = io_clk(0x20);
    int c = 23 + delivery + kQueueRefillTax + 2 * ioclk + bus_extra_;
#ifdef IBMPCAT_PERF
    // Each INTA cycle's 2 clocks stay with the instruction; the rest is the wait.
    perf.qtax += kQueueRefillTax;
    int extra = ioclk - 2;
    if (extra > 0) {
        if (ioclk >= 6) perf.wait_io8 += uint64_t(2 * extra);
        else perf.wait_io16 += uint64_t(2 * extra);
    }
    perf.cls[kPerfFlow] += uint64_t(23 + 4);
#endif
    flushed_ = true;
    cycles += c;
    return c;
}

#ifdef IBMPCAT_PERF
void Cpu::note_mem_wait(uint32_t addr, int clocks) {
    int extra = clocks - 2;
    if (extra <= 0) return;
    // RAM below A0000, EGA to C0000, EGA BIOS, system ROM from E0000 and its alias.
    uint64_t n = uint64_t(extra);
    if (addr < 0xA0000) perf.wait_ram += n;
    else if (addr < 0xC0000) perf.wait_ega += n;
    else if (addr < 0xC8000 || (addr >= 0xE0000 && addr < 0x100000) || addr >= 0xFF0000)
        perf.wait_rom += n;
    else perf.wait_slot += n;
}

void Cpu::note_io_wait(bool eight, int clocks) {
    int extra = clocks - 2;
    if (extra <= 0) return;
    if (eight) perf.wait_io8 += uint64_t(extra);
    else perf.wait_io16 += uint64_t(extra);
}

int perf_class(uint8_t op, int grp) {
    if (op == 0xF4) return kPerfHlt;
    if (op == 0xFF) {
        int r = grp & 7;
        if (r <= 1) return kPerfAlu;
        if (r == 6) return kPerfStack;
        return kPerfFlow;
    }
    if (op < 0x40) {
        if ((op & 7) <= 5) return kPerfAlu;
        if (op == 0x0F) return kPerfMisc;
        if (op == 0x27 || op == 0x2F || op == 0x37 || op == 0x3F) return kPerfAlu;
        return kPerfStack;
    }
    if (op <= 0x4F) return kPerfAlu;
    if (op <= 0x61) return kPerfStack;
    if (op < 0x68) return kPerfMisc;
    if (op == 0x68 || op == 0x6A) return kPerfStack;
    if (op == 0x69 || op == 0x6B) return kPerfAlu;
    if (op <= 0x6F) return kPerfString;
    if (op <= 0x7F) return kPerfFlow;
    if (op <= 0x85) return kPerfAlu;
    if (op <= 0x8E) return kPerfMov;
    if (op == 0x8F) return kPerfStack;
    if (op <= 0x99) return op == 0x9A ? kPerfFlow : kPerfMov;
    if (op == 0x9B) return kPerfMisc;
    if (op <= 0x9D) return kPerfStack;
    if (op <= 0x9F) return kPerfMisc;
    if (op <= 0xA3) return kPerfMov;
    if (op <= 0xA7) return kPerfString;
    if (op <= 0xA9) return kPerfAlu;
    if (op <= 0xAF) return kPerfString;
    if (op <= 0xBF) return kPerfMov;
    if (op <= 0xC1) return kPerfAlu;
    if (op <= 0xC3) return kPerfFlow;
    if (op <= 0xC7) return kPerfMov;
    if (op <= 0xC9) return kPerfStack;
    if (op <= 0xCF) return kPerfFlow;
    if (op <= 0xD5) return kPerfAlu;
    if (op == 0xD7) return kPerfMov;
    if (op <= 0xDF) return kPerfMisc;
    if (op <= 0xE3) return kPerfFlow;
    if (op <= 0xE7) return kPerfIo;
    if (op <= 0xEB) return kPerfFlow;
    if (op <= 0xEF) return kPerfIo;
    if (op == 0xF6 || op == 0xF7 || op == 0xFE) return kPerfAlu;
    return kPerfMisc;
}
#endif

int Cpu::prefetch(int exec_clocks) {
    int k = mem_clk(code_base() + instr_start_ip_);
    int word = word_clocks(k, false);
    int need = rep_resumed_ ? 0 : fetched_;
    int stall = 0;
    if (flushed_) {
        // A transfer's published count already fetched its target at 0 wait states.
        int words = (need + 1) / 2;
        stall = words * (word - 2);
        queue_ = words * 2 - need;
        fetch_progress_ = 0;
    } else {
        while (queue_ < need) {
            stall += word - fetch_progress_;
            fetch_progress_ = 0;
            queue_ += 2;
        }
        queue_ -= need;
    }
    // The bus unit works on the next word whenever execution leaves the bus idle.
    int idle = exec_clocks - bus_busy_;
    if (idle > 0 && queue_ < kQueueBytes) {
        fetch_progress_ += idle;
        while (fetch_progress_ >= word && queue_ < kQueueBytes) {
            fetch_progress_ -= word;
            queue_ += 2;
        }
        if (queue_ >= kQueueBytes) { queue_ = kQueueBytes; fetch_progress_ = 0; }
    }
    flushed_ = vectored_ || transfer_ || (!rep_resume_ && (ip != uint16_t(instr_start_ip_ + fetched_) || cs != snap_.cs));
    if (flushed_) queue_ = fetch_progress_ = 0;
    return stall;
}

void Cpu::save_regs() {
    snap_ = Snapshot{ax, bx, cx, dx, sp, bp, si, di, cs, ds, es, ss, flags, msw_,
                     {sc_[0], sc_[1], sc_[2], sc_[3]},
                     gdt_base_, idt_base_, gdt_limit_, idt_limit_, halted,
                     cpl_, ldtr_sel_, tr_sel_, ldt_base_, tr_base_, ldt_limit_, tr_limit_};
}

void Cpu::restore_regs() {
    ax = snap_.ax; bx = snap_.bx; cx = snap_.cx; dx = snap_.dx;
    sp = snap_.sp; bp = snap_.bp; si = snap_.si; di = snap_.di;
    cs = snap_.cs; ds = snap_.ds; es = snap_.es; ss = snap_.ss;
    flags = snap_.flags; msw_ = snap_.msw;
    for (int i = 0; i < 4; ++i) sc_[i] = snap_.sc[i];
    gdt_base_ = snap_.gdt_base; idt_base_ = snap_.idt_base;
    gdt_limit_ = snap_.gdt_limit; idt_limit_ = snap_.idt_limit;
    halted = snap_.halted;
    cpl_ = snap_.cpl;
    ldtr_sel_ = snap_.ldtr_sel; tr_sel_ = snap_.tr_sel;
    ldt_base_ = snap_.ldt_base; tr_base_ = snap_.tr_base;
    ldt_limit_ = snap_.ldt_limit; tr_limit_ = snap_.tr_limit;
}

// --- register file ----------------------------------------------------

uint16_t Cpu::get_reg16(int idx) const { return uint16_t(get_reg32(idx)); }
void Cpu::set_reg16(int idx, uint16_t v) {
    // Upper 16 bits are preserved.
    switch (idx & 7) {
        case 0: ax = (ax & 0xFFFF0000u) | v; break;
        case 1: cx = (cx & 0xFFFF0000u) | v; break;
        case 2: dx = (dx & 0xFFFF0000u) | v; break;
        case 3: bx = (bx & 0xFFFF0000u) | v; break;
        case 4: sp = (sp & 0xFFFF0000u) | v; break;
        case 5: bp = (bp & 0xFFFF0000u) | v; break;
        case 6: si = (si & 0xFFFF0000u) | v; break;
        default: di = (di & 0xFFFF0000u) | v; break;
    }
}
uint32_t Cpu::get_reg32(int idx) const {
    switch (idx & 7) {
        case 0: return ax;
        case 1: return cx;
        case 2: return dx;
        case 3: return bx;
        case 4: return sp;
        case 5: return bp;
        case 6: return si;
        default: return di;
    }
}
void Cpu::set_reg32(int idx, uint32_t v) {
    switch (idx & 7) {
        case 0: ax = v; break;
        case 1: cx = v; break;
        case 2: dx = v; break;
        case 3: bx = v; break;
        case 4: sp = v; break;
        case 5: bp = v; break;
        case 6: si = v; break;
        default: di = v; break;
    }
}
uint8_t Cpu::get_reg8(int idx) const {
    switch (idx & 7) {
        case 0: return uint8_t(ax);
        case 1: return uint8_t(cx);
        case 2: return uint8_t(dx);
        case 3: return uint8_t(bx);
        case 4: return uint8_t(ax >> 8);
        case 5: return uint8_t(cx >> 8);
        case 6: return uint8_t(dx >> 8);
        default: return uint8_t(bx >> 8);
    }
}
void Cpu::set_reg8(int idx, uint8_t v) {
    // Only the addressed byte changes.
    switch (idx & 7) {
        case 0: ax = (ax & 0xFFFFFF00u) | v; break;
        case 1: cx = (cx & 0xFFFFFF00u) | v; break;
        case 2: dx = (dx & 0xFFFFFF00u) | v; break;
        case 3: bx = (bx & 0xFFFFFF00u) | v; break;
        case 4: ax = (ax & 0xFFFF00FFu) | (uint32_t(v) << 8); break;
        case 5: cx = (cx & 0xFFFF00FFu) | (uint32_t(v) << 8); break;
        case 6: dx = (dx & 0xFFFF00FFu) | (uint32_t(v) << 8); break;
        default: bx = (bx & 0xFFFF00FFu) | (uint32_t(v) << 8); break;
    }
}

uint16_t &Cpu::seg_reg(int idx) {
    switch (idx & 3) {
        case SEG_ES: return es;
        case SEG_CS: return cs;
        case SEG_SS: return ss;
        default: return ds;
    }
}

// --- ModR/M decode ------------------------------------------------------

Cpu::RM Cpu::decode_modrm() {
    uint8_t modrm = fetch8();
    int mod = (modrm >> 6) & 3;
    int reg = (modrm >> 3) & 7;
    int rm = modrm & 7;
    last_reg_ = reg;

    RM out{};
    if (mod == 3) {
        out.is_mem = false;
        out.reg = rm;
        return out;
    }

    out.is_mem = true;
    uint16_t addr = 0;
    bool uses_bp = false;
    bool has_disp_only = false;  // mod==0, rm==6: disp16, no base register
    switch (rm) {
        case 0: addr = uint16_t(bx + si); break;
        case 1: addr = uint16_t(bx + di); break;
        case 2: addr = uint16_t(bp + si); uses_bp = true; break;
        case 3: addr = uint16_t(bp + di); uses_bp = true; break;
        case 4: addr = si; break;
        case 5: addr = di; break;
        case 6:
            if (mod == 0) { addr = fetch16(); has_disp_only = true; }
            else { addr = bp; uses_bp = true; }
            break;
        default: addr = bx; break;  // rm == 7
    }
    if (!has_disp_only) {
        if (mod == 1) {
            int8_t d = int8_t(fetch8());
            addr = uint16_t(addr + int16_t(d));
        } else if (mod == 2) {
            uint16_t d = fetch16();
            addr = uint16_t(addr + d);
        }
        if (mod != 0 && rm < 4) ++extra_cycles_;  // base+index+disp sums 3 elements: +1 clock (80C286 data sheet)
    }
    int seg_idx = uses_bp ? SEG_SS : SEG_DS;
    if (seg_override_ >= 0) seg_idx = seg_override_;
    out.seg = seg_idx;
    out.off = addr;
    return out;
}

// --- flags --------------------------------------------------------------

bool Cpu::parity_even(uint8_t v) {
    v = uint8_t(v ^ (v >> 4));
    v = uint8_t(v ^ (v >> 2));
    v = uint8_t(v ^ (v >> 1));
    return !(v & 1);
}
void Cpu::set_pzs8(uint8_t r) {
    set_flag(FLAG_ZF, r == 0);
    set_flag(FLAG_SF, (r & 0x80) != 0);
    set_flag(FLAG_PF, parity_even(r));
}
void Cpu::set_pzs16(uint16_t r) {
    set_flag(FLAG_ZF, r == 0);
    set_flag(FLAG_SF, (r & 0x8000) != 0);
    set_flag(FLAG_PF, parity_even(uint8_t(r & 0xFF)));
}
void Cpu::set_pzs32(uint32_t r) {
    set_flag(FLAG_ZF, r == 0);
    set_flag(FLAG_SF, (r & 0x80000000u) != 0);
    set_flag(FLAG_PF, parity_even(uint8_t(r & 0xFF)));
}

// --- ALU primitives -------------------------------------------------------

uint8_t Cpu::add8(uint8_t a, uint8_t b, bool carry_in) {
    unsigned r = unsigned(a) + unsigned(b) + (carry_in ? 1u : 0u);
    uint8_t res = uint8_t(r);
    set_flag(FLAG_CF, r > 0xFF);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((~(a ^ b)) & (a ^ res) & 0x80) != 0);
    set_pzs8(res);
    return res;
}
uint16_t Cpu::add16(uint16_t a, uint16_t b, bool carry_in) {
    unsigned r = unsigned(a) + unsigned(b) + (carry_in ? 1u : 0u);
    uint16_t res = uint16_t(r);
    set_flag(FLAG_CF, r > 0xFFFF);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((~(a ^ b)) & (a ^ res) & 0x8000) != 0);
    set_pzs16(res);
    return res;
}
uint8_t Cpu::sub8(uint8_t a, uint8_t b, bool borrow_in) {
    unsigned bb = unsigned(b) + (borrow_in ? 1u : 0u);
    uint8_t res = uint8_t(unsigned(a) - bb);
    set_flag(FLAG_CF, unsigned(a) < bb);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((a ^ b) & (a ^ res) & 0x80) != 0);
    set_pzs8(res);
    return res;
}
uint16_t Cpu::sub16(uint16_t a, uint16_t b, bool borrow_in) {
    unsigned bb = unsigned(b) + (borrow_in ? 1u : 0u);
    uint16_t res = uint16_t(unsigned(a) - bb);
    set_flag(FLAG_CF, unsigned(a) < bb);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((a ^ b) & (a ^ res) & 0x8000) != 0);
    set_pzs16(res);
    return res;
}
uint8_t Cpu::and8(uint8_t a, uint8_t b) {
    uint8_t r = uint8_t(a & b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs8(r);
    return r;
}
uint16_t Cpu::and16(uint16_t a, uint16_t b) {
    uint16_t r = uint16_t(a & b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs16(r);
    return r;
}
uint8_t Cpu::or8(uint8_t a, uint8_t b) {
    uint8_t r = uint8_t(a | b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs8(r);
    return r;
}
uint16_t Cpu::or16(uint16_t a, uint16_t b) {
    uint16_t r = uint16_t(a | b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs16(r);
    return r;
}
uint8_t Cpu::xor8(uint8_t a, uint8_t b) {
    uint8_t r = uint8_t(a ^ b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs8(r);
    return r;
}
uint16_t Cpu::xor16(uint16_t a, uint16_t b) {
    uint16_t r = uint16_t(a ^ b);
    set_flag(FLAG_CF, false);
    set_flag(FLAG_OF, false);
    set_pzs16(r);
    return r;
}

uint32_t Cpu::add32(uint32_t a, uint32_t b, bool carry_in) {
    uint64_t r = uint64_t(a) + uint64_t(b) + (carry_in ? 1u : 0u);
    uint32_t res = uint32_t(r);
    set_flag(FLAG_CF, r > 0xFFFFFFFFull);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((~(a ^ b)) & (a ^ res) & 0x80000000u) != 0);
    set_pzs32(res);
    return res;
}
uint32_t Cpu::sub32(uint32_t a, uint32_t b, bool borrow_in) {
    uint64_t bb = uint64_t(b) + (borrow_in ? 1u : 0u);
    uint32_t res = uint32_t(uint64_t(a) - bb);
    set_flag(FLAG_CF, uint64_t(a) < bb);
    set_flag(FLAG_AF, ((a ^ b ^ res) & 0x10) != 0);
    set_flag(FLAG_OF, ((a ^ b) & (a ^ res) & 0x80000000u) != 0);
    set_pzs32(res);
    return res;
}
uint32_t Cpu::and32(uint32_t a, uint32_t b) {
    uint32_t r = a & b;
    set_flag(FLAG_CF, false); set_flag(FLAG_OF, false); set_pzs32(r);
    return r;
}
uint32_t Cpu::or32(uint32_t a, uint32_t b) {
    uint32_t r = a | b;
    set_flag(FLAG_CF, false); set_flag(FLAG_OF, false); set_pzs32(r);
    return r;
}
uint32_t Cpu::xor32(uint32_t a, uint32_t b) {
    uint32_t r = a ^ b;
    set_flag(FLAG_CF, false); set_flag(FLAG_OF, false); set_pzs32(r);
    return r;
}

uint8_t Cpu::alu_apply8(int alu, uint8_t a, uint8_t b) {
    switch (alu) {
        case 0: return add8(a, b, false);              // ADD
        case 1: return or8(a, b);                       // OR
        case 2: return add8(a, b, flag(FLAG_CF));       // ADC
        case 3: return sub8(a, b, flag(FLAG_CF));       // SBB
        case 4: return and8(a, b);                      // AND
        case 5: return sub8(a, b, false);               // SUB
        case 6: return xor8(a, b);                      // XOR
        default: return sub8(a, b, false);              // CMP (caller discards)
    }
}
uint16_t Cpu::alu_apply16(int alu, uint16_t a, uint16_t b) {
    switch (alu) {
        case 0: return add16(a, b, false);
        case 1: return or16(a, b);
        case 2: return add16(a, b, flag(FLAG_CF));
        case 3: return sub16(a, b, flag(FLAG_CF));
        case 4: return and16(a, b);
        case 5: return sub16(a, b, false);
        case 6: return xor16(a, b);
        default: return sub16(a, b, false);
    }
}

uint32_t Cpu::alu_apply32(int alu, uint32_t a, uint32_t b) {
    switch (alu) {
        case 0: return add32(a, b, false);
        case 1: return or32(a, b);
        case 2: return add32(a, b, flag(FLAG_CF));
        case 3: return sub32(a, b, flag(FLAG_CF));
        case 4: return and32(a, b);
        case 5: return sub32(a, b, false);
        case 6: return xor32(a, b);
        default: return sub32(a, b, false);
    }
}

// --- shift/rotate group ---------------------------------------------------
// reg-field selector: 0=ROL 1=ROR 2=RCL 3=RCR 4=SHL/SAL 5=SHR 6=SAL(alias) 7=SAR

uint8_t Cpu::shiftrot8(int op, uint8_t v, int count) {
    count &= 0x1F;  // 80286 masks the count mod 32 (8086 used the raw count)
    if (count == 0) return v;
    uint8_t orig = v;
    bool cf = flag(FLAG_CF);
    uint8_t res = v;
    for (int i = 0; i < count; ++i) {
        switch (op) {
            case 0: cf = (res & 0x80) != 0; res = uint8_t((res << 1) | (cf ? 1 : 0)); break;
            case 1: cf = (res & 1) != 0; res = uint8_t((res >> 1) | (cf ? 0x80 : 0)); break;
            case 2: { bool nc = (res & 0x80) != 0; res = uint8_t((res << 1) | (cf ? 1 : 0)); cf = nc; break; }
            case 3: { bool nc = (res & 1) != 0; res = uint8_t((res >> 1) | (cf ? 0x80 : 0)); cf = nc; break; }
            case 5: cf = (res & 1) != 0; res = uint8_t(res >> 1); break;
            case 7: cf = (res & 1) != 0; res = uint8_t(uint8_t(int8_t(res) >> 1)); break;
            default: cf = (res & 0x80) != 0; res = uint8_t(res << 1); break;  // 4/6: SHL/SAL
        }
    }
    set_flag(FLAG_CF, cf);
    if (op >= 4) set_pzs8(res);  // rotates (0-3) leave PF/ZF/SF alone
    if (count == 1) {
        bool of;
        switch (op) {
            case 1: case 3: of = ((res & 0x80) != 0) != ((res & 0x40) != 0); break;  // ROR/RCR
            case 5: of = (orig & 0x80) != 0; break;                                  // SHR
            case 7: of = false; break;                                              // SAR
            default: of = ((res & 0x80) != 0) != cf; break;                          // ROL/RCL/SHL
        }
        set_flag(FLAG_OF, of);
    }
    return res;
}
uint16_t Cpu::shiftrot16(int op, uint16_t v, int count) {
    count &= 0x1F;
    if (count == 0) return v;
    uint16_t orig = v;
    bool cf = flag(FLAG_CF);
    uint16_t res = v;
    for (int i = 0; i < count; ++i) {
        switch (op) {
            case 0: cf = (res & 0x8000) != 0; res = uint16_t((res << 1) | (cf ? 1 : 0)); break;
            case 1: cf = (res & 1) != 0; res = uint16_t((res >> 1) | (cf ? 0x8000 : 0)); break;
            case 2: { bool nc = (res & 0x8000) != 0; res = uint16_t((res << 1) | (cf ? 1 : 0)); cf = nc; break; }
            case 3: { bool nc = (res & 1) != 0; res = uint16_t((res >> 1) | (cf ? 0x8000 : 0)); cf = nc; break; }
            case 5: cf = (res & 1) != 0; res = uint16_t(res >> 1); break;
            case 7: cf = (res & 1) != 0; res = uint16_t(uint16_t(int16_t(res) >> 1)); break;
            default: cf = (res & 0x8000) != 0; res = uint16_t(res << 1); break;
        }
    }
    set_flag(FLAG_CF, cf);
    if (op >= 4) set_pzs16(res);
    if (count == 1) {
        bool of;
        switch (op) {
            case 1: case 3: of = ((res & 0x8000) != 0) != ((res & 0x4000) != 0); break;
            case 5: of = (orig & 0x8000) != 0; break;
            case 7: of = false; break;
            default: of = ((res & 0x8000) != 0) != cf; break;
        }
        set_flag(FLAG_OF, of);
    }
    return res;
}

uint32_t Cpu::shiftrot32(int op, uint32_t v, int count) {
    count &= 0x1F;
    if (count == 0) return v;
    uint32_t orig = v;
    bool cf = flag(FLAG_CF);
    uint32_t res = v;
    for (int i = 0; i < count; ++i) {
        switch (op) {
            case 0: cf = (res & 0x80000000u) != 0; res = (res << 1) | (cf ? 1u : 0u); break;
            case 1: cf = (res & 1) != 0; res = (res >> 1) | (cf ? 0x80000000u : 0u); break;
            case 2: { bool nc = (res & 0x80000000u) != 0; res = (res << 1) | (cf ? 1u : 0u); cf = nc; break; }
            case 3: { bool nc = (res & 1) != 0; res = (res >> 1) | (cf ? 0x80000000u : 0u); cf = nc; break; }
            case 5: cf = (res & 1) != 0; res = res >> 1; break;
            case 7: cf = (res & 1) != 0; res = uint32_t(int32_t(res) >> 1); break;
            default: cf = (res & 0x80000000u) != 0; res = res << 1; break;
        }
    }
    set_flag(FLAG_CF, cf);
    if (op >= 4) set_pzs32(res);
    if (count == 1) {
        bool of;
        switch (op) {
            case 1: case 3: of = ((res & 0x80000000u) != 0) != ((res & 0x40000000u) != 0); break;
            case 5: of = (orig & 0x80000000u) != 0; break;
            case 7: of = false; break;
            default: of = ((res & 0x80000000u) != 0) != cf; break;
        }
        set_flag(FLAG_OF, of);
    }
    return res;
}

// --- BCD adjust + misc single instructions -------------------------------

void Cpu::daa() {
    uint8_t al = get_reg8(0);
    bool cf = flag(FLAG_CF), af = flag(FLAG_AF);
    uint8_t old_al = al;
    bool old_cf = cf;
    if (((al & 0x0F) > 9) || af) {
        bool carry = (unsigned(al) + 6) > 0xFF;
        al = uint8_t(al + 6);
        cf = old_cf || carry;
        af = true;
    } else {
        af = false;
    }
    if (old_al > 0x99 || old_cf) {
        al = uint8_t(al + 0x60);
        cf = true;
    }
    set_reg8(0, al);
    set_flag(FLAG_CF, cf);
    set_flag(FLAG_AF, af);
    set_pzs8(al);
}
void Cpu::das() {
    uint8_t al = get_reg8(0);
    bool cf = flag(FLAG_CF), af = flag(FLAG_AF);
    uint8_t old_al = al;
    bool old_cf = cf;
    if (((al & 0x0F) > 9) || af) {
        bool borrow = al < 6;
        al = uint8_t(al - 6);
        cf = old_cf || borrow;
        af = true;
    } else {
        af = false;
    }
    if (old_al > 0x99 || old_cf) {
        al = uint8_t(al - 0x60);
        cf = true;
    }
    set_reg8(0, al);
    set_flag(FLAG_CF, cf);
    set_flag(FLAG_AF, af);
    set_pzs8(al);
}
void Cpu::aaa() {
    uint8_t al = get_reg8(0), ah = get_reg8(4);
    bool af = flag(FLAG_AF);
    if (((al & 0x0F) > 9) || af) {
        // AX + 106h on the 286, not AL + 6 (Hummel, Programmer's Technical Reference)
        uint16_t sum = uint16_t(((ah << 8) | al) + 0x106);
        al = uint8_t(sum); ah = uint8_t(sum >> 8);
        set_flag(FLAG_AF, true);
        set_flag(FLAG_CF, true);
    } else {
        set_flag(FLAG_AF, false);
        set_flag(FLAG_CF, false);
    }
    al = uint8_t(al & 0x0F);
    set_reg8(0, al);
    set_reg8(4, ah);
}
void Cpu::aas() {
    uint8_t al = get_reg8(0), ah = get_reg8(4);
    bool af = flag(FLAG_AF);
    if (((al & 0x0F) > 9) || af) {
        // AX - 6, then AH - 1 (Intel SDM, AAS)
        uint16_t diff = uint16_t(((ah << 8) | al) - 6);
        al = uint8_t(diff); ah = uint8_t((diff >> 8) - 1);
        set_flag(FLAG_AF, true);
        set_flag(FLAG_CF, true);
    } else {
        set_flag(FLAG_AF, false);
        set_flag(FLAG_CF, false);
    }
    al = uint8_t(al & 0x0F);
    set_reg8(0, al);
    set_reg8(4, ah);
}
void Cpu::aam() {
    uint8_t base = fetch8();
    uint8_t al = get_reg8(0);
    if (base == 0) { raise(EXC_DE); return; }  // #DE, divide error
    uint8_t ah = uint8_t(al / base);
    al = uint8_t(al % base);
    set_reg8(4, ah);
    set_reg8(0, al);
    set_pzs8(al);
}
void Cpu::aad() {
    uint8_t base = fetch8();
    uint8_t al = get_reg8(0), ah = get_reg8(4);
    uint8_t res = uint8_t(al + ah * base);
    set_reg8(0, res);
    set_reg8(4, 0);
    set_pzs8(res);
}

void Cpu::push_reg(int idx) {
    // PUSH SP pushes SP from before the push; the 8086 pushes the decremented
    // value. Intel iAPX 286 PRM, "Instruction Set Differences from the 8086".
    if (opsize32_) push32(get_reg32(idx));
    else push16(get_reg16(idx));
}
void Cpu::pusha() {
    uint32_t orig_sp = sp;
    // PUSHAD/POPAD under 0x66 are 386 additions; BIOS-bochs-legacy's 32-bit INT 15h frame uses them.
    if (opsize32_) {
        push32(ax); push32(cx); push32(dx); push32(bx);
        push32(orig_sp);
        push32(bp); push32(si); push32(di);
        return;
    }
    push16(ax); push16(cx); push16(dx); push16(bx);
    push16(uint16_t(orig_sp));
    push16(bp); push16(si); push16(di);
}
void Cpu::popa() {
    if (opsize32_) {
        di = pop32(); si = pop32(); bp = pop32();
        pop32();
        bx = pop32(); dx = pop32(); cx = pop32(); ax = pop32();
        return;
    }
    di = pop16(); si = pop16(); bp = pop16();
    pop16();  // saved-SP slot is discarded
    bx = pop16(); dx = pop16(); cx = pop16(); ax = pop16();
}
void Cpu::bound() {
    RM rm = decode_modrm();
    int16_t idx = int16_t(get_reg16(last_reg_));
    uint16_t lo, hi;
    if (!far_operand(rm, lo, hi)) return;
    if (idx < int16_t(lo) || idx > int16_t(hi)) raise(EXC_BR);
}
void Cpu::imul_imm16(int dst_reg, const RM &rm, uint16_t imm) {
    int32_t a = int16_t(rm_read16(rm));
    int32_t b = int16_t(imm);
    int32_t r = a * b;
    uint16_t res = uint16_t(r);
    bool of = (int32_t(int16_t(res)) != r);
    set_flag(FLAG_CF, of);
    set_flag(FLAG_OF, of);
    set_reg16(dst_reg, res);
}
void Cpu::imul_imm32(int dst_reg, const RM &rm, uint32_t imm) {
    int64_t a = int32_t(rm_read32(rm));
    int64_t b = int32_t(imm);
    int64_t r = a * b;
    uint32_t res = uint32_t(r);
    bool of = (int64_t(int32_t(res)) != r);
    set_flag(FLAG_CF, of);
    set_flag(FLAG_OF, of);
    set_reg32(dst_reg, res);
}
int Cpu::enter() {
    uint16_t size = fetch16();
    uint8_t level = uint8_t(fetch8() & 0x1F);
    push16(bp);
    uint16_t frame_ptr = uint16_t(sp);
    if (level > 0) {
        for (int i = 1; i < level; ++i) {
            bp = uint16_t(bp - 2);
            push16(rw(SEG_SS, bp));
        }
        push16(frame_ptr);
    }
    bp = frame_ptr;
    sp = uint16_t(sp - size);
    return level;
}
void Cpu::leave() {
    sp = bp;
    bp = pop16();
}

// SGDT/SIDT store FFh in the sixth byte where a 386 stores 00h (Intel386 PRM,
// SGDT/SIDT 80286 compatibility). Timings from the iAPX 286 PRM appendix.
int Cpu::system_0f01() {
    RM rm = decode_modrm();
    switch (last_reg_) {
        case 0: case 1: {  // SGDT, SIDT
            if (!rm.is_mem) { ud(); return 3; }
            uint32_t base = last_reg_ ? idt_base_ : gdt_base_;
            uint16_t limit = last_reg_ ? idt_limit_ : gdt_limit_;
            check(rm.seg, rm.off, 6, true);
            ww(rm.seg, rm.off, limit);
            ww(rm.seg, uint16_t(rm.off + 2), uint16_t(base));
            wb(rm.seg, uint16_t(rm.off + 4), uint8_t(base >> 16));
            wb(rm.seg, uint16_t(rm.off + 5), 0xFF);
            return last_reg_ ? 12 : 11;
        }
        case 2: case 3: {  // LGDT, LIDT
            if (!rm.is_mem) { ud(); return 3; }
            if (!cpl0()) return 11;
            check(rm.seg, rm.off, 6, false);
            uint16_t limit = rw(rm.seg, rm.off);
            uint32_t base = rw(rm.seg, uint16_t(rm.off + 2)) |
                            (uint32_t(rb(rm.seg, uint16_t(rm.off + 4))) << 16);
            if (last_reg_ == 2) { gdt_base_ = base; gdt_limit_ = limit; return 11; }
            idt_base_ = base; idt_limit_ = limit;
            return 12;
        }
        case 4: rm_write16(rm, msw_); return rm.is_mem ? 3 : 2;  // SMSW
        case 6: {  // LMSW: PE can be set but not cleared (Intel iAPX 286 PRM, LMSW)
            uint16_t v = rm_read16(rm);
            if (!cpl0()) return 3;
            constexpr uint16_t kLoadable = MSW_MP | MSW_EM | MSW_TS;
            msw_ = uint16_t((msw_ & ~kLoadable) | (v & (kLoadable | MSW_PE)));
            return rm.is_mem ? 6 : 3;
        }
        default: ud(); return 3;
    }
}

// --- segments and far transfers -------------------------------------------
// Protected-mode rules from the Intel iAPX 286 PRM, chapters 6-9 (memory
// management, protection, multitasking, interrupts) and the instruction
// pages; clock counts from its timing appendix.

uint8_t Cpu::sys_rb(uint32_t a) {
    a &= 0xFFFFFFu;
    charge_byte(a);
    return fault_ ? uint8_t(0xFF) : bus_.read(a);
}
uint16_t Cpu::sys_rw(uint32_t a) {
    a &= 0xFFFFFFu;
    charge_word(a);
    if (fault_) return 0xFFFF;
    return uint16_t(bus_.read(a) | (uint16_t(bus_.read((a + 1) & 0xFFFFFFu)) << 8));
}
void Cpu::sys_wb(uint32_t a, uint8_t v) {
    a &= 0xFFFFFFu;
    charge_byte(a);
    if (!fault_) bus_.write(a, v);
}
void Cpu::sys_ww(uint32_t a, uint16_t v) {
    a &= 0xFFFFFFu;
    charge_word(a);
    if (fault_) return;
    bus_.write(a, uint8_t(v));
    bus_.write((a + 1) & 0xFFFFFFu, uint8_t(v >> 8));
}

void Cpu::pm_check(int si, uint16_t off, int len, bool write) {
    const SegCache &s = sc_[si];
    uint8_t vec = si == SEG_SS ? EXC_SS : EXC_GP;
    uint32_t last = uint32_t(off) + uint32_t(len - 1);
    if (s.access == 0) { raise(EXC_GP); return; }  // null selector
    if (write ? !acc_writable(s.access) : !acc_readable(s.access)) { raise(vec); return; }
    // Expand-down data: valid offsets are limit+1 through FFFFh.
    if (!acc_code(s.access) && (s.access & 0x04)) {
        if (off <= s.limit || last > 0xFFFF) raise(vec);
    } else if (last > s.limit) {
        raise(vec);
    }
}

bool Cpu::read_desc(uint16_t sel, uint8_t vec, Desc &d) {
    bool ldt = (sel & 0x0004) != 0;
    uint32_t idx = sel & 0xFFF8u;
    if ((ldt && (ldtr_sel_ & 0xFFFC) == 0) || idx + 7 > (ldt ? ldt_limit_ : gdt_limit_)) {
        raise_sel(vec, sel);
        return false;
    }
    d.addr = (ldt ? ldt_base_ : gdt_base_) + idx;
    d.w0 = sys_rw(d.addr);
    d.w1 = sys_rw(d.addr + 2);
    uint16_t w2 = sys_rw(d.addr + 4);
    d.b4 = uint8_t(w2);
    d.access = uint8_t(w2 >> 8);
    return !fault_;
}

// LAR/LSL/VERR/VERW report through ZF instead of faulting.
bool Cpu::probe_desc(uint16_t sel, Desc &d) {
    if ((sel & 0xFFFC) == 0) return false;
    bool ldt = (sel & 0x0004) != 0;
    uint32_t idx = sel & 0xFFF8u;
    if ((ldt && (ldtr_sel_ & 0xFFFC) == 0) || idx + 7 > (ldt ? ldt_limit_ : gdt_limit_)) return false;
    d.addr = (ldt ? ldt_base_ : gdt_base_) + idx;
    d.w0 = sys_rw(d.addr);
    d.w1 = sys_rw(d.addr + 2);
    uint16_t w2 = sys_rw(d.addr + 4);
    d.b4 = uint8_t(w2);
    d.access = uint8_t(w2 >> 8);
    return !fault_;
}

void Cpu::set_accessed(const Desc &d) {
    if (acc_seg(d.access) && !(d.access & 0x01)) sys_wb(d.addr + 5, uint8_t(d.access | 0x01));
}

void Cpu::commit_seg(int si, uint16_t sel, const Desc &d) {
    set_accessed(d);
    seg_reg(si) = sel;
    sc_[si] = SegCache{d.base(), d.w0, uint8_t(d.access | 0x01), sel};
}

void Cpu::load_cs(uint16_t sel, const Desc &d, int new_cpl) {
    uint16_t v = uint16_t((sel & 0xFFFC) | new_cpl);
    commit_seg(SEG_CS, v, d);
    cpl_ = uint8_t(new_cpl);
}

void Cpu::load_seg(int si, uint16_t sel) {
    if (!pe()) { load_seg_real(si, sel); return; }
    load_seg_pm(si, sel, EXC_GP);
}

void Cpu::load_seg_pm(int si, uint16_t sel, uint8_t vec) {
    if ((sel & 0xFFFC) == 0) {
        // Null is legal in DS and ES and faults on use; never in SS.
        if (si == SEG_SS) { raise_err(vec, 0); return; }
        seg_reg(si) = sel;
        sc_[si] = SegCache{0, 0, 0, sel};
        return;
    }
    Desc d;
    if (!read_desc(sel, vec, d)) return;
    uint8_t a = d.access;
    int rpl = sel & 3;
    if (si == SEG_SS) {
        if (!acc_writable(a) || rpl != cpl_ || acc_dpl(a) != cpl_) { raise_sel(vec, sel); return; }
        if (!acc_p(a)) { raise_sel(EXC_SS, sel); return; }
    } else {
        if (!acc_readable(a)) { raise_sel(vec, sel); return; }
        if (!acc_conforming(a) && std::max<int>(cpl_, rpl) > acc_dpl(a)) { raise_sel(vec, sel); return; }
        if (!acc_p(a)) { raise_sel(EXC_NP, sel); return; }
    }
    commit_seg(si, sel, d);
}

bool Cpu::code_target_ok(uint16_t sel, const Desc &d, bool check_rpl) {
    uint8_t a = d.access;
    if (!acc_code(a)) { raise_sel(EXC_GP, sel); return false; }
    if (acc_conforming(a)) {
        if (acc_dpl(a) > cpl_) { raise_sel(EXC_GP, sel); return false; }
    } else if ((check_rpl && (sel & 3) > cpl_) || acc_dpl(a) != cpl_) {
        raise_sel(EXC_GP, sel);
        return false;
    }
    if (!acc_p(a)) { raise_sel(EXC_NP, sel); return false; }
    return true;
}

// SS:SP for privilege level `dpl` from the current 286 TSS (bytes 2-13).
bool Cpu::inner_stack(int dpl, uint16_t &new_ss, uint16_t &new_sp, Desc &sd) {
    uint32_t off = 2 + uint32_t(dpl) * 4;
    if (off + 3 > tr_limit_) { raise_sel(EXC_TS, tr_sel_); return false; }
    new_sp = sys_rw(tr_base_ + off);
    new_ss = sys_rw(tr_base_ + off + 2);
    if (fault_) return false;
    if ((new_ss & 0xFFFC) == 0) { raise_sel(EXC_TS, new_ss); return false; }
    if (!read_desc(new_ss, EXC_TS, sd)) return false;
    if ((new_ss & 3) != dpl || acc_dpl(sd.access) != dpl || !acc_writable(sd.access)) {
        raise_sel(EXC_TS, new_ss);
        return false;
    }
    if (!acc_p(sd.access)) { raise_sel(EXC_SS, new_ss); return false; }
    return true;
}

// The outer level's SS:SP sits on the stack at `sp_at` on a return to RPL `rpl`.
bool Cpu::outer_return(uint16_t sp_at, int rpl, uint16_t &new_ss, uint16_t &new_sp, Desc &sd) {
    check(SEG_SS, sp_at, 4, false);
    new_sp = rw(SEG_SS, sp_at);
    new_ss = rw(SEG_SS, uint16_t(sp_at + 2));
    if (fault_) return false;
    if ((new_ss & 0xFFFC) == 0) { raise(EXC_GP); return false; }
    if (!read_desc(new_ss, EXC_GP, sd)) return false;
    if ((new_ss & 3) != rpl || acc_dpl(sd.access) != rpl || !acc_writable(sd.access)) {
        raise_sel(EXC_GP, new_ss);
        return false;
    }
    if (!acc_p(sd.access)) { raise_sel(EXC_SS, new_ss); return false; }
    return true;
}

// On a return to an outer level, DS and ES go null if that level can't use them.
void Cpu::drop_outer_segs() {
    for (int si : {SEG_ES, SEG_DS}) {
        uint8_t a = sc_[si].access;
        if (a == 0 || acc_conforming(a)) continue;
        if (acc_dpl(a) < cpl_) { seg_reg(si) = 0; sc_[si] = SegCache{0, 0, 0, 0}; }
    }
}

// IOPL changes only at CPL 0 and IF only when CPL <= IOPL; bit 15 stays clear.
void Cpu::set_flags_pm(uint16_t v) {
    uint16_t mask = 0x0FD5 | FLAG_NT;
    if (cpl_ == 0) mask |= FLAG_IOPL;
    if (cpl_ > ((flags >> 12) & 3)) mask = uint16_t(mask & ~FLAG_IF);
    flags = uint16_t(((flags & ~mask) | (v & mask) | FLAG_R1) & 0x7FFF);
}

// IN, OUT, INS, OUTS, CLI, STI and LOCK need CPL <= IOPL in protected mode.
bool Cpu::io_ok() {
    if (pe() && cpl_ > ((flags >> 12) & 3)) { raise(EXC_GP); return false; }
    return true;
}
bool Cpu::cpl0() {
    if (pe() && cpl_ != 0) { raise(EXC_GP); return false; }
    return true;
}

// JMP far: 11 real, 23 protected, 38 through a call gate, 175 to a TSS, 180 through a task gate.
int Cpu::far_jmp(uint16_t sel, uint16_t off) {
    if (!pe()) {
        load_seg_real(SEG_CS, sel);
        ip = off;
        return 0;
    }
    if ((sel & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    Desc d;
    if (!read_desc(sel, EXC_GP, d)) return 0;
    if (acc_seg(d.access)) {
        if (!code_target_ok(sel, d, true)) return 0;
        if (off > d.w0) { raise(EXC_GP); return 0; }
        load_cs(sel, d, cpl_);
        ip = off;
        return 12;
    }
    int t = sys_type(d.access), dpl = acc_dpl(d.access);
    if (t != SYS_CALL_GATE && t != SYS_TASK_GATE && t != SYS_TSS) { raise_sel(EXC_GP, sel); return 0; }
    if (dpl < cpl_ || dpl < (sel & 3)) { raise_sel(EXC_GP, sel); return 0; }
    if (!acc_p(d.access)) { raise_sel(EXC_NP, sel); return 0; }
    if (t == SYS_TSS) return 164 + task_switch(sel, Link::Jmp);
    if (t == SYS_TASK_GATE) return 169 + task_switch(d.w1, Link::Jmp);
    uint16_t tsel = d.w1;
    if ((tsel & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    Desc td;
    if (!read_desc(tsel, EXC_GP, td)) return 0;
    if (!code_target_ok(tsel, td, false)) return 0;
    if (d.w0 > td.w0) { raise(EXC_GP); return 0; }
    load_cs(tsel, td, cpl_);
    ip = d.w0;
    return 27;
}

// CALL far: 13 real, 26 protected, 41 through a call gate, 82 to an inner
// level (86+4x with x parameter words), 177 to a TSS, 182 through a task gate.
int Cpu::far_call(uint16_t sel, uint16_t off) {
    if (!pe()) {
        push16(cs); push16(ip);
        load_seg_real(SEG_CS, sel);
        ip = off;
        return 0;
    }
    if ((sel & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    Desc d;
    if (!read_desc(sel, EXC_GP, d)) return 0;
    if (acc_seg(d.access)) {
        if (!code_target_ok(sel, d, true)) return 0;
        if (off > d.w0) { raise(EXC_GP); return 0; }
        check(SEG_SS, uint16_t(sp - 4), 4, true);
        push16(cs); push16(ip);
        if (fault_) return 0;
        load_cs(sel, d, cpl_);
        ip = off;
        return 13;
    }
    int t = sys_type(d.access), dpl = acc_dpl(d.access);
    if (t != SYS_CALL_GATE && t != SYS_TASK_GATE && t != SYS_TSS) { raise_sel(EXC_GP, sel); return 0; }
    if (dpl < cpl_ || dpl < (sel & 3)) { raise_sel(EXC_GP, sel); return 0; }
    if (!acc_p(d.access)) { raise_sel(EXC_NP, sel); return 0; }
    if (t == SYS_TSS) return 164 + task_switch(sel, Link::Call);
    if (t == SYS_TASK_GATE) return 169 + task_switch(d.w1, Link::Call);
    uint16_t tsel = d.w1;
    if ((tsel & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    Desc td;
    if (!read_desc(tsel, EXC_GP, td)) return 0;
    if (!acc_code(td.access) || acc_dpl(td.access) > cpl_) { raise_sel(EXC_GP, tsel); return 0; }
    if (!acc_p(td.access)) { raise_sel(EXC_NP, tsel); return 0; }
    if (d.w0 > td.w0) { raise(EXC_GP); return 0; }
    int tdpl = acc_dpl(td.access);
    uint16_t old_cs = cs, old_ip = ip;
    if (!acc_conforming(td.access) && tdpl < cpl_) {
        uint16_t new_ss, new_sp;
        Desc sd;
        if (!inner_stack(tdpl, new_ss, new_sp, sd)) return 0;
        int words = d.b4 & 0x1F;
        uint16_t params[31];
        uint16_t old_ss = ss, old_sp = uint16_t(sp);
        check(SEG_SS, old_sp, words * 2, false);
        for (int i = 0; i < words; ++i) params[i] = rw(SEG_SS, uint16_t(old_sp + 2 * i));
        if (fault_) return 0;
        commit_seg(SEG_SS, new_ss, sd);
        sp = new_sp;
        load_cs(tsel, td, tdpl);
        push16(old_ss); push16(old_sp);
        for (int i = words - 1; i >= 0; --i) push16(params[i]);
        push16(old_cs); push16(old_ip);
        ip = d.w0;
        return words ? 73 + 4 * words : 69;
    }
    push16(old_cs); push16(old_ip);
    if (fault_) return 0;
    load_cs(tsel, td, cpl_);
    ip = d.w0;
    return 28;
}

// RET far: 15 real, 25 protected, 55 to an outer level.
int Cpu::far_ret(uint16_t pop_bytes) {
    if (!pe()) {
        uint16_t new_ip = pop16();
        uint16_t new_cs = pop16();
        sp = uint16_t(sp + pop_bytes);
        ip = new_ip;
        load_seg_real(SEG_CS, new_cs);
        return 0;
    }
    check(SEG_SS, uint16_t(sp), 4, false);
    uint16_t new_ip = rw(SEG_SS, uint16_t(sp));
    uint16_t new_cs = rw(SEG_SS, uint16_t(sp + 2));
    if (fault_) return 0;
    int rpl = new_cs & 3;
    if ((new_cs & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    if (rpl < cpl_) { raise_sel(EXC_GP, new_cs); return 0; }
    Desc d;
    if (!read_desc(new_cs, EXC_GP, d)) return 0;
    uint8_t a = d.access;
    if (!acc_code(a) || (acc_conforming(a) ? acc_dpl(a) > rpl : acc_dpl(a) != rpl)) { raise_sel(EXC_GP, new_cs); return 0; }
    if (!acc_p(a)) { raise_sel(EXC_NP, new_cs); return 0; }
    if (new_ip > d.w0) { raise(EXC_GP); return 0; }
    if (rpl == cpl_) {
        sp = uint16_t(sp + 4 + pop_bytes);
        load_cs(new_cs, d, rpl);
        ip = new_ip;
        return 10;
    }
    uint16_t new_ss, new_sp;
    Desc sd;
    if (!outer_return(uint16_t(sp + 4 + pop_bytes), rpl, new_ss, new_sp, sd)) return 0;
    load_cs(new_cs, d, rpl);
    commit_seg(SEG_SS, new_ss, sd);
    sp = uint16_t(new_sp + pop_bytes);
    ip = new_ip;
    drop_outer_segs();
    return 40;
}

// IRET: 17 real, 31 protected, 55 to an outer level, 169 to the task named by NT's back link.
int Cpu::iret() {
    if (!pe()) {
        uint16_t new_ip = pop16();
        uint16_t new_cs = pop16();
        // FLAGS bits 12-15 are always 0 on a 286 in real mode (Intel AP-485 CPU identification).
        flags = uint16_t((pop16() & 0x0FD5) | FLAG_R1);
        ip = new_ip;
        load_seg_real(SEG_CS, new_cs);
        return 0;
    }
    if (flag(FLAG_NT)) {
        uint16_t back = sys_rw(tr_base_);
        if (fault_) return 0;
        return 152 + task_switch(back, Link::Iret);
    }
    check(SEG_SS, uint16_t(sp), 6, false);
    uint16_t new_ip = rw(SEG_SS, uint16_t(sp));
    uint16_t new_cs = rw(SEG_SS, uint16_t(sp + 2));
    uint16_t new_flags = rw(SEG_SS, uint16_t(sp + 4));
    if (fault_) return 0;
    int rpl = new_cs & 3;
    if ((new_cs & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    if (rpl < cpl_) { raise_sel(EXC_GP, new_cs); return 0; }
    Desc d;
    if (!read_desc(new_cs, EXC_GP, d)) return 0;
    uint8_t a = d.access;
    if (!acc_code(a) || (acc_conforming(a) ? acc_dpl(a) > rpl : acc_dpl(a) != rpl)) { raise_sel(EXC_GP, new_cs); return 0; }
    if (!acc_p(a)) { raise_sel(EXC_NP, new_cs); return 0; }
    if (new_ip > d.w0) { raise(EXC_GP); return 0; }
    if (rpl == cpl_) {
        sp = uint16_t(sp + 6);
        set_flags_pm(new_flags);
        load_cs(new_cs, d, rpl);
        ip = new_ip;
        return 14;
    }
    uint16_t new_ss, new_sp;
    Desc sd;
    if (!outer_return(uint16_t(sp + 6), rpl, new_ss, new_sp, sd)) return 0;
    set_flags_pm(new_flags);
    load_cs(new_cs, d, rpl);
    commit_seg(SEG_SS, new_ss, sd);
    sp = new_sp;
    ip = new_ip;
    drop_outer_segs();
    return 38;
}

// Through an interrupt or trap gate: 40 at the same level, 78 to an inner
// level, 167 through a task gate (INT n's protected-mode counts).
int Cpu::pm_interrupt(uint8_t vector, Kind kind, uint16_t err) {
    uint16_t idx = uint16_t(vector * 8);
    uint16_t gate_err = uint16_t(idx + 2);  // IDT bit
    if (uint32_t(idx) + 7 > idt_limit_) { raise_err(EXC_GP, gate_err); return 0; }
    uint32_t ga = idt_base_ + idx;
    uint16_t g_off = sys_rw(ga), g_sel = sys_rw(ga + 2);
    uint8_t g_acc = uint8_t(sys_rw(ga + 4) >> 8);
    if (fault_) return 0;
    int t = sys_type(g_acc);
    if (acc_seg(g_acc) || (t != SYS_TASK_GATE && t != SYS_INT_GATE && t != SYS_TRAP_GATE)) { raise_err(EXC_GP, gate_err); return 0; }
    if (kind == Kind::Soft && acc_dpl(g_acc) < cpl_) { raise_err(EXC_GP, gate_err); return 0; }
    if (!acc_p(g_acc)) { raise_err(EXC_NP, gate_err); return 0; }
    // #DF, #TS, #NP, #SS and #GP push an error code.
    bool push_err = kind == Kind::Exception && (vector == EXC_DF || (vector >= EXC_TS && vector <= EXC_GP));
    if (t == SYS_TASK_GATE) {
        int c = 144 + task_switch(g_sel, Link::Call);
        if (push_err && !fault_) push16(err);
        return c;
    }
    if ((g_sel & 0xFFFC) == 0) { raise(EXC_GP); return 0; }
    Desc d;
    if (!read_desc(g_sel, EXC_GP, d)) return 0;
    if (!acc_code(d.access) || acc_dpl(d.access) > cpl_) { raise_sel(EXC_GP, g_sel); return 0; }
    if (!acc_p(d.access)) { raise_sel(EXC_NP, g_sel); return 0; }
    if (g_off > d.w0) { raise(EXC_GP); return 0; }
    uint16_t old_flags = flags, old_cs = cs, old_ip = ip;
    int dpl = acc_dpl(d.access);
    int extra = 17;
    if (!acc_conforming(d.access) && dpl < cpl_) {
        uint16_t new_ss, new_sp;
        Desc sd;
        if (!inner_stack(dpl, new_ss, new_sp, sd)) return 0;
        uint16_t old_ss = ss, old_sp = uint16_t(sp);
        commit_seg(SEG_SS, new_ss, sd);
        sp = new_sp;
        load_cs(g_sel, d, dpl);
        push16(old_ss); push16(old_sp);
        extra = 55;
    } else {
        load_cs(g_sel, d, cpl_);
    }
    push16(old_flags); push16(old_cs); push16(old_ip);
    if (push_err) push16(err);
    if (t == SYS_INT_GATE) set_flag(FLAG_IF, false);
    set_flag(FLAG_TF, false);
    set_flag(FLAG_NT, false);
    ip = g_off;
    return extra;
}

// 286 TSS: back link 0, SP0/SS0..SP2/SS2 2-13, IP 14, FLAGS 16, AX CX DX BX
// SP BP SI DI 18-33, ES CS SS DS 34-41, LDT 42 (Intel iAPX 286 PRM, "Task
// State Segment"). Returns 0; callers charge the switch.
int Cpu::task_switch(uint16_t sel, Link link) {
    uint8_t bad = link == Link::Iret ? EXC_TS : EXC_GP;
    if (sel & 0x0004) { raise_sel(bad, sel); return 0; }
    Desc d;
    if (!read_desc(sel, bad, d)) return 0;
    int t = sys_type(d.access);
    if (acc_seg(d.access) || t != (link == Link::Iret ? SYS_TSS_BUSY : SYS_TSS)) { raise_sel(bad, sel); return 0; }
    if (!acc_p(d.access)) { raise_sel(EXC_NP, sel); return 0; }
    if (d.w0 < 43) { raise_sel(EXC_TS, sel); return 0; }
    if (tr_limit_ < 43) { raise_sel(EXC_TS, tr_sel_); return 0; }

    uint32_t old = tr_base_;
    uint16_t saved_flags = link == Link::Iret ? uint16_t(flags & ~FLAG_NT) : flags;
    const uint16_t state[14] = {ip, saved_flags, uint16_t(ax), uint16_t(cx), uint16_t(dx), uint16_t(bx),
                                uint16_t(sp), uint16_t(bp), uint16_t(si), uint16_t(di), es, cs, ss, ds};
    for (int i = 0; i < 14; ++i) sys_ww(old + 14 + 2 * i, state[i]);
    if (link != Link::Call) {
        uint32_t od = gdt_base_ + (tr_sel_ & 0xFFF8u) + 5;
        sys_wb(od, uint8_t(sys_rb(od) & ~0x02));
    }
    if (link != Link::Iret) sys_wb(d.addr + 5, uint8_t(d.access | 0x02));
    uint32_t nb = d.base();
    if (link == Link::Call) sys_ww(nb, tr_sel_);
    uint16_t n[15];
    for (int i = 0; i < 15; ++i) n[i] = sys_rw(nb + 14 + 2 * i);
    if (fault_) return 0;

    msw_ = uint16_t(msw_ | MSW_TS);
    tr_sel_ = sel;
    tr_base_ = nb;
    tr_limit_ = d.w0;
    ip = n[0];
    flags = uint16_t(((n[1] | (link == Link::Call ? FLAG_NT : 0)) | FLAG_R1) & 0x7FFF);
    set_reg16(0, n[2]); set_reg16(1, n[3]); set_reg16(2, n[4]); set_reg16(3, n[5]);
    set_reg16(4, n[6]); set_reg16(5, n[7]); set_reg16(6, n[8]); set_reg16(7, n[9]);
    es = n[10]; cs = n[11]; ss = n[12]; ds = n[13];
    uint16_t ldt = n[14];
    cpl_ = uint8_t(cs & 3);
    for (int i = 0; i < 4; ++i) sc_[i] = SegCache{0, 0, 0, seg_reg(i)};
    ldtr_sel_ = 0;
    // Faults from here on belong to the new task.
    instr_start_ip_ = ip;
    save_regs();

    if ((ldt & 0xFFFC) != 0) {
        Desc ld;
        if (ldt & 0x0004) { raise_sel(EXC_TS, ldt); return 0; }
        if (!read_desc(ldt, EXC_TS, ld)) return 0;
        if (acc_seg(ld.access) || sys_type(ld.access) != SYS_LDT) { raise_sel(EXC_TS, ldt); return 0; }
        if (!acc_p(ld.access)) { raise_sel(EXC_TS, ldt); return 0; }  // #TS, not #NP (iAPX 286 PRM, task-switch checks)
        ldt_base_ = ld.base();
        ldt_limit_ = ld.w0;
    }
    ldtr_sel_ = ldt;
    uint16_t new_cs = cs;
    if ((new_cs & 0xFFFC) == 0) { raise_sel(EXC_TS, new_cs); return 0; }
    Desc cd;
    if (!read_desc(new_cs, EXC_TS, cd)) return 0;
    uint8_t a = cd.access;
    int rpl = new_cs & 3;
    if (!acc_code(a) || (acc_conforming(a) ? acc_dpl(a) > rpl : acc_dpl(a) != rpl)) { raise_sel(EXC_TS, new_cs); return 0; }
    if (!acc_p(a)) { raise_sel(EXC_NP, new_cs); return 0; }
    load_cs(new_cs, cd, rpl);
    load_seg_pm(SEG_SS, n[12], EXC_TS);
    load_seg_pm(SEG_DS, n[13], EXC_TS);
    load_seg_pm(SEG_ES, n[10], EXC_TS);
    return 0;
}

// 0F 00: SLDT, STR (2 reg / 3 mem), LLDT, LTR (17 / 19), VERR, VERW (14 / 16).
int Cpu::system_0f00() {
    RM rm = decode_modrm();
    switch (last_reg_) {
        case 0: rm_write16(rm, ldtr_sel_); return rm.is_mem ? 3 : 2;
        case 1: rm_write16(rm, tr_sel_); return rm.is_mem ? 3 : 2;
        case 2: {  // LLDT
            uint16_t sel = rm_read16(rm);
            if (!cpl0()) return 17;
            if ((sel & 0xFFFC) == 0) { ldtr_sel_ = sel; ldt_base_ = 0; ldt_limit_ = 0; return rm.is_mem ? 19 : 17; }
            if (sel & 0x0004) { raise_sel(EXC_GP, sel); return 17; }
            Desc d;
            if (!read_desc(sel, EXC_GP, d)) return 17;
            if (acc_seg(d.access) || sys_type(d.access) != SYS_LDT) { raise_sel(EXC_GP, sel); return 17; }
            if (!acc_p(d.access)) { raise_sel(EXC_NP, sel); return 17; }
            ldtr_sel_ = sel;
            ldt_base_ = d.base();
            ldt_limit_ = d.w0;
            return rm.is_mem ? 19 : 17;
        }
        case 3: {  // LTR
            uint16_t sel = rm_read16(rm);
            if (!cpl0()) return 17;
            if ((sel & 0xFFFC) == 0 || (sel & 0x0004)) { raise_sel(EXC_GP, sel); return 17; }
            Desc d;
            if (!read_desc(sel, EXC_GP, d)) return 17;
            if (acc_seg(d.access) || sys_type(d.access) != SYS_TSS) { raise_sel(EXC_GP, sel); return 17; }
            if (!acc_p(d.access)) { raise_sel(EXC_NP, sel); return 17; }
            sys_wb(d.addr + 5, uint8_t(d.access | 0x02));
            tr_sel_ = sel;
            tr_base_ = d.base();
            tr_limit_ = d.w0;
            return rm.is_mem ? 19 : 17;
        }
        case 4: case 5: {  // VERR, VERW
            uint16_t sel = rm_read16(rm);
            Desc d;
            bool ok = probe_desc(sel, d) && acc_seg(d.access);
            if (ok) {
                uint8_t a = d.access;
                ok = last_reg_ == 4 ? acc_readable(a) : acc_writable(a);
                if (ok && !acc_conforming(a)) ok = std::max<int>(cpl_, sel & 3) <= acc_dpl(a);
            }
            set_flag(FLAG_ZF, ok);
            return rm.is_mem ? 16 : 14;
        }
        default: ud(); return 3;
    }
}

// LAR puts the access byte in the high byte; LSL loads the limit word.
int Cpu::lar_lsl(bool lsl) {
    RM rm = decode_modrm();
    uint16_t sel = rm_read16(rm);
    Desc d;
    bool ok = probe_desc(sel, d);
    if (ok) {
        uint8_t a = d.access;
        if (!acc_seg(a)) {
            int t = sys_type(a);
            ok = lsl ? (t >= SYS_TSS && t <= SYS_TSS_BUSY) : (t >= SYS_TSS && t <= SYS_TRAP_GATE);
        }
        if (ok && !acc_conforming(a)) ok = std::max<int>(cpl_, sel & 3) <= acc_dpl(a);
    }
    if (ok) set_reg16(last_reg_, lsl ? d.w0 : uint16_t(d.access << 8));
    set_flag(FLAG_ZF, ok);
    return rm.is_mem ? 16 : 14;
}

// --- condition codes, control flow ---------------------------------------

bool Cpu::cond(int cc) const {
    switch (cc & 0xF) {
        case 0x0: return flag(FLAG_OF);
        case 0x1: return !flag(FLAG_OF);
        case 0x2: return flag(FLAG_CF);
        case 0x3: return !flag(FLAG_CF);
        case 0x4: return flag(FLAG_ZF);
        case 0x5: return !flag(FLAG_ZF);
        case 0x6: return flag(FLAG_CF) || flag(FLAG_ZF);
        case 0x7: return !flag(FLAG_CF) && !flag(FLAG_ZF);
        case 0x8: return flag(FLAG_SF);
        case 0x9: return !flag(FLAG_SF);
        case 0xA: return flag(FLAG_PF);
        case 0xB: return !flag(FLAG_PF);
        case 0xC: return flag(FLAG_SF) != flag(FLAG_OF);
        case 0xD: return flag(FLAG_SF) == flag(FLAG_OF);
        case 0xE: return flag(FLAG_ZF) || (flag(FLAG_SF) != flag(FLAG_OF));
        default:  return !flag(FLAG_ZF) && (flag(FLAG_SF) == flag(FLAG_OF));
    }
}
void Cpu::jcc(bool taken) {
    int8_t rel = int8_t(fetch8());
    if (taken) ip = uint16_t(ip + rel);
}
int Cpu::loop_group(uint8_t op) {
    int8_t rel = int8_t(fetch8());
    bool take;
    if (op == 0xE3) {  // JCXZ
        take = (cx == 0);
        if (take) ip = uint16_t(ip + rel);
    } else {
        cx = uint16_t(cx - 1);
        if (op == 0xE0) take = (cx != 0) && !flag(FLAG_ZF);
        else if (op == 0xE1) take = (cx != 0) && flag(FLAG_ZF);
        else take = (cx != 0);
        if (take) ip = uint16_t(ip + rel);
    }
    // Taken costs 8-11 (iAPX 286 PRM timing appendix): floor 8 plus
    // kQueueRefillTax. Not taken pays no flush.
    if (take) return 8 + flush();
    return 4;  // all four not taken (HelpPC 2.10, 286 column)
}

// --- string instructions ---------------------------------------------------

int Cpu::string_op(uint8_t op) {
    bool wide = (op & 1) != 0;
    bool is_rep = (rep_ != REP_NONE);
    int src_seg = (seg_override_ >= 0) ? seg_override_ : SEG_DS;
    bool is_cmp_scan = (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF);
    // 0x66 widens word string ops to dword (REP MOVSD); byte forms are unaffected.
    bool dword = wide && opsize32_;
    int iterations = 0;  // actually executed; REPE/REPNE can stop early
    const int per_iteration = (op == 0xA4 || op == 0xA5) ? 4 : (op == 0xA6 || op == 0xA7) ? 9
                            : (op == 0xAA || op == 0xAB) ? 3 : (op == 0xAC || op == 0xAD) ? 4 : 8;
    do {
        if (is_rep && cx == 0) break;
        if (is_rep) save_regs();
        int step = dword ? 4 : (wide ? 2 : 1);
        int dir = flag(FLAG_DF) ? -step : step;
        switch (op) {
            case 0xA4: case 0xA5:  // MOVS
                if (!wide) wb(SEG_ES, di, rb(src_seg, si));
                else if (dword) wd(SEG_ES, di, rd(src_seg, si));
                else ww(SEG_ES, di, rw(src_seg, si));
                si = uint16_t(si + dir); di = uint16_t(di + dir);
                break;
            case 0xA6: case 0xA7:  // CMPS
                if (!wide) sub8(rb(src_seg, si), rb(SEG_ES, di), false);
                else if (dword) sub32(rd(src_seg, si), rd(SEG_ES, di), false);
                else sub16(rw(src_seg, si), rw(SEG_ES, di), false);
                si = uint16_t(si + dir); di = uint16_t(di + dir);
                break;
            case 0xAA: case 0xAB:  // STOS
                if (!wide) wb(SEG_ES, di, get_reg8(0));
                else if (dword) wd(SEG_ES, di, ax);
                else ww(SEG_ES, di, uint16_t(ax));
                di = uint16_t(di + dir);
                break;
            case 0xAC: case 0xAD:  // LODS
                if (!wide) set_reg8(0, rb(src_seg, si));
                else if (dword) ax = rd(src_seg, si);
                else ax = (ax & 0xFFFF0000u) | rw(src_seg, si);
                si = uint16_t(si + dir);
                break;
            case 0xAE: case 0xAF:  // SCAS
                if (!wide) sub8(get_reg8(0), rb(SEG_ES, di), false);
                else if (dword) sub32(ax, rd(SEG_ES, di), false);
                else sub16(uint16_t(ax), rw(SEG_ES, di), false);
                di = uint16_t(di + dir);
                break;
        }
        ++iterations;
        if (!is_rep) break;
        cx = uint16_t(cx - 1);
        if (fault_) break;
        if (is_cmp_scan) {
            bool z = flag(FLAG_ZF);
            if (rep_ == REP_Z && !z) break;   // REPE/REPZ: stop once not-equal
            if (rep_ == REP_NZ && z) break;   // REPNE/REPNZ: stop once equal
        }
        if (cx != 0 && rep_yield(iterations, per_iteration)) break;
    } while (cx != 0);
    if (is_rep && rep_resumed_) return iterations * per_iteration;
    // REP runs cost fixed overhead plus a per-iteration cost (iAPX 286 PRM
    // timing appendix), scaled by the iterations that actually ran.
    switch (op) {
        case 0xA4: case 0xA5: return is_rep ? (5 + 4 * iterations) : 5;  // MOVS
        case 0xA6: case 0xA7: return is_rep ? (5 + 9 * iterations) : 8;  // CMPS
        case 0xAA: case 0xAB: return is_rep ? (4 + 3 * iterations) : 3;  // STOS
        case 0xAC: case 0xAD: return is_rep ? (5 + 4 * iterations) : 5;  // LODS
        default:              return is_rep ? (5 + 8 * iterations) : 7;  // SCAS
    }
}
int Cpu::io_string_op(uint8_t op) {
    bool wide = (op & 1) != 0;
    bool is_rep = (rep_ != REP_NONE);  // only plain REP applies
    int src_seg = (seg_override_ >= 0) ? seg_override_ : SEG_DS;
    int iterations = 0;
    do {
        if (is_rep && cx == 0) break;
        if (is_rep) save_regs();
        int step = wide ? 2 : 1;
        int dir = flag(FLAG_DF) ? -step : step;
        if (op == 0x6C || op == 0x6D) {  // INS: port DX -> ES:DI
            if (!wide) wb(SEG_ES, di, in8(uint16_t(dx)));
            else ww(SEG_ES, di, in16(uint16_t(dx)));
            di = uint16_t(di + dir);
        } else {  // OUTS: DS:SI -> port DX
            if (!wide) out8(uint16_t(dx), rb(src_seg, si));
            else {
                uint16_t v = rw(src_seg, si);
                out16(uint16_t(dx), v);
            }
            si = uint16_t(si + dir);
        }
        ++iterations;
        if (!is_rep) break;
        cx = uint16_t(cx - 1);
        if (fault_) break;
        if (cx != 0 && rep_yield(iterations, 4)) break;
    } while (cx != 0);
    if (is_rep && rep_resumed_) return iterations * 4;
    // INS/OUTS: 5 non-rep, 5+4n rep (iAPX 286 PRM timing appendix).
    return is_rep ? (5 + 4 * iterations) : 5;
}

// The 286 takes interrupts and the single-step trap between REP iterations,
// with IP still on the instruction (Intel iAPX 286 PRM, string instructions).
bool Cpu::rep_yield(int iterations, int per_iteration) {
    if (step_each_) { ip = instr_start_ip_; return true; }
    if (rep_yield_cycles != 0 && uint32_t(iterations * per_iteration) >= rep_yield_cycles) {
        ip = instr_start_ip_;
        rep_resume_ = true;
        return true;
    }
    return false;
}

// --- opcode groups (reg field of ModR/M selects the operation) -----------

int Cpu::grp1_immed(uint8_t op) {  // 0x80/0x82: r/m8,imm8  0x81: r/m16/32,imm16/32  0x83: r/m16/32,imm8(sx)
    RM rm = decode_modrm();
    int alu = last_reg_;
    bool wide = (op == 0x81 || op == 0x83);
    if (!wide) {
        uint8_t imm = fetch8();
        uint8_t res = alu_apply8(alu, rm_read8(rm), imm);
        if (alu != 7) rm_write8(rm, res);
    } else if (opsize32_) {
        uint32_t imm;
        if (op == 0x83) { int8_t d = int8_t(fetch8()); imm = uint32_t(int32_t(d)); }
        else imm = fetch32();
        uint32_t res = alu_apply32(alu, rm_read32(rm), imm);
        if (alu != 7) rm_write32(rm, res);
    } else {
        uint16_t imm;
        if (op == 0x83) { int8_t d = int8_t(fetch8()); imm = uint16_t(int16_t(d)); }
        else imm = fetch16();
        uint16_t res = alu_apply16(alu, rm_read16(rm), imm);
        if (alu != 7) rm_write16(rm, res);
    }
    // reg 3, mem 7, CMP mem 6 (iAPX 286 PRM timing appendix).
    return rm.is_mem ? (alu == 7 ? 6 : 7) : 3;
}
int Cpu::grp2_shift(uint8_t op) {  // 0xC0/0xD0/0xD2: 8-bit  0xC1/0xD1/0xD3: 16-bit
    RM rm = decode_modrm();
    int alu = last_reg_;
    bool wide = (op == 0xC1 || op == 0xD1 || op == 0xD3);
    int count;
    if (op == 0xC0 || op == 0xC1) count = fetch8();
    else if (op == 0xD0 || op == 0xD1) count = 1;
    else count = get_reg8(1);  // CL
    count &= 0x1F;  // the 286 masks the count before it runs, so 5+n has n <= 31
    if (!wide) {
        uint8_t res = shiftrot8(alu, rm_read8(rm), count);
        if (count != 0) rm_write8(rm, res);
    } else if (opsize32_) {
        uint32_t res = shiftrot32(alu, rm_read32(rm), count);
        if (count != 0) rm_write32(rm, res);
    } else {
        uint16_t res = shiftrot16(alu, rm_read16(rm), count);
        if (count != 0) rm_write16(rm, res);
    }
    // Shift-by-1 (D0/D1) has its own cost. CL and imm8 forms scale with count (iAPX 286 PRM timing appendix).
    if (op == 0xD0 || op == 0xD1) return rm.is_mem ? 7 : 2;
    return rm.is_mem ? (8 + count) : (5 + count);
}
int Cpu::grp3_unary(uint8_t op) {  // 0xF6: r/m8  0xF7: r/m16, TEST/NOT/NEG/MUL/IMUL/DIV/IDIV
    RM rm = decode_modrm();
    int alu = last_reg_;
    bool wide = (op == 0xF7);
    if (!wide) {
        uint8_t v = rm_read8(rm);
        switch (alu) {
            case 0: case 1: { uint8_t imm = fetch8(); and8(v, imm); break; }  // TEST
            case 2: rm_write8(rm, uint8_t(~v)); break;                        // NOT
            case 3: { bool nz = v != 0; rm_write8(rm, sub8(0, v, false)); set_flag(FLAG_CF, nz); break; }  // NEG
            case 4: {  // MUL
                unsigned r = unsigned(get_reg8(0)) * unsigned(v);
                ax = uint16_t(r);
                bool ov = (ax >> 8) != 0;
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 5: {  // IMUL
                int r = int(int8_t(get_reg8(0))) * int(int8_t(v));
                ax = uint16_t(r);
                bool ov = (int8_t(uint8_t(r)) != r);
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 6: {  // DIV
                if (v == 0) { raise(EXC_DE); break; }
                int q = ax / v, rem = ax % v;
                if (q > 0xFF) { raise(EXC_DE); break; }
                ax = uint16_t((uint16_t(uint8_t(rem)) << 8) | uint8_t(q));
                break;
            }
            default: {  // IDIV
                int16_t dividend = int16_t(ax);
                int8_t divisor = int8_t(v);
                if (divisor == 0) { raise(EXC_DE); break; }
                int q = dividend / divisor, rem = dividend % divisor;
                if (q > 127 || q < -128) { raise(EXC_DE); break; }
                ax = uint16_t((uint16_t(uint8_t(int8_t(rem))) << 8) | uint8_t(int8_t(q)));
                break;
            }
        }
    } else if (opsize32_) {
        uint32_t v = rm_read32(rm);
        switch (alu) {
            case 0: case 1: { uint32_t imm = fetch32(); and32(v, imm); break; }
            case 2: rm_write32(rm, ~v); break;
            case 3: { bool nz = v != 0; rm_write32(rm, sub32(0, v, false)); set_flag(FLAG_CF, nz); break; }
            case 4: {
                uint64_t r = uint64_t(ax) * v;
                ax = uint32_t(r); dx = uint32_t(r >> 32);
                bool ov = dx != 0;
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 5: {
                int64_t r = int64_t(int32_t(ax)) * int64_t(int32_t(v));
                ax = uint32_t(r); dx = uint32_t(uint64_t(r) >> 32);
                bool ov = (int64_t(int32_t(uint32_t(r))) != r);
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 6: {
                uint64_t dividend = (uint64_t(dx) << 32) | ax;
                if (v == 0) { raise(EXC_DE); break; }
                uint64_t q = dividend / v, rem = dividend % v;
                if (q > 0xFFFFFFFFull) { raise(EXC_DE); break; }
                ax = uint32_t(q); dx = uint32_t(rem);
                break;
            }
            default: {
                int64_t dividend = int64_t((uint64_t(dx) << 32) | ax);
                int32_t divisor = int32_t(v);
                if (divisor == 0 || (divisor == -1 && dividend == INT64_MIN)) { raise(EXC_DE); break; }
                int64_t q = dividend / divisor, rem = dividend % divisor;
                if (q > 2147483647ll || q < -2147483648ll) { raise(EXC_DE); break; }
                ax = uint32_t(int32_t(q)); dx = uint32_t(int32_t(rem));
                break;
            }
        }
    } else {
        uint16_t v = rm_read16(rm);
        switch (alu) {
            case 0: case 1: { uint16_t imm = fetch16(); and16(v, imm); break; }
            case 2: rm_write16(rm, uint16_t(~v)); break;
            case 3: { bool nz = v != 0; rm_write16(rm, sub16(0, v, false)); set_flag(FLAG_CF, nz); break; }
            case 4: {
                uint32_t r = uint32_t(uint16_t(ax)) * v;
                ax = (ax & 0xFFFF0000u) | uint16_t(r); dx = (dx & 0xFFFF0000u) | uint16_t(r >> 16);
                bool ov = uint16_t(dx) != 0;
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 5: {
                int32_t r = int32_t(int16_t(ax)) * int32_t(int16_t(v));
                ax = (ax & 0xFFFF0000u) | uint16_t(r); dx = (dx & 0xFFFF0000u) | uint16_t(uint32_t(r) >> 16);
                bool ov = (int32_t(int16_t(uint16_t(r))) != r);
                set_flag(FLAG_CF, ov); set_flag(FLAG_OF, ov);
                break;
            }
            case 6: {
                uint32_t dividend = (uint32_t(uint16_t(dx)) << 16) | uint16_t(ax);
                if (v == 0) { raise(EXC_DE); break; }
                uint32_t q = dividend / v, rem = dividend % v;
                if (q > 0xFFFF) { raise(EXC_DE); break; }
                ax = (ax & 0xFFFF0000u) | uint16_t(q); dx = (dx & 0xFFFF0000u) | uint16_t(rem);
                break;
            }
            default: {
                int64_t dividend = int32_t((uint32_t(uint16_t(dx)) << 16) | uint16_t(ax));
                int64_t divisor = int16_t(v);
                if (divisor == 0) { raise(EXC_DE); break; }
                int64_t q = dividend / divisor, rem = dividend % divisor;
                if (q > 32767 || q < -32768) { raise(EXC_DE); break; }
                ax = (ax & 0xFFFF0000u) | uint16_t(int16_t(q)); dx = (dx & 0xFFFF0000u) | uint16_t(int16_t(rem));
                break;
            }
        }
    }
    // iAPX 286 PRM timing appendix. The 0x66 forms reuse the 16-bit numbers; no 286 timing exists for them.
    switch (alu) {
        case 0: case 1: return rm.is_mem ? 6 : 3;                                   // TEST
        case 2: case 3: return rm.is_mem ? 7 : 2;                                   // NOT, NEG
        case 4: case 5: return wide ? (rm.is_mem ? 24 : 21) : (rm.is_mem ? 16 : 13); // MUL, IMUL
        case 6:         return wide ? (rm.is_mem ? 25 : 22) : (rm.is_mem ? 17 : 14); // DIV
        default:        return wide ? (rm.is_mem ? 28 : 25) : (rm.is_mem ? 20 : 17); // IDIV
    }
}
int Cpu::grp5(uint8_t op) {  // 0xFE: INC/DEC r/m8   0xFF: INC/DEC/CALL/JMP/PUSH r/m16
    RM rm = decode_modrm();
    int alu = last_reg_;
    if (op == 0xFE) {
        bool cf = flag(FLAG_CF);
        if (alu == 0) { uint8_t r = add8(rm_read8(rm), 1, false); set_flag(FLAG_CF, cf); rm_write8(rm, r); }
        else if (alu == 1) { uint8_t r = sub8(rm_read8(rm), 1, false); set_flag(FLAG_CF, cf); rm_write8(rm, r); }
        else return 7;
        return rm.is_mem ? 7 : 2;
    }
    switch (alu) {
        case 0: {
            bool cf = flag(FLAG_CF);
            if (opsize32_) { uint32_t r = add32(rm_read32(rm), 1, false); set_flag(FLAG_CF, cf); rm_write32(rm, r); }
            else { uint16_t r = add16(rm_read16(rm), 1, false); set_flag(FLAG_CF, cf); rm_write16(rm, r); }
            return rm.is_mem ? 7 : 2;
        }
        case 1: {
            bool cf = flag(FLAG_CF);
            if (opsize32_) { uint32_t r = sub32(rm_read32(rm), 1, false); set_flag(FLAG_CF, cf); rm_write32(rm, r); }
            else { uint16_t r = sub16(rm_read16(rm), 1, false); set_flag(FLAG_CF, cf); rm_write16(rm, r); }
            return rm.is_mem ? 7 : 2;
        }
        // Indirect transfers: reg 7, mem 11, far 16 (CALL) / 15 (JMP), each plus the refill tax.
        case 2: { uint16_t target = rm_read16(rm); push16(ip); ip = target; return (rm.is_mem ? 11 : 7) + flush(); }  // CALL near indirect
        case 3: {                                                                                    // CALL far indirect (memory only)
            uint16_t off, seg;
            if (!far_operand(rm, off, seg)) return 3;
            return 16 + far_call(seg, off) + flush();
        }
        case 4: ip = rm_read16(rm); return (rm.is_mem ? 11 : 7) + flush();                          // JMP near indirect
        case 5: {                                                                                    // JMP far indirect (memory only)
            uint16_t off, seg;
            if (!far_operand(rm, off, seg)) return 3;
            return 15 + far_jmp(seg, off) + flush();
        }
        case 6: { if (opsize32_) push32(rm_read32(rm)); else push16(rm_read16(rm)); return rm.is_mem ? 5 : 3; }  // PUSH r/m16/32
        default: ud(); return 3;
    }
}

// --- main decode loop ------------------------------------------------------

int Cpu::step() {
    if (halted) {
        IBMPCAT_PERF_ADD(perf.cls[kPerfHlt], 2);
        cycles += 2;
        return 2;  // idling for an interrupt, or in shutdown until RESET
    }
#ifdef IBMPCAT_PERF
    perf_tax_step_ = 0;
#endif

    seg_override_ = -1;
    rep_ = REP_NONE;
    opsize32_ = false;
    extra_cycles_ = 0;
    instr_start_ip_ = ip;
    rep_resumed_ = rep_resume_;
    rep_resume_ = false;
    // Single-step traps after an instruction that began with TF set, so a POPF
    // that sets TF runs untrapped and one that clears it still traps.
    bool trap = flag(FLAG_TF);
    step_each_ = trap;
    shadow_ = vectored_ = false;
    fault_ = false;
    fetched_ = 0;
    fetch_wrapped_ = false;
    bus_busy_ = bus_extra_ = 0;
    transfer_ = false;
    if (!pe()) refresh_real_bases();
    save_regs();
    int c = 0;

    uint8_t op;
    for (;;) {
        op = fetch8();
        bool is_prefix = true;
        switch (op) {
            case 0x26: seg_override_ = SEG_ES; break;
            case 0x2E: seg_override_ = SEG_CS; break;
            case 0x36: seg_override_ = SEG_SS; break;
            case 0x3E: seg_override_ = SEG_DS; break;
            case 0x66: if (!firmware()) ud(); opsize32_ = true; break;  // 386 operand-size override
            // LOCK is IOPL-sensitive in protected mode (Intel iAPX 286 PRM); no other bus master to lock out.
            case 0xF0: io_ok(); break;
            case 0xF1: io_ok(); break;    // LOCK alias on the 286, tested on hardware (rep lodsb, "Intel 286 secrets")
            case 0xF2: rep_ = REP_NZ; break;
            case 0xF3: rep_ = REP_Z; break;
            default: is_prefix = false; break;
        }
        if (!is_prefix || fault_) break;
        c += 2;
    }
    if (rep_resumed_) c = 0;
    if (!fault_) c = execute(op, c);
    c += extra_cycles_;

    // Faults restart the instruction from its first prefix with the registers
    // it began with. Exceptions and the trap cost an INT n (iAPX 286 PRM).
    if (fault_) {
        uint8_t vec = fault_vec_;
        uint16_t err = fault_err_;
        fault_ = false;
        restore_regs();
        ip = instr_start_ip_;
        c += 23 + kQueueRefillTax + raise_event(vec, Kind::Exception, err);
        IBMPCAT_PERF_ADD(perf_tax_step_, kQueueRefillTax);
    } else if (trap && !vectored_ && !shadow_) {
        // A fault delivering the trap rolls back to the end of this instruction.
        instr_start_ip_ = ip;
        save_regs();
        c += 23 + kQueueRefillTax + raise_event(EXC_DB, Kind::Exception);
        IBMPCAT_PERF_ADD(perf_tax_step_, kQueueRefillTax);
    }
    c += bus_extra_;
#ifdef IBMPCAT_PERF
    // Class clocks exclude wait states, the queue tax and prefetch stalls.
    int stall = 0;
    if (bus_.mem_clocks) stall = prefetch(c);
    perf.stall += stall;
    c += stall;
    int tax = perf_tax_step_;
    perf.qtax += tax;
    perf_tax_step_ = 0;
    int base = c - bus_extra_ - stall - tax;
    if (base > 0) perf.cls[perf_class(op, last_reg_)] += uint64_t(base);
#else
    if (bus_.mem_clocks) c += prefetch(c);
#endif
    cycles += c;
    return c;
}

int Cpu::execute(uint8_t op, int c) {
    constexpr int CYC_REG = 2, CYC_MEM = 7, CYC_JMP_TAKEN = 7, CYC_JMP_NOT = 3;

    // Fast path for the ADD/OR/ADC/SBB/AND/SUB/XOR/CMP block 0x00-0x3D, six forms per group.
    // The (op & 7) > 5 slots are handled in the switch below.
    if (op < 0x40 && (op & 7) <= 5) {
        int alu = op >> 3;
        switch (op & 7) {
            case 0: { RM rm = decode_modrm(); uint8_t res = alu_apply8(alu, rm_read8(rm), get_reg8(last_reg_)); if (alu != 7) rm_write8(rm, res); c += rm.is_mem ? CYC_MEM : CYC_REG; break; }
            case 1: {
                RM rm = decode_modrm();
                if (opsize32_) { uint32_t res = alu_apply32(alu, rm_read32(rm), get_reg32(last_reg_)); if (alu != 7) rm_write32(rm, res); }
                else { uint16_t res = alu_apply16(alu, rm_read16(rm), get_reg16(last_reg_)); if (alu != 7) rm_write16(rm, res); }
                c += rm.is_mem ? CYC_MEM : CYC_REG;
                break;
            }
            case 2: { RM rm = decode_modrm(); uint8_t res = alu_apply8(alu, get_reg8(last_reg_), rm_read8(rm)); if (alu != 7) set_reg8(last_reg_, res); c += rm.is_mem ? (alu == 7 ? 6 : CYC_MEM) : CYC_REG; break; }
            case 3: {
                RM rm = decode_modrm();
                if (opsize32_) { uint32_t res = alu_apply32(alu, get_reg32(last_reg_), rm_read32(rm)); if (alu != 7) set_reg32(last_reg_, res); }
                else { uint16_t res = alu_apply16(alu, get_reg16(last_reg_), rm_read16(rm)); if (alu != 7) set_reg16(last_reg_, res); }
                c += rm.is_mem ? (alu == 7 ? 6 : CYC_MEM) : CYC_REG;  // CMP reg,mem is 6
                break;
            }
            case 4: { uint8_t imm = fetch8(); uint8_t res = alu_apply8(alu, get_reg8(0), imm); if (alu != 7) set_reg8(0, res); c += 3; break; }
            default: {
                if (opsize32_) { uint32_t imm = fetch32(); uint32_t res = alu_apply32(alu, get_reg32(0), imm); if (alu != 7) set_reg32(0, res); }
                else { uint16_t imm = fetch16(); uint16_t res = alu_apply16(alu, get_reg16(0), imm); if (alu != 7) set_reg16(0, res); }
                c += 3;  // ALU acc,imm
                break;
            }
        }
        return c;
    }

    switch (op) {
        case 0x06: push16(es); c += 3; break;
        case 0x07: load_seg(SEG_ES, pop16()); c += pe() ? 20 : 5; break;
        case 0x0E: push16(cs); c += 3; break;
        case 0x0F: {
            uint8_t op2 = fetch8();
            if (pe() && (op2 == 0x00 || op2 == 0x02 || op2 == 0x03)) {
                if (op2 == 0x00) c += system_0f00();
                else c += lar_lsl(op2 == 0x03);
            } else if (op2 == 0x01) {
                c += system_0f01();
            } else if (op2 == 0x06) {
                if (cpl0()) msw_ = uint16_t(msw_ & ~MSW_TS);  // CLTS
                c += 2;
            } else if (op2 == 0x05) {
                // LOADALL: reported no-op until a primary-source table layout (C14).
                if (on_unimplemented) on_unimplemented(cs, instr_start_ip_, 0x0F05);
                c += 3;
            } else if (!firmware()) {
                ud();  // includes SLDT..VERW, LAR and LSL, which are #UD in real mode
            } else if (op2 == 0x20 || op2 == 0x22) {
                // MOV r32,CR0 / MOV CR0,r32, 386 additions. BIOS-bochs-legacy's INT 15h
                // AH=87h enters and leaves protected mode with them; a 286 can't clear PE.
                uint8_t m = fetch8();
                if ((m >> 3) & 7) { if (on_unimplemented) on_unimplemented(cs, instr_start_ip_, uint16_t(0x0F00 | op2)); }
                else if (op2 == 0x20) set_reg32(m & 7, msw_);
                else msw_ = uint16_t(0xFFF0 | (get_reg32(m & 7) & 0x000F));
                c += 3;
            } else if (op2 >= 0x80 && op2 <= 0x8F) {
                // Jcc rel16 is an 80386 addition (the 286 has only rel8). BIOS-bochs-legacy needs it.
                int16_t rel = int16_t(fetch16());
                if (cond(op2 & 0xF)) { ip = uint16_t(ip + rel); transfer_ = true; }
                c += 3;
            } else if (op2 >= 0x90 && op2 <= 0x9F) {
                // SETcc r/m8, 386 addition.
                RM rm2 = decode_modrm();
                rm_write8(rm2, cond(op2 & 0xF) ? 1 : 0);
                c += 3;
            } else if (op2 == 0xAF) {
                // two-operand IMUL, 386 addition (the 286 has only the 0x69/0x6B imm forms)
                RM rm2 = decode_modrm();
                if (opsize32_) {
                    int64_t a = int32_t(get_reg32(last_reg_)), b = int32_t(rm_read32(rm2));
                    int64_t r = a * b;
                    bool of = (int64_t(int32_t(uint32_t(r))) != r);
                    set_flag(FLAG_CF, of); set_flag(FLAG_OF, of);
                    set_reg32(last_reg_, uint32_t(r));
                } else {
                    int32_t a = int16_t(get_reg16(last_reg_)), b = int16_t(rm_read16(rm2));
                    int32_t r = a * b;
                    bool of = (int32_t(int16_t(uint16_t(r))) != r);
                    set_flag(FLAG_CF, of); set_flag(FLAG_OF, of);
                    set_reg16(last_reg_, uint16_t(r));
                }
                c += 3;
            } else if (op2 == 0xB6 || op2 == 0xB7) {
                // MOVZX r16/32, r/m8 (0xB6) or r/m16 (0xB7), 386 addition.
                RM rm2 = decode_modrm();
                if (op2 == 0xB6) { uint8_t v = rm_read8(rm2); if (opsize32_) set_reg32(last_reg_, v); else set_reg16(last_reg_, v); }
                else { uint16_t v = rm_read16(rm2); if (opsize32_) set_reg32(last_reg_, v); else set_reg16(last_reg_, v); }
                c += 3;
            } else if (op2 == 0xBE || op2 == 0xBF) {
                // MOVSX r16/32, r/m8 (0xBE) or r/m16 (0xBF), 386 addition.
                RM rm2 = decode_modrm();
                if (op2 == 0xBE) { int8_t v = int8_t(rm_read8(rm2)); if (opsize32_) set_reg32(last_reg_, uint32_t(int32_t(v))); else set_reg16(last_reg_, uint16_t(int16_t(v))); }
                else { int16_t v = int16_t(rm_read16(rm2)); if (opsize32_) set_reg32(last_reg_, uint32_t(int32_t(v))); else set_reg16(last_reg_, uint16_t(v)); }
                c += 3;
            } else {
                if (on_unimplemented) on_unimplemented(cs, instr_start_ip_, uint16_t(0x0F00 | op2));
                c += 3;  // other protected-mode 0x0F opcodes are unimplemented
            }
            break;
        }
        case 0x16: push16(ss); c += 3; break;
        case 0x17: load_seg(SEG_SS, pop16()); shadow_ = true; c += pe() ? 20 : 5; break;
        case 0x1E: push16(ds); c += 3; break;
        case 0x1F: load_seg(SEG_DS, pop16()); c += pe() ? 20 : 5; break;
        case 0x27: daa(); c += 3; break;
        case 0x2F: das(); c += 3; break;
        case 0x37: aaa(); c += 3; break;
        case 0x3F: aas(); c += 3; break;
        default:
            if (op <= 0x47) {
                int r = op - 0x40; bool cf = flag(FLAG_CF);
                if (opsize32_) set_reg32(r, add32(get_reg32(r), 1, false));
                else set_reg16(r, add16(get_reg16(r), 1, false));
                set_flag(FLAG_CF, cf); c += CYC_REG;
            }
            else if (op <= 0x4F) {
                int r = op - 0x48; bool cf = flag(FLAG_CF);
                if (opsize32_) set_reg32(r, sub32(get_reg32(r), 1, false));
                else set_reg16(r, sub16(get_reg16(r), 1, false));
                set_flag(FLAG_CF, cf); c += CYC_REG;
            }
            else if (op <= 0x57) { push_reg(op - 0x50); c += 3; }  // PUSH reg16 costs 3 (iAPX 286 PRM timing appendix)
            else if (op <= 0x5F) {
                int r = op - 0x58;
                if (opsize32_) set_reg32(r, pop32()); else set_reg16(r, pop16());
                c += 5;
            }
            else if (op == 0x60) { pusha(); c += 17; }  // PUSHA
            else if (op == 0x61) { popa(); c += 19; }  // POPA
            else if (op == 0x62) { bound(); c += 13; }
            else if (op == 0x63 && pe()) {  // ARPL, protected mode only
                RM rm = decode_modrm();
                uint16_t dst = rm_read16(rm), src = get_reg16(last_reg_);
                bool adjust = (dst & 3) < (src & 3);
                if (adjust) rm_write16(rm, uint16_t((dst & 0xFFFC) | (src & 3)));
                set_flag(FLAG_ZF, adjust);
                c += rm.is_mem ? 11 : 10;
            }
            else if (op == 0x68) { if (opsize32_) push32(fetch32()); else push16(fetch16()); c += 3; }
            else if (op == 0x69) {
                RM rm = decode_modrm(); int r = last_reg_;
                if (opsize32_) imul_imm32(r, rm, fetch32()); else imul_imm16(r, rm, fetch16());
                c += rm.is_mem ? 24 : 21;
            }
            else if (op == 0x6A) { int8_t imm = int8_t(fetch8()); if (opsize32_) push32(uint32_t(int32_t(imm))); else push16(uint16_t(int16_t(imm))); c += 3; }
            else if (op == 0x6B) {
                RM rm = decode_modrm(); int r = last_reg_; int8_t imm = int8_t(fetch8());
                if (opsize32_) imul_imm32(r, rm, uint32_t(int32_t(imm))); else imul_imm16(r, rm, uint16_t(int16_t(imm)));
                c += rm.is_mem ? 24 : 21;
            }
            else if (op >= 0x6C && op <= 0x6F) { if (io_ok()) c += io_string_op(op); else c += 5; }
            else if (op >= 0x70 && op <= 0x7F) { bool taken = cond(op & 0xF); jcc(taken); c += taken ? (CYC_JMP_TAKEN + flush()) : CYC_JMP_NOT; }  // taken: floor 7 plus queue-refill tax
            else if (op == 0x80 || op == 0x81 || op == 0x82 || op == 0x83) { c += grp1_immed(op); }
            else if (op == 0x84) { RM rm = decode_modrm(); and8(rm_read8(rm), get_reg8(last_reg_)); c += rm.is_mem ? 6 : CYC_REG; }
            else if (op == 0x85) { RM rm = decode_modrm(); and16(rm_read16(rm), get_reg16(last_reg_)); c += rm.is_mem ? 6 : CYC_REG; }
            else if (op == 0x86) { RM rm = decode_modrm(); uint8_t a = get_reg8(last_reg_), b = rm_read8(rm); set_reg8(last_reg_, b); rm_write8(rm, a); c += rm.is_mem ? 5 : 3; }
            else if (op == 0x87) { RM rm = decode_modrm(); uint16_t a = get_reg16(last_reg_), b = rm_read16(rm); set_reg16(last_reg_, b); rm_write16(rm, a); c += rm.is_mem ? 5 : 3; }
            // MOV memory cost is directional: write 3, read 5 (iAPX 286 PRM timing appendix).
            else if (op == 0x88) { RM rm = decode_modrm(); rm_write8(rm, get_reg8(last_reg_)); c += rm.is_mem ? 3 : CYC_REG; }
            else if (op == 0x89) { RM rm = decode_modrm(); if (opsize32_) rm_write32(rm, get_reg32(last_reg_)); else rm_write16(rm, get_reg16(last_reg_)); c += rm.is_mem ? 3 : CYC_REG; }
            else if (op == 0x8A) { RM rm = decode_modrm(); set_reg8(last_reg_, rm_read8(rm)); c += rm.is_mem ? 5 : CYC_REG; }
            else if (op == 0x8B) { RM rm = decode_modrm(); if (opsize32_) set_reg32(last_reg_, rm_read32(rm)); else set_reg16(last_reg_, rm_read16(rm)); c += rm.is_mem ? 5 : CYC_REG; }
            // MOV reg16,segreg and segreg,reg16 cost 2 register-to-register.
            else if (op == 0x8C) { RM rm = decode_modrm(); rm_write16(rm, seg_reg(last_reg_ & 3)); c += rm.is_mem ? 3 : CYC_REG; }
            else if (op == 0x8D) { RM rm = decode_modrm(); if (!rm.is_mem) ud(); else if (opsize32_) set_reg32(last_reg_, rm.off); else set_reg16(last_reg_, rm.off); c += 3; }  // LEA
            else if (op == 0x8E) {
                RM rm = decode_modrm();
                int sr = last_reg_ & 3;
                if (sr == SEG_CS) ud();  // MOV CS is #UD from the 286 on
                else { load_seg(sr, rm_read16(rm)); if (sr == SEG_SS) shadow_ = true; }
                c += pe() ? (rm.is_mem ? 19 : 17) : (rm.is_mem ? 5 : CYC_REG);
            }
            else if (op == 0x8F) { RM rm = decode_modrm(); if (opsize32_) rm_write32(rm, pop32()); else rm_write16(rm, pop16()); c += 5; }
            else if (op == 0x90) { c += 3; }
            else if (op >= 0x91 && op <= 0x97) {
                int r = op - 0x90;
                if (opsize32_) { uint32_t t = ax; ax = get_reg32(r); set_reg32(r, t); }
                else { uint16_t t = uint16_t(ax); ax = (ax & 0xFFFF0000u) | get_reg16(r); set_reg16(r, t); }
                c += 3;
            }
            else if (op == 0x98) { if (opsize32_) ax = uint32_t(int32_t(int16_t(ax))); else ax = (ax & 0xFFFF0000u) | uint16_t(int16_t(int8_t(ax & 0xFF))); c += CYC_REG; }  // CBW / CWDE
            else if (op == 0x99) { if (opsize32_) dx = (ax & 0x80000000u) ? 0xFFFFFFFFu : 0u; else dx = (dx & 0xFFFF0000u) | ((ax & 0x8000) ? 0xFFFFu : 0u); c += CYC_REG; }  // CWD / CDQ
            else if (op == 0x9A) { uint16_t off = fetch16(); uint16_t seg = fetch16(); c += 13 + far_call(seg, off) + flush(); }  // CALL far: floor 13 plus queue-refill tax
            else if (op == 0x9B) { if ((msw_ & MSW_MP) && (msw_ & MSW_TS)) raise(EXC_NM); c += 3; }  // WAIT: no coprocessor present
            else if (op == 0x9C) { push16(flags); c += 3; }
            // FLAGS bits 12-15 are always 0 on a 286 in real mode (Intel AP-485 CPU identification).
            else if (op == 0x9D) { uint16_t v = pop16(); if (pe()) set_flags_pm(v); else flags = uint16_t((v & 0x0FD5) | FLAG_R1); c += 5; }
            else if (op == 0x9E) { uint8_t ah = get_reg8(4); flags = uint16_t((flags & 0xFF00) | (ah & 0xD5) | FLAG_R1); c += CYC_REG; }  // SAHF
            else if (op == 0x9F) { set_reg8(4, uint8_t(flags & 0xFF)); c += CYC_REG; }  // LAHF
            else if (op >= 0xA0 && op <= 0xA3) {
                int seg = (seg_override_ >= 0) ? seg_override_ : SEG_DS;
                uint16_t off = fetch16();
                bool is_load = (op == 0xA0 || op == 0xA1);
                if (op == 0xA0) set_reg8(0, rb(seg, off));
                else if (op == 0xA1) { if (opsize32_) ax = rd(seg, off); else ax = (ax & 0xFFFF0000u) | rw(seg, off); }
                else if (op == 0xA2) wb(seg, off, get_reg8(0));
                else { if (opsize32_) wd(seg, off, ax); else ww(seg, off, uint16_t(ax)); }
                c += is_load ? 5 : 3;
            }
            else if (op >= 0xA4 && op <= 0xA7) { c += string_op(op); }
            else if (op == 0xA8) { uint8_t imm = fetch8(); and8(get_reg8(0), imm); c += 3; }
            else if (op == 0xA9) { if (opsize32_) and32(ax, fetch32()); else and16(uint16_t(ax), fetch16()); c += 3; }
            else if (op >= 0xAA && op <= 0xAF) { c += string_op(op); }
            else if (op >= 0xB0 && op <= 0xB7) { set_reg8(op - 0xB0, fetch8()); c += CYC_REG; }
            else if (op >= 0xB8 && op <= 0xBF) { if (opsize32_) set_reg32(op - 0xB8, fetch32()); else set_reg16(op - 0xB8, fetch16()); c += CYC_REG; }
            else if (op == 0xC0 || op == 0xC1) { c += grp2_shift(op); }
            else if (op == 0xC2) { uint16_t n = fetch16(); ip = pop16(); sp = uint16_t(sp + n); c += 11 + flush(); }  // RET imm16: floor 11 plus queue-refill tax
            else if (op == 0xC3) { ip = pop16(); c += 11 + flush(); }  // RET: floor 11 plus queue-refill tax
            else if (op == 0xC4) { RM rm = decode_modrm(); int r = last_reg_; uint16_t off, seg; if (far_operand(rm, off, seg)) { load_seg(SEG_ES, seg); if (!fault_) set_reg16(r, off); } c += pe() ? 21 : CYC_MEM; }
            else if (op == 0xC5) { RM rm = decode_modrm(); int r = last_reg_; uint16_t off, seg; if (far_operand(rm, off, seg)) { load_seg(SEG_DS, seg); if (!fault_) set_reg16(r, off); } c += pe() ? 21 : CYC_MEM; }
            else if (op == 0xC6) { RM rm = decode_modrm(); uint8_t imm = fetch8(); rm_write8(rm, imm); c += rm.is_mem ? 3 : CYC_REG; }
            else if (op == 0xC7) { RM rm = decode_modrm(); if (opsize32_) rm_write32(rm, fetch32()); else rm_write16(rm, fetch16()); c += rm.is_mem ? 3 : CYC_REG; }
            // ENTER: 11 (level 0), 15 (level 1), 12+4*(lex-1) (level>1), iAPX 286 PRM timing appendix.
            else if (op == 0xC8) { int lex = enter(); c += (lex == 0) ? 11 : (lex == 1) ? 15 : (12 + 4 * (lex - 1)); }
            else if (op == 0xC9) { leave(); c += 5; }
            else if (op == 0xCA) { uint16_t n = fetch16(); c += 15 + far_ret(n) + flush(); }  // RETF imm16: floor 15 plus queue-refill tax
            else if (op == 0xCB) { c += 15 + far_ret(0) + flush(); }  // RETF: floor 15 plus queue-refill tax
            // INT n/INT3 floor 23, INTO 24, IRET 17 (iAPX 286 PRM timing appendix), plus kQueueRefillTax.
            else if (op == 0xCC) { c += 23 + deliver(3, Kind::Soft) + flush(); }
            else if (op == 0xCD) { uint8_t n = fetch8(); c += 23 + deliver(n, Kind::Soft) + flush(); }
            else if (op == 0xCE) { if (flag(FLAG_OF)) { c += 24 + deliver(4, Kind::Soft) + flush(); } else c += 3; }
            else if (op == 0xCF) { c += 17 + iret() + flush(); }
            else if (op >= 0xD0 && op <= 0xD3) { c += grp2_shift(op); }
            else if (op == 0xD4) { aam(); c += 16; }
            else if (op == 0xD5) { aad(); c += 14; }
            else if (op == 0xD6) { set_reg8(0, flag(FLAG_CF) ? 0xFF : 0x00); c += CYC_REG; }  // SALC, undocumented (Ralf Brown's OPCODES.LST)
            else if (op == 0xD7) { int seg = (seg_override_ >= 0) ? seg_override_ : SEG_DS; set_reg8(0, rb(seg, uint16_t(bx + get_reg8(0)))); c += 5; }
            // x87 escape, no 80287 fitted: #NM if EM or TS is set (iAPX 286 PRM, exception 7).
            else if (op >= 0xD8 && op <= 0xDF) { decode_modrm(); if (msw_ & (MSW_EM | MSW_TS)) raise(EXC_NM); c += 3; }
            else if (op == 0xE0 || op == 0xE1 || op == 0xE2 || op == 0xE3) { c += loop_group(op); }
            // IN=5, OUT=3 (iAPX 286 PRM timing appendix).
            else if (op == 0xE4) { uint8_t p = fetch8(); if (io_ok()) set_reg8(0, in8(p)); c += 5; }
            else if (op == 0xE5) { uint8_t p = fetch8(); if (io_ok()) ax = (ax & 0xFFFF0000u) | in16(p); c += 5; }
            else if (op == 0xE6) { uint8_t p = fetch8(); if (io_ok()) out8(p, get_reg8(0)); c += 3; }
            else if (op == 0xE7) { uint8_t p = fetch8(); if (io_ok()) out16(p, uint16_t(ax)); c += 3; }
            // CALL/JMP near floor 7, JMP far floor 11 (iAPX 286 PRM timing appendix), plus queue-refill tax.
            else if (op == 0xE8) { int16_t rel = int16_t(fetch16()); push16(ip); ip = uint16_t(ip + rel); c += 7 + flush(); }
            else if (op == 0xE9) { int16_t rel = int16_t(fetch16()); ip = uint16_t(ip + rel); c += 7 + flush(); }
            else if (op == 0xEA) { uint16_t off = fetch16(); uint16_t seg = fetch16(); c += 11 + far_jmp(seg, off) + flush(); }
            else if (op == 0xEB) { int8_t rel = int8_t(fetch8()); ip = uint16_t(ip + rel); c += 7 + flush(); }
            else if (op == 0xEC) { if (io_ok()) set_reg8(0, in8(uint16_t(dx))); c += 5; }
            else if (op == 0xED) { if (io_ok()) ax = (ax & 0xFFFF0000u) | in16(uint16_t(dx)); c += 5; }
            else if (op == 0xEE) { if (io_ok()) out8(uint16_t(dx), get_reg8(0)); c += 3; }
            else if (op == 0xEF) { if (io_ok()) out16(uint16_t(dx), uint16_t(ax)); c += 3; }
            else if (op == 0xF4) { if (cpl0()) halted = true; c += 2; }
            else if (op == 0xF5) { set_flag(FLAG_CF, !flag(FLAG_CF)); c += CYC_REG; }
            else if (op == 0xF6 || op == 0xF7) { c += grp3_unary(op); }
            else if (op == 0xF8) { set_flag(FLAG_CF, false); c += CYC_REG; }
            else if (op == 0xF9) { set_flag(FLAG_CF, true); c += CYC_REG; }
            else if (op == 0xFA) { if (io_ok()) set_flag(FLAG_IF, false); c += CYC_REG; }
            else if (op == 0xFB) { if (io_ok()) { if (!flag(FLAG_IF)) shadow_ = true; set_flag(FLAG_IF, true); } c += CYC_REG; }
            else if (op == 0xFC) { set_flag(FLAG_DF, false); c += CYC_REG; }
            else if (op == 0xFD) { set_flag(FLAG_DF, true); c += CYC_REG; }
            else if (op == 0xFE || op == 0xFF) { c += grp5(op); }
            else if (firmware()) {
                if (on_unimplemented) on_unimplemented(cs, instr_start_ip_, op);
                c += 2;
            }
            else ud();
            break;
    }

    return c;
}

}  // namespace cpu80286
