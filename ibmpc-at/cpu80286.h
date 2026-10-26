// Intel 80286 CPU core, real mode only, plus an 80386 compatibility layer:
// 32-bit registers via the 0x66 prefix, Jcc rel16, SETcc, two-operand IMUL,
// MOVZX/MOVSX. None of that exists on a real 286. BIOS-bochs-legacy assumes
// a 386+ baseline (IBM_PCAT_REVIEW.md §6), so the layer runs only for code
// fetched where firmware_at() says firmware lives; anywhere else it is #UD.
//
// Protected mode: 286 descriptors, gates, privilege and limit checks, 286
// TSS task switches. LMSW sets PE and can't clear it; only RESET, or the
// firmware layer's MOV CR0 below, does. LOADALL is not implemented.
//
// The core talks only through Bus. Addresses are 24 bits and never wrapped at
// 1MB here, because the A20 gate is motherboard logic (8042 P21) in the chipset.
//
// Timings and semantics: Intel iAPX 286 Programmer's Reference Manual (1987),
// 80286 data sheet timing appendix, and the 8086/8088 User's Manual.

#ifndef IBMPCAT_CPU80286_H
#define IBMPCAT_CPU80286_H

#include <algorithm>
#include <cstdint>
#include <functional>

// Performance panel counters, compiled out unless IBMPCAT_PERF.
#ifdef IBMPCAT_PERF
#define IBMPCAT_PERF_ADD(field, n) ((field) += (n))
#define IBMPCAT_NOTE_MEM(addr, clocks) note_mem_wait((addr), (clocks))
#define IBMPCAT_NOTE_IO(eight, clocks) note_io_wait((eight), (clocks))
#else
#define IBMPCAT_PERF_ADD(field, n) ((void)0)
#define IBMPCAT_NOTE_MEM(addr, clocks) ((void)0)
#define IBMPCAT_NOTE_IO(eight, clocks) ((void)0)
#endif

namespace cpu80286 {

#ifdef IBMPCAT_PERF
// Order is the Performance panel's class list.
enum PerfClass : int {
    kPerfAlu, kPerfMov, kPerfStack, kPerfFlow,
    kPerfString, kPerfIo, kPerfMisc, kPerfHlt, kPerfClassCount
};
#endif

// FLAGS bits.
enum Flag : uint16_t {
    FLAG_CF   = 1 << 0,   // carry
    FLAG_R1   = 1 << 1,   // reserved, always 1
    FLAG_PF   = 1 << 2,   // parity (low byte of result, even = 1)
    FLAG_AF   = 1 << 4,   // auxiliary carry (BCD)
    FLAG_ZF   = 1 << 6,   // zero
    FLAG_SF   = 1 << 7,   // sign
    FLAG_TF   = 1 << 8,   // trap (single-step)
    FLAG_IF   = 1 << 9,   // interrupt enable
    FLAG_DF   = 1 << 10,  // direction (string ops)
    FLAG_OF   = 1 << 11,  // overflow
    FLAG_IOPL = 3 << 12,  // I/O privilege level (2 bits), always 0 in real mode
    FLAG_NT   = 1 << 14,  // nested task, always 0 in real mode
};

// `addr` is a 24-bit physical address, unmasked. in16/out16 are one atomic
// 16-bit bus cycle, needed for the WD1003 data register at 0x1F0 (0x1F1 is
// the Error register, not its high byte). The chipset composes two 8-bit
// accesses for every other port.
struct Bus {
    std::function<uint8_t(uint32_t addr)>          read;
    std::function<void(uint32_t addr, uint8_t v)>  write;
    std::function<uint8_t(uint16_t port)>          in;
    std::function<void(uint16_t port, uint8_t v)>  out;
    std::function<uint16_t(uint16_t port)>         in16;
    std::function<void(uint16_t port, uint16_t v)> out16;

    // Clocks per byte cycle by 4KB page and by port; 6+ is an 8-bit device.
    // Unset is the zero-wait bus the published instruction timings assume.
    const uint8_t* mem_clocks = nullptr;
    std::function<uint8_t(uint16_t port)> io_clocks;
    // Pages with kMemSlotted set hold the cycle until the device grants it:
    // returns the clocks a byte access starting at `now` takes.
    static constexpr uint8_t kMemSlotted = 0x80;
    std::function<int(uint32_t addr, uint64_t now)> mem_wait;
};

class Cpu {
public:
    // Stored as 32-bit for the 0x66 386 concession. AX/AL/AH are the low bits;
    // writing AX leaves EAX's upper 16 bits alone.
    uint32_t ax = 0, bx = 0, cx = 0, dx = 0;
    uint32_t sp = 0, bp = 0, si = 0, di = 0;

