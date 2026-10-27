// NS16450 UART (IBM PC/AT Technical Reference, Serial/Parallel Adapter;
// National Semiconductor NS16450 data sheet). Eight consecutive I/O ports at
// a programmable base (COM1 = 3F8h). DLAB (LCR bit 7) swaps +0/+1 between
// data/IER and the baud divisor. MCR bit 3 (OUT2) gates the IRQ line on the
// AT adapter. RX bytes arrive after a full character time at the programmed
// rate; push_rx queues them for that clock.
#ifndef SHARED_PC_NS16450_H
#define SHARED_PC_NS16450_H

#include <cstdint>
#include <deque>

namespace pc {

class Ns16450 {
public:
    explicit Ns16450(uint16_t base = 0x3F8) : base_(base) { reset(); }

    void reset();

    bool owns(uint16_t port) const { return (port & ~7u) == base_; }
    uint8_t in(uint16_t port);
    void out(uint16_t port, uint8_t v);

    // Advances the bit clock against the CPU's absolute cycle count.
    void tick(uint64_t cpu_cycles, double cpu_hz);

    // True when an enabled interrupt source is pending and OUT2 is set.
    bool irq_pending() const;

    // Queues a received byte (serial mouse, null-modem, …). Delivered after
    // one character time at the programmed baud (idle time is not banked).
    void push_rx(uint8_t b);

    // Modem outputs the guest last wrote (MCR bits 0-1), for attached devices.
    bool dtr() const { return (mcr_ & 0x01) != 0; }
    bool rts() const { return (mcr_ & 0x02) != 0; }
    uint8_t mcr() const { return mcr_; }
    uint8_t ier() const { return ier_; }
    uint8_t lcr() const { return lcr_; }
    uint8_t lsr_peek() const { return lsr_; }
    uint16_t divisor() const { return divisor_; }
    bool rbr_full() const { return rbr_full_; }
    std::size_t rx_queued() const { return rx_q_.size(); }

    uint16_t base() const { return base_; }

private:
    uint16_t base_;
    uint8_t ier_ = 0;
    uint8_t iir_ = 0x01;
    uint8_t lcr_ = 0;
    uint8_t mcr_ = 0;
    uint8_t lsr_ = 0x60;  // THRE | TEMT
    uint8_t msr_ = 0;
    uint8_t scratch_ = 0;
    uint8_t thr_ = 0;
    uint8_t rbr_ = 0;
    uint16_t divisor_ = 1;
    bool rbr_full_ = false;
    bool thre_irq_shadow_ = true;  // THRE interrupt after reset until IIR read or THR write

    std::deque<uint8_t> rx_q_;
    double bit_credit_ = 0;
    uint64_t prev_cycles_ = 0;
    double ratio_hz_ = 0;
    double bits_per_cpu_cycle_ = 0;
    int rx_bits_left_ = 0;  // bits still shifting into RBR; 0 = idle
    uint8_t pending_rx_ = 0;

    bool dlab() const { return (lcr_ & 0x80) != 0; }
    int frame_bits() const;
    double baud() const;
    void recompute_iir();
    void accept_rx_byte(uint8_t b);
    void update_msr_from_mcr();
};

}  // namespace pc

#endif  // SHARED_PC_NS16450_H
