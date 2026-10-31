#include "machine.h"

namespace joust {

namespace {

// 5I counts frames; Q4 (count 8) resets the board. 5G 74LS133 + 5H 7402s clear it on exactly $39 at $CBFF.
constexpr int kWatchdogBite = 8;
constexpr int kWatchdogRelease = 16;
constexpr uint8_t kWatchdogKey = 0x39;

// R1-R24 ladders into the Q1-Q3 drivers: each bit adds its conductance.
uint32_t palette_rgb(uint8_t v) {
    const double g3[3] = {1 / 1200.0, 1 / 560.0, 1 / 330.0};
    const double g2[2] = {1 / 560.0, 1 / 330.0};
    auto level3 = [&](int bits) {
        double s = 0;
        for (int i = 0; i < 3; i++)
            if (bits & (1 << i)) s += g3[i];
        return uint32_t(255.0 * s / (g3[0] + g3[1] + g3[2]) + 0.5);
    };
    double sb = 0;
    for (int i = 0; i < 2; i++)
        if ((v >> 6) & (1 << i)) sb += g2[i];
    const uint32_t r = level3(v & 7), g = level3((v >> 3) & 7);
    const uint32_t b = uint32_t(255.0 * sb / (g2[0] + g2[1]) + 0.5);
    return (r << 16) | (g << 8) | b;
}

int line_value(int index) { return index < 4 ? 0xFC + index : index - 4; }

}  // namespace

Machine::Machine() : cpu(make_bus()) {
    widget_pia.in_a = [this] { return widget_inputs(); };
    // PB2-PB6 are tied low on the interface board.
    widget_pia.in_b = [] { return uint8_t(0x00); };
    rom_pia.in_a = [this] { return inputs.door; };
    rom_pia.in_b = [] { return uint8_t(0xFF); };
    rom_pia.out_b = [this](uint8_t v) { sound.set_select(v & 0x3F); };
    blitter.read = [this](uint16_t a) { return mem_read(a); };
    blitter.write = [this](uint16_t a, uint8_t v, uint8_t k) { blit_write(a, v, k); };
    reset();
}

m6809::Bus Machine::make_bus() {
    m6809::Bus b;
    b.read = [this](uint16_t a) { return mem_read(a); };
    b.write = [this](uint16_t a, uint8_t v) { mem_write(a, v); };
    return b;
}

void Machine::reset() {
    dram.fill(0);
    palette.fill(0);
    frame_.fill(0);
    frames = 0;
    watchdog_resets = 0;
    frame_cycle_ = 0;
    audio.clear();
    audio_acc_ = 0;
    sound.reset();
    hard_reset();
}

void Machine::hard_reset() {
    rom_enable = false;
    screen_control = false;
    stall_ = 0;
    watchdog_ = 0;
    release_pending_ = false;
    widget_pia.reset();
    rom_pia.reset();
    line_start(line_value(frame_cycle_ / kLineCycles));
    cpu.reset();
    update_irq();
}

void Machine::load_roms(const RomSet& set) {
    roms_ = set;
    sound.rom = set.sound;
}

uint8_t Machine::widget_inputs() const {
    // IC3/IC4 74LS257 select is the widget PIA's CB2; the ROM reads 3J3 with CB2 high ($6BA6).
    const uint8_t stick = widget_pia.cb2() ? inputs.p1 : inputs.p2;
    return uint8_t((stick & 0x07) | (inputs.starts & 0x30));
}

int Machine::dram_index(uint16_t addr) const {
    const uint8_t e = roms_.decoder4[(addr >> 8) | (screen_control ? 0x100 : 0)];
    // D6-D7 into 4G pick WE1/WE2/WE3; 11 selects nothing.
    const int bank = e >> 6;
    if (bank == 3) return -1;
    return bank * 0x4000 + (e & 0x3F) * 0x100 + (addr & 0xFF);
}

int Machine::line() const { return line_value(frame_cycle_ / kLineCycles); }

void Machine::update_irq() { cpu.set_irq(rom_pia.irq_a() || rom_pia.irq_b()); }

// COUNT 240 (4C, VA10-VA13) into CA1; 4MS IRQ (VA11) into CB1.
void Machine::line_start(int v) {
    rom_pia.set_ca1((v & 0xF0) == 0xF0);
    rom_pia.set_cb1((v & 0x20) != 0);
    update_irq();
}

void Machine::render_line(int v) {
    if (v < kFirstVisibleLine || v >= kFirstVisibleLine + kScreenH) return;
    const uint8_t row = roms_.decoder6[v | (screen_control ? 0x100 : 0)];
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) rgb[i] = palette_rgb(palette[i]);
    uint32_t* out = &frame_[size_t(v - kFirstVisibleLine) * kScreenW];
    // 1H/3H/4H latch a page from each bank, then shift it out a count later.
    for (int c = 0; c < kVisibleCounts; c++) {
        const int page = c + kFirstVisibleCount - 1;
        for (int bank = 0; bank < 3; bank++) {
            const uint8_t px = dram[bank * 0x4000 + page * 0x100 + row];
            *out++ = rgb[px >> 4];
            *out++ = rgb[px & 0x0F];
        }
    }
}

