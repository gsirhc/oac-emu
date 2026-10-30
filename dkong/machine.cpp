#include "machine.h"

namespace dkong {

namespace {

// Coin: LS123 1N, R59 33K / C46 10u: tw = 0.28·R·C·(1 + 0.7/R[k]) (TI SN74LS123).
constexpr int kCoinPulseCycles = int(0.28 * 33000.0 * 10e-6 * (1.0 + 0.7 / 33.0) * kCpuHz);
// 8257-5: four clocks per DMA cycle; each byte is a ch0 read plus a ch1 write.
constexpr int kDmaClocksPerCycle = 4;

}  // namespace

Machine::Machine() : cpu(make_bus()) { reset(); }

z80::Bus Machine::make_bus() {
    z80::Bus b;
    b.read = [this](uint16_t a) { return mem_read(a); };
    b.write = [this](uint16_t a, uint8_t v) { mem_write(a, v); };
    b.in = [](uint16_t) { return uint8_t(0xFF); };
    b.out = [](uint16_t, uint8_t) {};
    b.irq_data = [] { return uint8_t(0xFF); };
    // 7F holds /WAIT on a VRAM access until the video side lets go.
    b.wait = [this](uint16_t a, z80::Cycle kind) {
        if (kind == z80::Cycle::In || kind == z80::Cycle::Out) return 0;
        if ((a & 0xFC00) != 0x7400) return 0;
        return video.vram_wait();
    };
    b.tick = [this](int t) { tick(t); };
    return b;
}

void Machine::reset() {
    work_ram.fill(0);
    video.vram.fill(0);
    video.obj_ram.fill(0);
    latch_5h = 0;
    latch_6h = 0;
    latch_3d = 0;
    frames = 0;
    audio.clear();
    audio_acc_ = 0;
    coin_timer_ = 0;
    coin_prev_ = inputs.coin;
    dma_stall_ = 0;
    dma_mode_ = 0;
    dma_ff_ = false;
    for (int i = 0; i < 4; i++) {
        dma_addr_[i] = 0;
        dma_tc_[i] = 0;
    }
    video.reset();
    video.flip = true;
    sound.reset();
    cpu.reset();
}

void Machine::load_roms(const RomSet& set) {
    program = set.program;
    video.load(set.video);
    sound.load(set.sound_prog, set.sound_data);
}

void Machine::tick(int t) {
    if (inputs.coin && !coin_prev_) coin_timer_ = kCoinPulseCycles;
    coin_prev_ = inputs.coin;
    if (coin_timer_ > 0) coin_timer_ -= t;

    sound.advance(t);
    if (audio_hz > 0) {
        audio_acc_ += double(t) * audio_hz;
        while (audio_acc_ >= kCpuHz) {
            audio_acc_ -= kCpuHz;
            audio.push_back(sound.sample());
        }
    }

    if (video.advance(t)) {
        frames++;
        // 8F latches /NMI on the /VBLK edge unless $7D84 holds it set.
        if (nmi_enabled()) cpu.set_nmi(true);
    }
}

void Machine::dma_request() {
    // ch0 IOR reads work RAM into 2N; ch1 IOW writes it to object RAM. Rotating
    // priority alternates them; TC stop (mode bit 6) disables each at its end.
    if ((dma_mode_ & 0x03) != 0x03) return;
    const unsigned n0 = (dma_tc_[0] & 0x3FFF) + 1u;
    const unsigned n1 = (dma_tc_[1] & 0x3FFF) + 1u;
    const unsigned n = n0 < n1 ? n0 : n1;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t v = mem_read(uint16_t(dma_addr_[0] + i));
        mem_write(uint16_t(dma_addr_[1] + i), v);
    }
    dma_addr_[0] = uint16_t(dma_addr_[0] + n);
    dma_addr_[1] = uint16_t(dma_addr_[1] + n);
    if (dma_mode_ & 0x40) dma_mode_ &= uint8_t(~0x03);
    dma_stall_ += int(n) * 2 * kDmaClocksPerCycle + 2;
}

uint8_t Machine::mem_read(uint16_t addr) {
    if (addr < 0x4000) return program[addr];
    if (addr >= 0x6000 && addr < 0x7000) return work_ram[addr - 0x6000u];
    if (addr >= 0x7000 && addr < 0x7400) return video.obj_ram[addr - 0x7000u];
    if (addr >= 0x7400 && addr < 0x7800) return video.vram[addr - 0x7400u];
    // 1C: A9..A7 within $7C00-$7DFF.
    switch (addr & 0xFF80) {
        case 0x7C00: return inputs.in0;
        case 0x7C80: return inputs.in1;
        case 0x7D00: return uint8_t((inputs.in2 & 0x7F) | (coin_latched() ? 0x80 : 0));
        case 0x7D80: return inputs.dip;
    }
    return 0xFF;
}

void Machine::latch_write(uint8_t& latch, int bit, bool v) {
    latch = uint8_t(v ? (latch | (1 << bit)) : (latch & ~(1 << bit)));
}

void Machine::mem_write(uint16_t addr, uint8_t v) {
    if (addr >= 0x6000 && addr < 0x7000) {
        work_ram[addr - 0x6000u] = v;
        return;
    }
    if (addr >= 0x7000 && addr < 0x7400) {
        video.obj_ram[addr - 0x7000u] = v;
        return;
    }
    if (addr >= 0x7400 && addr < 0x7800) {
        video.vram[addr - 0x7400u] = v;
        return;
    }
    if (addr >= 0x7800 && addr < 0x7C00) {
        // 8257: A3 picks the mode register; one first/last flip-flop steps
        // every address and count write, low byte first.
        if (addr & 0x08) {
            dma_mode_ = v;
            dma_ff_ = false;
            return;
        }
        const int reg = addr & 0x07;
        uint16_t& r = (reg & 1) ? dma_tc_[reg >> 1] : dma_addr_[reg >> 1];
        r = dma_ff_ ? uint16_t((r & 0x00FF) | (v << 8)) : uint16_t((r & 0xFF00) | v);
        dma_ff_ = !dma_ff_;
        return;
    }
    switch (addr & 0xFF80) {
        case 0x7C00:
            latch_3d = v & 0x0F;
            sound.set_command(latch_3d);
            return;
        case 0x7D00:
            latch_write(latch_6h, addr & 7, v & 1);
            sound.set_triggers(latch_6h);
            return;
        case 0x7D80: {
            const int bit = addr & 7;
            const bool was = (latch_5h >> bit) & 1;
            latch_write(latch_5h, bit, v & 1);
            switch (bit) {
                case 0: sound.set_irq(v & 1); break;
                case 2: video.flip = !(v & 1); break;
                case 3: video.obj_bank = v & 1; break;
                case 4:
                    if (!(v & 1)) cpu.set_nmi(false);
                    break;
                case 5:
                    if ((v & 1) && !was) dma_request();
                    break;
                case 6: video.cref = uint8_t((video.cref & 2) | (v & 1)); break;
                case 7: video.cref = uint8_t((video.cref & 1) | ((v & 1) << 1)); break;
            }
            return;
        }
    }
}

int Machine::run_cycles(int n) {
    int done = 0;
    while (done < n) {
        int t;
        if (dma_stall_ > 0) {
            // BUSAK: the 8257 owns the bus; time still runs.
            t = dma_stall_ < 64 ? dma_stall_ : 64;
            dma_stall_ -= t;
            tick(t);
            cpu.cycles += uint64_t(t);
        } else {
            t = cpu.step();
        }
        done += t;
    }
    return done;
}

}  // namespace dkong