    // Segment registers and IP.
    uint16_t cs = 0xF000, ds = 0, es = 0, ss = 0;
    uint16_t ip = 0;

    uint16_t flags = FLAG_R1;

    bool     halted = false;
    uint64_t cycles = 0;   // total clock cycles executed (includes wait states)

#ifdef IBMPCAT_PERF
    // Clocks since the last read. Class clocks exclude waits, the queue tax and prefetch stalls.
    struct Perf {
        uint64_t cls[kPerfClassCount]{};
        uint64_t wait_ram = 0, wait_rom = 0, wait_ega = 0;
        uint64_t wait_io8 = 0, wait_io16 = 0, wait_slot = 0;
        uint64_t stall = 0, qtax = 0;
    };
    Perf perf{};
#endif

    // Called with (CS, IP, opcode-word) on an opcode firmware runs that this
    // core skips as a no-op: 0x00xx single-byte, 0x0Fxx two-byte. Also LOADALL.
    std::function<void(uint16_t cs, uint16_t ip, uint16_t opcode_word)> on_unimplemented;

    // True for physical addresses holding firmware that may use the 386 layer.
    // Unset means none does.
    std::function<bool(uint32_t addr)> firmware_at;

    // A REP string op hands back to the caller after this many cycles so
    // devices advance and interrupts land between iterations. 0 never yields.
    uint32_t rep_yield_cycles = 0;

    explicit Cpu(Bus bus) : bus_(std::move(bus)) {}

    // CS:IP F000:FFF0 with CS's base at FF0000h, so the first fetch is FFFFF0h
    // (Intel iAPX 286 PRM, "Initialization").
    void reset();

    // A fault while delivering #DF puts the 286 in shutdown, which an AT board
    // turns into RESET (IBM AT Technical Reference, "Shutdown").
    bool shutdown() const { return shutdown_; }

    // Executes one instruction at CS:IP and returns its clock cycles (iAPX 286
    // PRM / 80286 data sheet). Shift, string and MUL/DIV groups compute their
    // own cost.
    int step();

    // INTR: INT n's cost plus the two INTA bus cycles. Returns its clocks. The
    // caller checks IF and the interrupt shadow first. Wakes HLT.
    int hardware_interrupt(uint8_t vector);

    // STI, MOV SS and POP SS hold off INTR for one instruction.
    bool interrupt_shadow() const { return shadow_; }

    uint16_t msw() const { return msw_; }
    int cpl() const { return pe() ? cpl_ : 0; }

    bool flag(Flag f) const { return (flags & f) != 0; }
    void set_flag(Flag f, bool on) { flags = on ? (flags | f) : (flags & ~f); }

    // 8-bit register by ModR/M encoding (0=AL..3=BL, 4=AH..7=BH).
    uint8_t  get_reg8(int idx) const;
    void     set_reg8(int idx, uint8_t v);
    // 16-bit register by encoding (0=AX,1=CX,2=DX,3=BX,4=SP,5=BP,6=SI,7=DI).
    // Upper 16 bits are preserved.
    uint16_t get_reg16(int idx) const;
    void     set_reg16(int idx, uint16_t v);
    // 32-bit form (0x66 prefix).
    uint32_t get_reg32(int idx) const;
    void     set_reg32(int idx, uint32_t v);

private:
    Bus bus_;

    // Segment override (-1 = none) and REP/REPNE state; reset each step().
    int  seg_override_ = -1;
    enum { SEG_ES = 0, SEG_CS = 1, SEG_SS = 2, SEG_DS = 3 };
    enum RepMode { REP_NONE, REP_Z, REP_NZ };
    RepMode rep_ = REP_NONE;
    // 0x67 address-size is unsupported: only 32-bit operands are observed.
    bool opsize32_ = false;

    uint16_t &seg_reg(int idx);   // ES/CS/SS/DS by the indices above

    // Exception vectors (Intel iAPX 286 PRM, "Interrupts and Exceptions").
    enum : uint8_t {
        EXC_DE = 0, EXC_DB = 1, EXC_BR = 5, EXC_UD = 6, EXC_NM = 7, EXC_DF = 8,
        EXC_TS = 10, EXC_NP = 11, EXC_SS = 12, EXC_GP = 13
    };