void Machine::render(uint32_t* out) const {
    for (size_t i = 0; i < frame_.size(); i++) out[i] = frame_[i];
}

void Machine::tick(int t) {
    cycles += uint64_t(t);
    sound.advance(t, kCpuHz);
    if (audio_hz > 0) {
        audio_acc_ += double(t) * audio_hz;
        while (audio_acc_ >= kCpuHz) {
            audio_acc_ -= kCpuHz;
            audio.push_back(sound.sample(audio_hz));
        }
    }
    const int before = frame_cycle_ / kLineCycles;
    frame_cycle_ += t;
    int after = frame_cycle_ / kLineCycles;
    for (int i = before; i < after; i++) {
        render_line(line_value(i % kFrameLines));
        if (i + 1 == kFrameLines) {
            frames++;
            if (++watchdog_ == kWatchdogBite) watchdog_resets++;
            if (watchdog_ >= kWatchdogRelease) release_pending_ = true;
        }
        line_start(line_value((i + 1) % kFrameLines));
    }
    if (frame_cycle_ >= kFrameCycles) frame_cycle_ -= kFrameCycles;
}

uint8_t Machine::mem_read(uint16_t addr) {
    if (addr < 0x9000 && rom_enable) return roms_.bank[addr];
    if (addr < 0xC000) {
        const int i = dram_index(addr);
        return i < 0 ? 0xFF : dram[size_t(i)];
    }
    if (addr >= 0xD000) return roms_.fixed[addr - 0xD000u];
    switch (addr & 0xFF00) {
        case 0xC800: {
            uint8_t v = 0xFF;
            if ((addr & 0x0C) == 0x04) v = widget_pia.read(addr & 3);
            else if ((addr & 0x0C) == 0x0C) v = rom_pia.read(addr & 3);
            update_irq();
            return v;
        }
        // 3B 8T97 puts VA8-VA13 on D2-D7.
        case 0xCB00: return uint8_t(line() & 0xFC);
    }
    if ((addr & 0xFC00) == 0xCC00) return uint8_t(0xF0 | (cmos[addr & 0x3FF] & 0x0F));
    return 0xFF;
}

void Machine::mem_write(uint16_t addr, uint8_t v) {
    if (addr < 0xC000) {
        const int i = dram_index(addr);
        if (i >= 0) dram[size_t(i)] = v;
        return;
    }
    if (addr >= 0xD000) return;
    if ((addr & 0xFC00) == 0xC000) {
        palette[addr & 0x0F] = v;
        return;
    }
    if ((addr & 0xFC00) == 0xCC00) {
        // 6E/6G: $CC00-$CCFF only writes with the memory protect line high.
        if ((addr & 0x300) == 0 && !inputs.door_open) return;
        cmos[addr & 0x3FF] = uint8_t(v & 0x0F);
        return;
    }
    switch (addr & 0xFF00) {
        case 0xC800:
            if ((addr & 0x0C) == 0x04) widget_pia.write(addr & 3, v);
            else if ((addr & 0x0C) == 0x0C) rom_pia.write(addr & 3, v);
            update_irq();
            return;
        case 0xC900:
            rom_enable = v & 0x01;
            screen_control = v & 0x02;
            return;
        case 0xCA00:
            if ((addr & 7) == 0) stall_ += blitter.start(v);
            else blitter.write_reg(addr & 7, v);
            return;
        case 0xCB00:
            if ((addr & 0xFF) == 0xFF && v == kWatchdogKey) watchdog_ = 0;
            return;
    }
}

// /WINHU and /WINHL only reach the DRAM; I/O sees the plain write.
void Machine::blit_write(uint16_t addr, uint8_t v, uint8_t inhibit) {
    if (addr < 0xC000) {
        const int i = dram_index(addr);
        if (i < 0) return;
        uint8_t& m = dram[size_t(i)];
        m = uint8_t((m & inhibit) | (v & ~inhibit));
        return;
    }
    mem_write(addr, v);
}

int Machine::run_cycles(int n) {
    int done = 0;
    while (done < n) {
        int t;
        if (watchdog_ >= kWatchdogBite) {
            // /RESET holds while 5I Q4 is high, until the counter wraps.
            t = kLineCycles;
            tick(t);
            if (release_pending_) {
                release_pending_ = false;
                hard_reset();
            }
        } else if (stall_ > 0) {
            // /HALT: the Special Chips own the bus.
            t = stall_ < 64 ? stall_ : 64;
            stall_ -= t;
            tick(t);
        } else {
            t = cpu.step();
            tick(t);
        }
        widget_pia.clock_e();
        rom_pia.clock_e();
        done += t;
    }
    return done;
}

}  // namespace joust
