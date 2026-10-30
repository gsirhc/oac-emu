#include "cpu_mcs48.h"

namespace mcs48 {

// RESET forces PC=0 and the stack pointer to 0; ports default to the
// input-ready (all-1s) latch state (8048/8035 datasheet, RESET).
void Cpu::reset() {
    a = 0;
    for (auto &b : ram) b = 0;
    pc = 0;
    cy = ac = f0 = f1 = bs = false;
    sp = 0;
    t = 0;
    timer_run = counter_run = false;
    tf = false;
    int_enabled = tcnti_enabled = false;
    in_service = false;
    mb_ff = false;
    timer_presc_ = 0;
    t1_prev_ = true;
    cycles = 0;
    if (bus_.write_p1) bus_.write_p1(0xFF);
    if (bus_.write_p2) bus_.write_p2(0xFF);
}

uint8_t Cpu::fetch8() {
    uint8_t v = bus_.read_program ? bus_.read_program(pc) : 0;
    pc = (pc + 1) & 0xFFF;
    return v;
}

uint8_t Cpu::reg(int r) const {
    return ram[(bs ? 24 : 0) + r];
}

void Cpu::set_reg(int r, uint8_t v) {
    ram[(bs ? 24 : 0) + r] = v;
}

uint8_t Cpu::rd_ind(int r) const {
    return ram[reg(r) & 0x3F];
}

void Cpu::wr_ind(int r, uint8_t v) {
    ram[reg(r) & 0x3F] = v;
}

uint8_t Cpu::psw() const {
    return uint8_t((cy << 7) | (ac << 6) | (f0 << 5) | (bs << 4) | (sp & 7));
}

void Cpu::set_psw(uint8_t v) {
    sp = v & 7;
    bs = (v >> 4) & 1;
    f0 = (v >> 5) & 1;
    ac = (v >> 6) & 1;
    cy = (v >> 7) & 1;
}

// Stack lives in internal RAM 8-23, 8 levels of 2 bytes each (CALL/RETR,
// "(SP) <- (PC), (PSW 4-7)"). Byte 1's low nibble is PC 8-11, high nibble
// PSW 4-7 (CY,AC,F0,BS); RET leaves that nibble unread.
void Cpu::push_pc_psw() {
    uint8_t addr = uint8_t(8 + 2 * sp);
    ram[addr] = uint8_t(pc & 0xFF);
    ram[addr + 1] = uint8_t((cy << 7) | (ac << 6) | (f0 << 5) | (bs << 4) | ((pc >> 8) & 0xF));
    sp = (sp + 1) & 7;
}

void Cpu::pop_pc(bool restore_psw) {
    sp = (sp - 1) & 7;
    uint8_t addr = uint8_t(8 + 2 * sp);
    uint8_t lo = ram[addr];
    uint8_t hi = ram[addr + 1];
    pc = uint16_t(((hi & 0xF) << 8) | lo);
    if (restore_psw) {
        cy = (hi >> 7) & 1;
        ac = (hi >> 6) & 1;
        f0 = (hi >> 5) & 1;
        bs = (hi >> 4) & 1;
    }
}

void Cpu::add(uint8_t v, bool carry_in) {
    uint16_t cin = carry_in ? 1 : 0;
    uint16_t r = uint16_t(a) + v + cin;
    cy = r > 0xFF;
    ac = ((a & 0xF) + (v & 0xF) + cin) > 0xF;
    a = uint8_t(r);
}

// MCS-48 DA A only ever adds 6; there is no subtract-mode nibble (isa.txt).
void Cpu::daa() {
    if ((a & 0xF) > 9 || ac) a = uint8_t(a + 0x06);
    if ((a >> 4) > 9 || cy) {
        a = uint8_t(a + 0x60);
        cy = true;
    }
}

// STRT T ticks the counter register once per 32 machine cycles; STRT CNT
// counts T1 high-to-low edges instead (isa.txt STRT T / STRT CNT). The
// `_was_running` flags are sampled before decode(), so STRT T/CNT's own
// cycle doesn't count ("this instruction resets the prescaler to 0").
void Cpu::tick_timer(int mcycles, bool timer_was_running, bool counter_was_running) {
    if (timer_was_running) {
        timer_presc_ += mcycles;
        while (timer_presc_ >= 32) {
            timer_presc_ -= 32;
            t++;
            if (t == 0) tf = true;
        }
    }
    if (counter_was_running && bus_.read_t1) {
        bool cur = bus_.read_t1();
        if (t1_prev_ && !cur) {
            t++;
            if (t == 0) tf = true;
        }
        t1_prev_ = cur;
    }
}

// External interrupt takes priority over the timer; both vector like a
// hidden CALL to a fixed address with PC bit 11 held at 0, and neither can
// retrigger until RETR (isa.txt "Interrupts always vector ... to location
// 3 or 7 ... RETR ... enables a new interrupt request").
int Cpu::service_interrupt() {
    if (in_service) return 0;
    if (int_enabled && bus_.int_asserted && bus_.int_asserted()) {
        push_pc_psw();
        pc = 3;
        in_service = true;
        return 2;
    }
    if (tcnti_enabled && tf) {
        tf = false;
        push_pc_psw();
        pc = 7;
        in_service = true;
        return 2;
    }
    return 0;
}

int Cpu::step() {
    int taken = service_interrupt();
    if (taken) {
        cycles += taken;
        tick_timer(taken, timer_run, counter_run);
        return taken;
    }

    bool timer_was_running = timer_run;
    bool counter_was_running = counter_run;
    uint8_t op = fetch8();
    int c = decode(op);
    cycles += c;
    tick_timer(c, timer_was_running, counter_was_running);
    if (bus_.tick) bus_.tick(c);
    return c;
}

int Cpu::decode(uint8_t op) {
    // --- register/indirect-register fields --------------------------------
    if ((op & 0xF8) == 0x68) { add(reg(op & 7), false); return 1; }           // ADD A,Rr
    if ((op & 0xFE) == 0x60) { add(rd_ind(op & 1), false); return 1; }        // ADD A,@Rr
    if ((op & 0xF8) == 0x78) { add(reg(op & 7), cy); return 1; }              // ADDC A,Rr
    if ((op & 0xFE) == 0x70) { add(rd_ind(op & 1), cy); return 1; }           // ADDC A,@Rr
    if ((op & 0xF8) == 0x58) { a &= reg(op & 7); return 1; }                  // ANL A,Rr
    if ((op & 0xFE) == 0x50) { a &= rd_ind(op & 1); return 1; }               // ANL A,@Rr
    if ((op & 0xF8) == 0x48) { a |= reg(op & 7); return 1; }                  // ORL A,Rr
    if ((op & 0xFE) == 0x40) { a |= rd_ind(op & 1); return 1; }               // ORL A,@Rr
    if ((op & 0xF8) == 0xD8) { a ^= reg(op & 7); return 1; }                  // XRL A,Rr
    if ((op & 0xFE) == 0xD0) { a ^= rd_ind(op & 1); return 1; }               // XRL A,@Rr
    if ((op & 0xF8) == 0x28) { uint8_t t2 = a; a = reg(op & 7); set_reg(op & 7, t2); return 1; } // XCH A,Rr
    if ((op & 0xFE) == 0x20) { int r = op & 1; uint8_t t2 = a; a = rd_ind(r); wr_ind(r, t2); return 1; } // XCH A,@Rr
    if ((op & 0xFE) == 0x30) {                                                 // XCHD A,@Rr
        int r = op & 1;
        uint8_t addr = reg(r) & 0x3F;
        uint8_t m = ram[addr];
        uint8_t lo = a & 0xF;
        a = (a & 0xF0) | (m & 0xF);
        ram[addr] = (m & 0xF0) | lo;
        return 1;
    }
    if ((op & 0xF8) == 0xF8) { a = reg(op & 7); return 1; }                  // MOV A,Rr
    if ((op & 0xFE) == 0xF0) { a = rd_ind(op & 1); return 1; }                // MOV A,@Rr
    if ((op & 0xF8) == 0xA8) { set_reg(op & 7, a); return 1; }                // MOV Rr,A
    if ((op & 0xFE) == 0xA0) { wr_ind(op & 1, a); return 1; }                 // MOV @Rr,A
    if ((op & 0xF8) == 0xB8) { set_reg(op & 7, fetch8()); return 2; }         // MOV Rr,data
    if ((op & 0xFE) == 0xB0) { wr_ind(op & 1, fetch8()); return 2; }          // MOV @Rr,data
    if ((op & 0xF8) == 0xC8) { set_reg(op & 7, uint8_t(reg(op & 7) - 1)); return 1; } // DEC Rr
    if ((op & 0xF8) == 0x18) { set_reg(op & 7, uint8_t(reg(op & 7) + 1)); return 1; } // INC Rr
    if ((op & 0xFE) == 0x10) { int r = op & 1; wr_ind(r, uint8_t(rd_ind(r) + 1)); return 1; } // INC @Rr
    if ((op & 0xF8) == 0xE8) {                                                 // DJNZ Rr,addr
        int r = op & 7;
        uint8_t v = uint8_t(reg(r) - 1);
        set_reg(r, v);
        uint8_t addr = fetch8();
        if (v != 0) pc = uint16_t((pc & 0xF00) | addr);
        return 2;
    }
    if ((op & 0xFE) == 0x80) { a = bus_.read_data_ext ? bus_.read_data_ext(reg(op & 1)) : 0; return 2; } // MOVX A,@Rr
    if ((op & 0xFE) == 0x90) { if (bus_.write_data_ext) bus_.write_data_ext(reg(op & 1), a); return 2; } // MOVX @Rr,A

    // --- short (within-page) conditional jumps, bits 5-7 pick the field ---
    if ((op & 0x1F) == 0x12) {                                                // JBb addr
        int b = (op >> 5) & 7;
        uint8_t addr = fetch8();
        if ((a >> b) & 1) pc = uint16_t((pc & 0xF00) | addr);
        return 2;
    }
    if ((op & 0x1F) == 0x04) {                                                // JMP addr
        int page = (op >> 5) & 7;
        uint8_t lo = fetch8();
        pc = uint16_t((mb_ff ? 0x800 : 0) | (page << 8) | lo);
        return 2;
    }
    if ((op & 0x1F) == 0x14) {                                                // CALL addr
        int page = (op >> 5) & 7;
        uint8_t lo = fetch8();
        push_pc_psw();
        pc = uint16_t((mb_ff ? 0x800 : 0) | (page << 8) | lo);
        return 2;
    }

    // --- port field (p=0 BUS/INS, p=1 P1, p=2 P2) --------------------------
    if ((op & 0xFC) == 0x08) {                                                // IN A,Pp / INS A,BUS
        int p = op & 3;
        if (p == 1) a = bus_.read_p1 ? bus_.read_p1() : 0xFF;
        else if (p == 2) a = bus_.read_p2 ? bus_.read_p2() : 0xFF;
        else if (p == 0) a = bus_.read_bus ? bus_.read_bus() : 0xFF;          // BUS floats high, unused on 8035
        return 2;
    }
    if ((op & 0xFC) == 0x38) {                                                // OUTL Pp,A
        int p = op & 3;
        if (p == 1 && bus_.write_p1) bus_.write_p1(a);
        else if (p == 2 && bus_.write_p2) bus_.write_p2(a);
        return 2;
    }
    if ((op & 0xFC) == 0x98) {                                                // ANL Pp,data / ANL BUS,data
        int p = op & 3;
        uint8_t data = fetch8();
        if (p == 1 && bus_.write_p1) bus_.write_p1((bus_.read_p1 ? bus_.read_p1() : 0) & data);
        else if (p == 2 && bus_.write_p2) bus_.write_p2((bus_.read_p2 ? bus_.read_p2() : 0) & data);
        else if (p == 0 && bus_.write_bus) bus_.write_bus((bus_.read_bus ? bus_.read_bus() : 0xFF) & data); // not for 8035
        return 2;
    }
    if ((op & 0xFC) == 0x88) {                                                // ORL Pp,data / ORL BUS,data
        int p = op & 3;
        uint8_t data = fetch8();
        if (p == 1 && bus_.write_p1) bus_.write_p1((bus_.read_p1 ? bus_.read_p1() : 0) | data);
        else if (p == 2 && bus_.write_p2) bus_.write_p2((bus_.read_p2 ? bus_.read_p2() : 0) | data);
        else if (p == 0 && bus_.write_bus) bus_.write_bus((bus_.read_bus ? bus_.read_bus() : 0xFF) | data); // not for 8035
        return 2;
    }

    // --- 8243 expander ports 4-7 --------------------------------------------
    if ((op & 0xFC) == 0x0C) { int p = (op & 3) + 4; a = (bus_.read_pexp ? bus_.read_pexp(p) : 0) & 0xF; return 2; } // MOVD A,Pp
    if ((op & 0xFC) == 0x3C) { if (bus_.write_pexp) bus_.write_pexp((op & 3) + 4, a & 0xF); return 2; }              // MOVD Pp,A
    if ((op & 0xFC) == 0x9C) {                                                // ANLD Pp,A
        int p = (op & 3) + 4;
        uint8_t v = (bus_.read_pexp ? bus_.read_pexp(p) : 0) & (a & 0xF);
        if (bus_.write_pexp) bus_.write_pexp(p, v & 0xF);
        return 2;
    }
    if ((op & 0xFC) == 0x8C) {                                                // ORLD Pp,A
        int p = (op & 3) + 4;
        uint8_t v = (bus_.read_pexp ? bus_.read_pexp(p) : 0) | (a & 0xF);
        if (bus_.write_pexp) bus_.write_pexp(p, v & 0xF);
        return 2;
    }

    switch (op) {
        case 0x00: break;                                                    // NOP

        case 0x02: if (bus_.write_bus) bus_.write_bus(a); return 2;          // OUTL BUS,A (not for 8035)
        case 0x03: add(fetch8(), false); return 2;                           // ADD A,data
        case 0x05: int_enabled = true; return 1;                             // EN I
        case 0x07: a = uint8_t(a - 1); return 1;                             // DEC A
        case 0x13: add(fetch8(), cy); return 2;                              // ADDC A,data
        case 0x15: int_enabled = false; return 1;                            // DIS I
        case 0x16: {                                                         // JTF addr
            uint8_t addr = fetch8();
            bool was_set = tf;
            tf = false;
            if (was_set) pc = uint16_t((pc & 0xF00) | addr);
            return 2;
        }
        case 0x17: a = uint8_t(a + 1); return 1;                             // INC A

        case 0x23: a = fetch8(); return 2;                                   // MOV A,data
        case 0x25: tcnti_enabled = true; return 1;                           // EN TCNTI
        case 0x26: { uint8_t addr = fetch8(); if (bus_.read_t0 && !bus_.read_t0()) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JNT0
        case 0x27: a = 0; return 1;                                          // CLR A
        case 0x35: tcnti_enabled = false; return 1;                          // DIS TCNTI
        case 0x36: { uint8_t addr = fetch8(); if (bus_.read_t0 && bus_.read_t0()) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JT0
        case 0x37: a = uint8_t(~a); return 1;                                // CPL A

        case 0x42: a = t; return 1;                                         // MOV A,T
        case 0x43: a |= fetch8(); return 2;                                 // ORL A,data
        case 0x45: counter_run = true; timer_run = false; return 1;         // STRT CNT
        case 0x46: { uint8_t addr = fetch8(); if (bus_.read_t1 && !bus_.read_t1()) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JNT1
        case 0x47: a = uint8_t((a << 4) | (a >> 4)); return 1;              // SWAP A

        case 0x53: a &= fetch8(); return 2;                                 // ANL A,data
        case 0x55: timer_run = true; counter_run = false; timer_presc_ = 0; return 1; // STRT T
        case 0x56: { uint8_t addr = fetch8(); if (bus_.read_t1 && bus_.read_t1()) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JT1
        case 0x57: daa(); return 1;                                         // DA A

        case 0x62: t = a; return 1;                                         // MOV T,A
        case 0x65: timer_run = counter_run = false; return 1;               // STOP TCNT
        case 0x67: { uint8_t lo = a & 1; a = uint8_t((a >> 1) | (cy ? 0x80 : 0)); cy = lo; return 1; } // RRC A

        case 0x75: return 1;                                               // ENT0 CLK (clock-out pin, no bus hook)
        case 0x76: { uint8_t addr = fetch8(); if (f1) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JF1
        case 0x77: { uint8_t hi = a & 1; a = uint8_t((a >> 1) | (hi << 7)); return 1; } // RR A

        case 0x83: pop_pc(false); return 2;                                 // RET
        case 0x85: f0 = false; return 1;                                    // CLR F0
        case 0x86: { uint8_t addr = fetch8(); if (bus_.int_asserted && bus_.int_asserted()) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JNI

        case 0x93: in_service = false; pop_pc(true); return 2;              // RETR
        case 0x95: f0 = !f0; return 1;                                      // CPL F0
        case 0x96: { uint8_t addr = fetch8(); if (a != 0) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JNZ
        case 0x97: cy = false; return 1;                                    // CLR C

        case 0xA3: { uint16_t addr = uint16_t((pc & 0xF00) | a); a = bus_.read_program ? bus_.read_program(addr) : 0; return 2; } // MOVP A,@A
        case 0xA5: f1 = false; return 1;                                    // CLR F1
        case 0xA7: cy = !cy; return 1;                                      // CPL C

        case 0xB3: {                                                       // JMPP @A
            uint16_t addr = uint16_t((pc & 0xF00) | a);
            uint8_t lo = bus_.read_program ? bus_.read_program(addr) : 0;
            pc = uint16_t((pc & 0xF00) | lo);
            return 2;
        }
        case 0xB5: f1 = !f1; return 1;                                      // CPL F1
        case 0xB6: { uint8_t addr = fetch8(); if (f0) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JF0

        case 0xC5: bs = false; return 1;                                   // SEL RB0
        case 0xC6: { uint8_t addr = fetch8(); if (a == 0) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JZ
        case 0xC7: a = psw(); return 1;                                    // MOV A,PSW

        case 0xD3: a ^= fetch8(); return 2;                                 // XRL A,data
        case 0xD5: bs = true; return 1;                                    // SEL RB1
        case 0xD7: set_psw(a); return 1;                                   // MOV PSW,A

        case 0xE3: {                                                       // MOVP3 A,@A
            uint16_t addr = uint16_t(0x300 | a);
            a = bus_.read_program ? bus_.read_program(addr) : 0;
            return 2;
        }
        case 0xE5: mb_ff = false; return 1;                                // SEL MB0
        case 0xE6: { uint8_t addr = fetch8(); if (!cy) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JNC
        case 0xE7: { uint8_t hi = a >> 7; a = uint8_t((a << 1) | hi); return 1; } // RL A

        case 0xF5: mb_ff = true; return 1;                                 // SEL MB1
        case 0xF6: { uint8_t addr = fetch8(); if (cy) pc = uint16_t((pc & 0xF00) | addr); return 2; } // JC
        case 0xF7: { uint8_t hi = a >> 7; a = uint8_t((a << 1) | (cy ? 1 : 0)); cy = hi; return 1; } // RLC A

        default: break;                                                    // undefined opcode decodes as NOP
    }
    return 1;
}

} // namespace mcs48