    // A fault found mid-instruction: further bus traffic is dropped, and the
    // end of step() restores the registers and vectors through fault_vec_.
    bool     fault_ = false;
    uint8_t  fault_vec_ = 0;
    uint16_t fault_err_ = 0;
    // Error-code EXT bit while delivering an interrupt or exception.
    uint16_t ext_ = 0;
    void raise_err(uint8_t vec, uint16_t err) {
        if (fault_) return;
        fault_ = true;
        fault_vec_ = vec;
        fault_err_ = uint16_t(err | ext_);
    }
    void raise(uint8_t vec) { raise_err(vec, 0); }
    void raise_sel(uint8_t vec, uint16_t sel) { raise_err(vec, uint16_t(sel & 0xFFFC)); }
    bool in_interrupt_ = false;
    // An operand that runs past the segment limit: #SS on the stack, #GP otherwise.
    void overrun(bool stack) { if (!in_interrupt_) raise(stack ? EXC_SS : EXC_GP); }

    // Hidden descriptor cache behind each segment register (Intel iAPX 286
    // PRM, "Segment Descriptor Cache Registers"). Real-mode loads set only
    // the base, so limit and rights outlast a return from protected mode.
    struct SegCache {
        uint32_t base = 0;
        uint16_t limit = 0xFFFF;
        uint8_t  access = 0x93;  // unsourced (IBM_PCAT_REVIEW.md §63.6)
        uint16_t sel = 0;
    };
    SegCache sc_[4];
    void load_seg_real(int si, uint16_t v) {
        seg_reg(si) = v;
        sc_[si].sel = v;
        sc_[si].base = uint32_t(v) << 4;
    }
    // A host or test may assign a public segment field directly between steps.
    void refresh_real_bases() {
        for (int i = 0; i < 4; ++i) {
            uint16_t v = seg_reg(i);
            if (sc_[i].sel != v) { sc_[i].sel = v; sc_[i].base = uint32_t(v) << 4; }
        }
    }

    // The 286 drives 24 address lines.
    uint32_t phys(int si, uint16_t off) const { return (sc_[si].base + off) & 0xFFFFFFu; }
    // Offsets off..off+len-1 against the cached limit (and rights in protected mode).
    void check(int si, uint16_t off, int len, bool write) {
        if (pe()) { pm_check(si, off, len, write); return; }
        if (uint32_t(off) + uint32_t(len - 1) > sc_[si].limit) overrun(si == SEG_SS);
    }
    void pm_check(int si, uint16_t off, int len, bool write);
    // Bus time this step's data cycles took, and how much of it the published
    // counts don't already include (2 clocks per cycle).
    int bus_busy_ = 0, bus_extra_ = 0;
    void charge(int clocks) { bus_busy_ += clocks; bus_extra_ += clocks - 2; }
    int  mem_clk(uint32_t a) const { return bus_.mem_clocks ? bus_.mem_clocks[(a >> 12) & 0xFFF] & ~Bus::kMemSlotted : 2; }
    bool slotted(uint32_t a) const { return bus_.mem_clocks && (bus_.mem_clocks[(a >> 12) & 0xFFF] & Bus::kMemSlotted) && bus_.mem_wait; }
    int byte_clocks(uint32_t a, int after = 0) {
        int k = mem_clk(a);
        if (slotted(a)) k = std::max(k, bus_.mem_wait(a, cycles + uint64_t(bus_busy_ + after)));
        return k;
    }
    void charge_byte(uint32_t a) {
        int k = byte_clocks(a);
        charge(k);
        IBMPCAT_NOTE_MEM(a, k);
    }
    int  io_clk(uint16_t p) const { return bus_.io_clocks ? bus_.io_clocks(p) : 2; }
    // A word is one cycle to a 16-bit device at an even address, two at an odd
    // one, and always two byte cycles to an 8-bit device.
    int  word_clocks(int k, bool odd) const { return (k >= 6 || odd) ? 2 * k : k; }
    void charge_word(uint32_t a) {
        int k;
        if (slotted(a)) {
            int lo = byte_clocks(a);
            k = lo + byte_clocks(a + 1, lo);
        } else {
            k = word_clocks(mem_clk(a), a & 1);
        }
        charge(k);
        IBMPCAT_NOTE_MEM(a, k);
    }

