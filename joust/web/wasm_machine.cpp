// Emscripten wrapper for the Williams Joust (1982) arcade board.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../machine.h"
#include "arcade_frame.h"
#include "../hwtest_set.h"

using emscripten::val;

namespace {

void copy_val(val src, uint8_t* dst, size_t n) {
    auto v = emscripten::convertJSArrayToNumberVector<uint8_t>(src);
    size_t m = v.size() < n ? v.size() : n;
    std::copy(v.begin(), v.begin() + std::ptrdiff_t(m), dst);
}

}  // namespace

class Machine {
public:
    Machine() {
        m_.load_roms(joust::hwtest_set());
        m_.reset();
    }

    // Power cycle: chips and DRAM restart, CMOS keeps its battery.
    void reset() { m_.reset(); }

    void loadHwtest() {
        m_.load_roms(joust::hwtest_set());
        m_.cmos.fill(0);
        m_.reset();
    }

    // bank 36K (ROMs 1-9), fixed 12K (ROMs 10-12), decoder.4, decoder.6, sound 4K.
    void loadRomSet(val bank, val fixed, val d4, val d6, val sound) {
        joust::RomSet s;
        copy_val(bank, s.bank.data(), s.bank.size());
        copy_val(fixed, s.fixed.data(), s.fixed.size());
        copy_val(d4, s.decoder4.data(), s.decoder4.size());
        copy_val(d6, s.decoder6.data(), s.decoder6.size());
        copy_val(sound, s.sound.data(), s.sound.size());
        m_.load_roms(s);
        m_.cmos.fill(0);
        m_.reset();
    }

    int runCycles(int n) { return m_.run_cycles(n); }

    val frameBuffer() {
        m_.render(frame_.clear());
        return frame_.rgba();
    }

    void setP1(int v) { m_.inputs.p1 = uint8_t(v); }
    void setP2(int v) { m_.inputs.p2 = uint8_t(v); }
    void setStarts(int v) { m_.inputs.starts = uint8_t(v); }
    void setDoor(int v) { m_.inputs.door = uint8_t(v); }
    void setDoorOpen(bool v) { m_.inputs.door_open = v; }
    void setAudioHz(int hz) { m_.audio_hz = hz; }
    int p1() const { return m_.inputs.p1; }
    int p2() const { return m_.inputs.p2; }
    int starts() const { return m_.inputs.starts; }
    int door() const { return m_.inputs.door; }
    bool doorOpen() const { return m_.inputs.door_open; }
    void soundWrite(int addr, int v) { m_.sound.write(uint16_t(addr), uint8_t(v)); }
    int widgetPort() const { return m_.widget_pia.port_a(); }

    val drainAudio() {
        val out = val::global("Float32Array").new_(m_.audio.size());
        if (!m_.audio.empty())
            out.call<void>("set",
                           val(emscripten::typed_memory_view(m_.audio.size(), m_.audio.data())));
        m_.audio.clear();
        return out;
    }

    val state() const {
        val s = val::object();
        s.set("pc", int(m_.cpu.pc));
        s.set("frames", m_.frames);
        s.set("line", m_.line());
        s.set("romEnable", m_.rom_enable);
        s.set("watchdogResets", m_.watchdog_resets);
        s.set("soundPc", int(m_.sound.cpu.pc));
        s.set("dac", int(m_.sound.dac()));
        s.set("select", int(m_.sound.pia.port_b() & 0x3F));
        return s;
    }

    double totalCycles() const { return double(m_.cycles); }
    int screenWidth() const { return joust::kScreenW; }
    int screenHeight() const { return joust::kScreenH; }
    int cmosByte(int addr) const { return m_.cmos[unsigned(addr) & 0x3FF]; }
    void setCmosByte(int addr, int v) { m_.cmos[unsigned(addr) & 0x3FF] = uint8_t(v & 0x0F); }
    int memRead(int addr) { return m_.mem_read(uint16_t(addr & 0xFFFF)); }
    void setMemByte(int addr, int v) { m_.mem_write(uint16_t(addr & 0xFFFF), uint8_t(v)); }

private:
    joust::Machine m_;
    arcade::RgbaFrame<joust::kScreenW, joust::kScreenH> frame_;
};

EMSCRIPTEN_BINDINGS(joust) {
    emscripten::class_<Machine>("Machine")
        .constructor<>()
        .function("reset", &Machine::reset)
        .function("loadHwtest", &Machine::loadHwtest)
        .function("loadRomSet", &Machine::loadRomSet)
        .function("runCycles", &Machine::runCycles)
        .function("frameBuffer", &Machine::frameBuffer)
        .function("setP1", &Machine::setP1)
        .function("setP2", &Machine::setP2)
        .function("setStarts", &Machine::setStarts)
        .function("setDoor", &Machine::setDoor)
        .function("setDoorOpen", &Machine::setDoorOpen)
        .function("setAudioHz", &Machine::setAudioHz)
        .function("p1", &Machine::p1)
        .function("p2", &Machine::p2)
        .function("starts", &Machine::starts)
        .function("door", &Machine::door)
        .function("doorOpen", &Machine::doorOpen)
        .function("soundWrite", &Machine::soundWrite)
        .function("widgetPort", &Machine::widgetPort)
        .function("drainAudio", &Machine::drainAudio)
        .function("state", &Machine::state)
        .function("totalCycles", &Machine::totalCycles)
        .function("screenWidth", &Machine::screenWidth)
        .function("screenHeight", &Machine::screenHeight)
        .function("cmosByte", &Machine::cmosByte)
        .function("setCmosByte", &Machine::setCmosByte)
        .function("memRead", &Machine::memRead)
        .function("setMemByte", &Machine::setMemByte);
}
