// IBM Printer Adapter / Centronics parallel port (IBM PC/AT Technical
// Reference, Serial/Parallel Adapter). Data 378h, status 379h, control 37Ah
// for LPT1. A rising edge of STROBE (control bit 0) captures the data latch
// into an in-memory buffer the front end can download, and pulses /ACK so
// BIOS INT 17h (which samples status bit 6 once) succeeds. BUSY holds for
// one character time for status pacing; it does not block the next strobe.
#ifndef SHARED_PC_LPT_H
#define SHARED_PC_LPT_H

#include <cstdint>
#include <vector>

namespace pc {

class Lpt {
public:
    // `cps` is characters per second while BUSY after each strobe (EPSON-class
    // draft ~150). 0 = unlimited (tests).
    explicit Lpt(uint16_t base = 0x378, int cps = 150) : base_(base), cps_(cps) { reset(); }

    void reset();

    bool owns(uint16_t port) const {
        uint16_t off = uint16_t(port - base_);
        return off <= 2;
    }
    uint8_t in(uint16_t port);
    void out(uint16_t port, uint8_t v);

    void tick(uint64_t cpu_cycles, double cpu_hz);

    // Control bit 4 enables IRQ; ACK falling edge while enabled raises it.
    bool irq_pending() const { return irq_; }

    const std::vector<uint8_t> &capture() const { return capture_; }
    void clear_capture() { capture_.clear(); }
    std::size_t capture_size() const { return capture_.size(); }

    uint16_t base() const { return base_; }

private:
    uint16_t base_;
    int cps_;
    uint8_t data_ = 0;
    uint8_t control_ = 0xC4;  // /INIT high idle; bits 6-7 often read 1 on AT adapters
    bool strobe_ = false;
    bool busy_ = false;
    bool ack_low_ = false;
    bool irq_ = false;
    uint64_t busy_until_ = 0;
    uint64_t ack_until_ = 0;
    uint64_t now_ = 0;
    double hz_ = 0;
    std::vector<uint8_t> capture_;

    uint8_t status() const;
};

}  // namespace pc

#endif  // SHARED_PC_LPT_H