    uint8_t  raw_rb(int si, uint16_t off)            { return fault_ ? uint8_t(0xFF) : bus_.read(phys(si, off)); }
    void     raw_wb(int si, uint16_t off, uint8_t v) { if (!fault_) bus_.write(phys(si, off), v); }
    uint8_t  rb(int si, uint16_t off)            { check(si, off, 1, false); charge_byte(phys(si, off)); return raw_rb(si, off); }
    void     wb(int si, uint16_t off, uint8_t v) { check(si, off, 1, true); charge_byte(phys(si, off)); raw_wb(si, off, v); }
    uint16_t rw(int si, uint16_t off) {
        check(si, off, 2, false);
        charge_word(phys(si, off));
        return raw_rb(si, off) | (uint16_t(raw_rb(si, uint16_t(off + 1))) << 8);
    }
    void     ww(int si, uint16_t off, uint16_t v) {
        check(si, off, 2, true);
        charge_word(phys(si, off));
        raw_wb(si, off, v & 0xFF); raw_wb(si, uint16_t(off + 1), v >> 8);
    }
    uint8_t  in8(uint16_t p) {
        int k = io_clk(p);
        charge(k);
        IBMPCAT_NOTE_IO(k >= 6, k);
        return bus_.in(p);
    }
    void     out8(uint16_t p, uint8_t v) {
        int k = io_clk(p);
        charge(k);
        IBMPCAT_NOTE_IO(k >= 6, k);
        bus_.out(p, v);
    }
    uint16_t in16(uint16_t p) {
        int k = io_clk(p);
        int clocks = word_clocks(k, p & 1);
        charge(clocks);
        IBMPCAT_NOTE_IO(k >= 6, clocks);
        return bus_.in16(p);
    }
    void     out16(uint16_t p, uint16_t v) {
        int k = io_clk(p);
        int clocks = word_clocks(k, p & 1);
        charge(clocks);
        IBMPCAT_NOTE_IO(k >= 6, clocks);
        bus_.out16(p, v);
    }

    // 6-byte prefetch queue, filled a word at a time while the bus is idle
    // (Intel iAPX 286 PRM, bus unit). Published counts assume it's full.
    static constexpr int kQueueBytes = 6;
    int  queue_ = 0;
    int  fetch_progress_ = 0;  // clocks already spent on the word being fetched
    bool flushed_ = true;
    int  prefetch(int exec_clocks);
    uint32_t rd(int si, uint16_t off) {
        check(si, off, 4, false);
        return uint32_t(rw(si, off)) | (uint32_t(rw(si, uint16_t(off + 2))) << 16);
    }
    void     wd(int si, uint16_t off, uint32_t v) {
        check(si, off, 4, true);
        ww(si, off, uint16_t(v & 0xFFFF)); ww(si, uint16_t(off + 2), uint16_t(v >> 16));
    }

    // The 286 faults an instruction over 10 bytes or one that runs past FFFFh.
    int  fetched_ = 0;
    bool fetch_wrapped_ = false;
    uint8_t fetch8() {
        if (++fetched_ > 10 || fetch_wrapped_ || ip > sc_[SEG_CS].limit) raise(EXC_GP);
        if (ip == 0xFFFF) fetch_wrapped_ = true;
        return fault_ ? uint8_t(0xFF) : bus_.read(code_base() + ip++);
    }
    uint16_t fetch16() { uint16_t lo = fetch8(); return uint16_t(lo | (uint16_t(fetch8()) << 8)); }
    uint32_t fetch32() { uint32_t lo = fetch16(); return lo | (uint32_t(fetch16()) << 16); }

    // Stack is always SS. 32-bit push/pop moves SP by 4.
    void     push16(uint16_t v) { sp = uint16_t(sp - 2); ww(SEG_SS, uint16_t(sp), v); }
    uint16_t pop16()            { uint16_t v = rw(SEG_SS, uint16_t(sp)); sp = uint16_t(sp + 2); return v; }
    // sp wraps at 64KB even for 32-bit operands (no 0x67 / big real mode).
    void     push32(uint32_t v) { sp = (sp - 4) & 0xFFFF; wd(SEG_SS, uint16_t(sp), v); }
    uint32_t pop32()            { uint32_t v = rd(SEG_SS, uint16_t(sp)); sp = (sp + 4) & 0xFFFF; return v; }

