// Intel MCS-48 (8035 subset), host-agnostic: all I/O goes through Bus callbacks.
// Sources: Grokking the MCS-48 System instruction set listing (mcs48-isa.txt,
// restating the Intel MCS-48 Users Manual), and the 8048/8035 datasheet.

#ifndef SHARED_CPU_MCS48_H
#define SHARED_CPU_MCS48_H

#include <cstdint>
#include <functional>

namespace mcs48 {

// 8035 has no internal program ROM; EA is tied high, so every fetch is
// external (8048/8748/8035 datasheet, "Instruction Fetch Cycle (External)").
struct Bus {
    std::function<uint8_t(uint16_t addr)> read_program;           // 12-bit
    std::function<uint8_t(uint8_t addr)> read_data_ext;           // MOVX A,@R
    std::function<void(uint8_t addr, uint8_t v)> write_data_ext;  // MOVX @R,A

    std::function<uint8_t()> read_p1;
    std::function<void(uint8_t v)> write_p1;
    std::function<uint8_t()> read_p2;
    std::function<void(uint8_t v)> write_p2;

    // 8243 expander ports 4-7, reached through P2 lower + PROG (not modeled
    // as pins here). Optional; a board without an 8243 leaves these unset.
    std::function<uint8_t(int port)> read_pexp;
    std::function<void(int port, uint8_t v)> write_pexp;

    std::function<bool()> read_t0;          // true = T0 pin high
    std::function<bool()> read_t1;          // true = T1 pin high
    std::function<bool()> int_asserted;     // true = /INT pin low

    // BUS port latch (OUTL BUS,A / INS A,BUS / ANL,ORL BUS,data). The
    // datasheet says these must not be used on the 8035, since BUS carries
    // the external program address; optional, defaults to a floating read.
    std::function<uint8_t()> read_bus;
    std::function<void(uint8_t v)> write_bus;

    std::function<void(int cycles)> tick;   // optional, machine cycles elapsed
};

class Cpu {
public:
    uint8_t a = 0;
    uint8_t ram[64] = {};    // internal data RAM (8035: 64 bytes)
    uint16_t pc = 0;         // 12-bit program counter

    bool cy = false, ac = false, f0 = false, f1 = false;
    bool bs = false;         // register bank select, PSW bit 4 (SEL RB0/RB1)
    uint8_t sp = 0;          // 3-bit stack pointer, PSW bits 0-2

    uint8_t t = 0;           // timer/counter register
    bool timer_run = false;  // STRT T / STOP TCNT
    bool counter_run = false;// STRT CNT / STOP TCNT
    bool tf = false;         // timer/counter overflow flag (read by JTF)

    bool int_enabled = false;    // EN I / DIS I
    bool tcnti_enabled = false;  // EN TCNTI / DIS TCNTI
    bool in_service = false;     // set on interrupt accept, cleared by RETR

    bool mb_ff = false;      // DBF: set by SEL MB0/MB1, latched into PC bit 11
                              // by the next JMP/CALL only (not part of PSW)

    uint64_t cycles = 0;     // total machine cycles executed

    explicit Cpu(Bus bus) : bus_(std::move(bus)) {}

    void reset();

    // Decode and execute one instruction (or take a pending interrupt);
    // returns machine cycles consumed (tCY = 2.5us at 6 MHz).
    int step();

    uint8_t psw() const;
    void set_psw(uint8_t v);

private:
    Bus bus_;
    int timer_presc_ = 0;    // machine cycles toward the next STRT T tick (mod 32)
    bool t1_prev_ = true;    // last-sampled T1 level, for STRT CNT edge detect

    uint8_t fetch8();

    uint8_t reg(int r) const;                  // Rr, r=0..7, honors bank select
    void set_reg(int r, uint8_t v);
    uint8_t rd_ind(int r) const;                // @Rr, r=0..1, internal RAM
    void wr_ind(int r, uint8_t v);

    void push_pc_psw();
    void pop_pc(bool restore_psw);

    void add(uint8_t v, bool carry_in);
    void daa();

    void tick_timer(int mcycles, bool timer_was_running, bool counter_was_running);
    int service_interrupt();   // 2 if one was taken, else 0

    int decode(uint8_t op);
};

} // namespace mcs48

#endif // SHARED_CPU_MCS48_H