    // Register state at the start of the instruction (or REP iteration) a
    // fault rolls back to.
    struct Snapshot {
        uint32_t ax = 0, bx = 0, cx = 0, dx = 0, sp = 0, bp = 0, si = 0, di = 0;
        uint16_t cs = 0, ds = 0, es = 0, ss = 0, flags = 0, msw = 0;
        SegCache sc[4];
        uint32_t gdt_base = 0, idt_base = 0;
        uint16_t gdt_limit = 0, idt_limit = 0;
        bool halted = false;
        uint8_t cpl = 0;
        uint16_t ldtr_sel = 0, tr_sel = 0;
        uint32_t ldt_base = 0, tr_base = 0;
        uint16_t ldt_limit = 0, tr_limit = 0;
    };
    Snapshot snap_{};
    void save_regs();
    void restore_regs();

    bool firmware() const { return firmware_at && firmware_at(code_base() + instr_start_ip_); }

    uint32_t code_base() const { return sc_[SEG_CS].base; }
    void ud() { raise(EXC_UD); }

    // Machine status word and descriptor-table registers. MSW resets to FFF0h
    // and the IDT to base 0, limit 3FFh (Intel iAPX 286 PRM, "Reset").
    uint16_t msw_ = 0xFFF0;
    uint32_t gdt_base_ = 0, idt_base_ = 0;
    uint16_t gdt_limit_ = 0xFFFF, idt_limit_ = 0x03FF;
    enum : uint16_t { MSW_PE = 1, MSW_MP = 2, MSW_EM = 4, MSW_TS = 8 };
    int system_0f01();  // 0x0F 0x01: SGDT/SIDT/LGDT/LIDT/SMSW/LMSW

    bool shadow_ = false;
    bool vectored_ = false;      // this step entered a handler, so no single-step trap
    bool rep_resume_ = false;    // the last step yielded part-way through a REP
    bool rep_resumed_ = false;   // this step continues one, so setup is already paid
    bool step_each_ = false;     // TF set on a REP: one iteration per step
    bool rep_yield(int iterations, int per_iteration);

    int execute(uint8_t op, int c);

    bool pe() const { return (msw_ & MSW_PE) != 0; }
    // Segment-register load through the descriptor tables in protected mode.
    void load_seg(int si, uint16_t sel);
    // Far transfers. Each returns the clocks it adds to the real-mode form.
    int  far_jmp(uint16_t sel, uint16_t off);
    int  far_call(uint16_t sel, uint16_t off);
    int  far_ret(uint16_t pop_bytes);
    int  iret();

    // --- protected mode ---
    enum class Kind { Soft, Hard, Exception };
    enum class Link { Jmp, Call, Iret };
    // Word 0 limit (gate: offset), word 1 base 0-15 (gate: selector), byte 4
    // base 16-23 (call gate: word count), byte 5 access; bytes 6-7 reserved
    // (Intel iAPX 286 PRM, "Descriptors").
    struct Desc {
        uint16_t w0 = 0, w1 = 0;
        uint8_t  b4 = 0, access = 0;
        uint32_t addr = 0;
        uint32_t base() const { return w1 | (uint32_t(b4) << 16); }
    };
    // 286 system descriptor types; 0 and 8-15 are invalid on a 286.
    enum : int {
        SYS_TSS = 1, SYS_LDT = 2, SYS_TSS_BUSY = 3, SYS_CALL_GATE = 4,
        SYS_TASK_GATE = 5, SYS_INT_GATE = 6, SYS_TRAP_GATE = 7
    };
    static bool acc_p(uint8_t a) { return (a & 0x80) != 0; }
    static int  acc_dpl(uint8_t a) { return (a >> 5) & 3; }
    static bool acc_seg(uint8_t a) { return (a & 0x10) != 0; }
    static bool acc_code(uint8_t a) { return (a & 0x18) == 0x18; }
    static bool acc_conforming(uint8_t a) { return acc_code(a) && (a & 0x04); }
    static bool acc_readable(uint8_t a) { return acc_seg(a) && (!acc_code(a) || (a & 0x02)); }
    static bool acc_writable(uint8_t a) { return (a & 0x1A) == 0x12; }
    static int  sys_type(uint8_t a) { return acc_seg(a) ? 0 : (a & 0x0F); }

    uint8_t  cpl_ = 0;
    uint16_t ldtr_sel_ = 0, tr_sel_ = 0;
    uint32_t ldt_base_ = 0, tr_base_ = 0;
    uint16_t ldt_limit_ = 0, tr_limit_ = 0;
    bool     shutdown_ = false;

    // Descriptor-table and TSS accesses are physical.
    uint8_t  sys_rb(uint32_t a);
    uint16_t sys_rw(uint32_t a);
    void     sys_wb(uint32_t a, uint8_t v);
    void     sys_ww(uint32_t a, uint16_t v);
    bool read_desc(uint16_t sel, uint8_t vec, Desc &d);
    bool probe_desc(uint16_t sel, Desc &d);
    void set_accessed(const Desc &d);
    void commit_seg(int si, uint16_t sel, const Desc &d);
    void load_cs(uint16_t sel, const Desc &d, int new_cpl);
    void load_seg_pm(int si, uint16_t sel, uint8_t vec);
    bool code_target_ok(uint16_t sel, const Desc &d, bool check_rpl);
    bool inner_stack(int dpl, uint16_t &new_ss, uint16_t &new_sp, Desc &sd);
    bool outer_return(uint16_t sp_at, int rpl, uint16_t &new_ss, uint16_t &new_sp, Desc &sd);
    void drop_outer_segs();
    void set_flags_pm(uint16_t v);
    bool io_ok();
    bool cpl0();

    void real_interrupt(uint8_t vector);
    int  pm_interrupt(uint8_t vector, Kind kind, uint16_t err);
    // One delivery attempt; returns clocks beyond the real-mode INT n's 23.
    int  deliver(uint8_t vector, Kind kind, uint16_t err = 0);
    // deliver(), escalating a fault on the way to #DF and then shutdown.
    int  raise_event(uint8_t vector, Kind kind, uint16_t err = 0);
    int  task_switch(uint16_t sel, Link link);
    int  system_0f00();
    int  lar_lsl(bool lsl);

    // --- ModR/M decode ---
    // Register (is_mem=false) or memory operand with default-segment rules
    // applied (BP-based defaults to SS, 8086 manual table 2-19).
    struct RM {
        bool     is_mem;
        int      reg;      // valid when !is_mem
        int      seg;      // segment index, valid when is_mem
        uint16_t off;
    };
    // Decodes ModR/M and any displacement at CS:IP, advancing IP.
    RM decode_modrm();
    uint8_t  rm_read8(const RM &rm)              { return rm.is_mem ? rb(rm.seg, rm.off) : get_reg8(rm.reg); }
    void     rm_write8(const RM &rm, uint8_t v)  { if (rm.is_mem) wb(rm.seg, rm.off, v); else set_reg8(rm.reg, v); }
    uint16_t rm_read16(const RM &rm)             { return rm.is_mem ? rw(rm.seg, rm.off) : get_reg16(rm.reg); }
    void     rm_write16(const RM &rm, uint16_t v){ if (rm.is_mem) ww(rm.seg, rm.off, v); else set_reg16(rm.reg, v); }
    uint32_t rm_read32(const RM &rm)             { return rm.is_mem ? rd(rm.seg, rm.off) : get_reg32(rm.reg); }
    void     rm_write32(const RM &rm, uint32_t v){ if (rm.is_mem) wd(rm.seg, rm.off, v); else set_reg32(rm.reg, v); }
    // Offset:segment pair (LES/LDS/far CALL/JMP/BOUND). Memory only; a register is #UD.
    bool     far_operand(const RM &rm, uint16_t &lo, uint16_t &hi) {
        if (!rm.is_mem) { ud(); return false; }
        check(rm.seg, rm.off, 4, false);
        lo = rw(rm.seg, rm.off);
        hi = rw(rm.seg, uint16_t(rm.off + 2));
        return !fault_;
    }

    // flag helpers
    void set_pzs8(uint8_t r);
    void set_pzs16(uint16_t r);
    void set_pzs32(uint32_t r);
    static bool parity_even(uint8_t v);

    // ALU primitives set CF/OF/AF/PF/ZF/SF per the Intel flag tables.
    uint8_t  add8(uint8_t a, uint8_t b, bool carry_in);
    uint16_t add16(uint16_t a, uint16_t b, bool carry_in);
    uint8_t  sub8(uint8_t a, uint8_t b, bool borrow_in);
    uint16_t sub16(uint16_t a, uint16_t b, bool borrow_in);
    uint8_t  and8(uint8_t a, uint8_t b);
    uint16_t and16(uint16_t a, uint16_t b);
    uint8_t  or8(uint8_t a, uint8_t b);
    uint16_t or16(uint16_t a, uint16_t b);
    uint8_t  xor8(uint8_t a, uint8_t b);
    uint16_t xor16(uint16_t a, uint16_t b);
    // 32-bit forms (0x66).
    uint32_t add32(uint32_t a, uint32_t b, bool carry_in);
    uint32_t sub32(uint32_t a, uint32_t b, bool borrow_in);
    uint32_t and32(uint32_t a, uint32_t b);
    uint32_t or32(uint32_t a, uint32_t b);
    uint32_t xor32(uint32_t a, uint32_t b);

    uint8_t  alu_apply8(int alu, uint8_t a, uint8_t b);   // alu: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP = 0-7
    uint16_t alu_apply16(int alu, uint16_t a, uint16_t b);
    uint32_t alu_apply32(int alu, uint32_t a, uint32_t b);

    // Shift/rotate group; `count` is already masked mod 32.
    uint8_t  shiftrot8(int op, uint8_t v, int count);
    uint16_t shiftrot16(int op, uint16_t v, int count);
    uint32_t shiftrot32(int op, uint32_t v, int count);

    // BCD adjust and single-purpose instructions.
    void daa(); void das(); void aaa(); void aas(); void aam(); void aad();
    void push_reg(int idx);         // PUSH SP pushes the value from before the push
    void pusha(); void popa();      // 286-native PUSHA/POPA
    void bound();                   // 286-native BOUND r16, m16&16
    void imul_imm16(int dst_reg, const RM &rm, uint16_t imm);  // 286 IMUL r16,r/m16,imm
    void imul_imm32(int dst_reg, const RM &rm, uint32_t imm);  // 0x66-prefixed 32-bit form
    // enter() returns the nesting level; ENTER cost is 11 / 15 / 12+4*(lex-1) for level 0 / 1 / >1.
    int  enter(); void leave();

    int  extra_cycles_ = 0;  // added to the opcode's base cost by step()

    int      last_reg_ = 0;        // ModR/M reg field of the last decode_modrm(), used by group opcodes
    uint16_t instr_start_ip_ = 0;  // first prefix byte: where a fault restarts

    // Helpers used by step()'s switch. string_op, io_string_op, grp2_shift and
    // grp3_unary return their own cycle cost, which varies by sub-opcode and
    // iteration count.
    //
    // Intel publishes ranges for queue-flushing transfers (Jcc taken 7-10, LOOP 8-11, CALL/JMP near 7-10,
    // far 11-14/13-16, RET 11-14/15-18, IRET 17-20; iAPX 286 PRM timing
    // appendix via Art of Assembly App. D), so each adds a flat tax on its
    // floor cost. 2 keeps floor+tax inside every range. Not-taken branches pay none.
    static constexpr int kQueueRefillTax = 2;
    // A taken control transfer empties the prefetch queue, even one to the next instruction.
    bool transfer_ = false;
    int flush() {
        transfer_ = true;
        IBMPCAT_PERF_ADD(perf_tax_step_, kQueueRefillTax);
        return kQueueRefillTax;
    }
#ifdef IBMPCAT_PERF
    int perf_tax_step_ = 0;
    void note_mem_wait(uint32_t addr, int clocks);
    void note_io_wait(bool eight, int clocks);
#endif

    bool cond(int cc) const;          // Jcc/LOOPcc condition, cc = opcode low nibble
    void jcc(bool taken);
    int  loop_group(uint8_t op);       // 0xE0-0xE3: LOOP/LOOPE/LOOPNE/JCXZ
    int  string_op(uint8_t op);       // 0xA4-0xA7, 0xAA-0xAF: MOVS/CMPS/STOS/LODS/SCAS, honors REP/REPE/REPNE
    int  io_string_op(uint8_t op);    // 0x6C-0x6F: INS/OUTS
    int  grp1_immed(uint8_t op);      // 0x80/0x81/0x83: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m,imm
    int  grp2_shift(uint8_t op);      // 0xC0/C1/D0-D3: shift/rotate group
    int  grp3_unary(uint8_t op);      // 0xF6/0xF7: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV
    int  grp5(uint8_t op);            // 0xFE/0xFF: INC/DEC/CALL/JMP/PUSH r/m
};

} // namespace cpu80286

#endif // IBMPCAT_CPU80286_H
