// Emscripten wrapper for the Nintendo Donkey Kong (1981) arcade board.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <array>
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
        m_.load_roms(dkong::hwtest_set());
        m_.reset();
    }

    void reset() { m_.reset(); }

    void loadHwtest() {
        m_.load_roms(dkong::hwtest_set());
        m_.reset();
    }

    // program 16K (5E 5C 5B 5A), chars 4K (3P 3N), objs 8K (7C 7D 7E 7F),
    // proms 768 (2F 2E 2N), sound 4K (3H 3F).
    void loadRomSet(val prog, val chars, val objs, val proms, val sound) {
        dkong::RomSet s;
        copy_val(prog, s.program.data(), s.program.size());
        std::array<uint8_t, 0x1000> c{};
        copy_val(chars, c.data(), c.size());
        std::copy_n(c.begin(), 0x800, s.video.char_3p.begin());
        std::copy_n(c.begin() + 0x800, 0x800, s.video.char_3n.begin());
        std::array<uint8_t, 0x2000> o{};
        copy_val(objs, o.data(), o.size());
        for (int i = 0; i < 4; i++)
            std::copy_n(o.begin() + i * 0x800, 0x800, s.video.obj[i].begin());
        std::array<uint8_t, 0x300> p{};
        copy_val(proms, p.data(), p.size());
        std::copy_n(p.begin(), 256, s.video.prom_2f.begin());
        std::copy_n(p.begin() + 256, 256, s.video.prom_2e.begin());
        std::copy_n(p.begin() + 512, 256, s.video.prom_2n.begin());
        std::array<uint8_t, 0x1000> a{};
        copy_val(sound, a.data(), a.size());
        std::copy_n(a.begin(), 0x800, s.sound_prog.begin());
        std::copy_n(a.begin() + 0x800, 0x800, s.sound_data.begin());
        m_.load_roms(s);
        m_.reset();
    }

    int runCycles(int n) { return m_.run_cycles(n); }

    val frameBuffer() {
        m_.render(frame_.clear());
        return frame_.rgba();
    }

    void setIn0(int v) { m_.inputs.in0 = uint8_t(v); }
    void setIn1(int v) { m_.inputs.in1 = uint8_t(v); }
    void setIn2(int v) { m_.inputs.in2 = uint8_t(v); }
    void setDip(int v) { m_.inputs.dip = uint8_t(v); }
    void setCoin(bool v) { m_.inputs.coin = v; }
    void setAudioHz(int hz) { m_.audio_hz = hz; }
    int in0() const { return m_.inputs.in0; }
    int in1() const { return m_.inputs.in1; }
    int in2() { return m_.mem_read(0x7D00); }
    int dip() const { return m_.inputs.dip; }

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
        s.set("sp", int(m_.cpu.sp));
        s.set("frames", m_.frames);
        s.set("nmiEnable", m_.nmi_enabled());
        s.set("coin", m_.coin_latched());
        s.set("soundPc", int(m_.sound.cpu.pc));
        s.set("dac", int(m_.sound.dac()));
        return s;
    }

    double totalCycles() const { return double(m_.cpu.cycles); }
    int screenWidth() const { return dkong::kUprightW; }
    int screenHeight() const { return dkong::kUprightH; }
    int ramByte(int addr) const {
        if (addr < 0x6000 || addr >= 0x7000) return -1;
        return m_.work_ram[unsigned(addr - 0x6000)];
    }
    void setRamByte(int addr, int v) {
        if (addr < 0x6000 || addr >= 0x7000) return;
        m_.work_ram[unsigned(addr - 0x6000)] = uint8_t(v);
    }
    void setMemByte(int addr, int v) {
        m_.mem_write(uint16_t(addr & 0xFFFF), uint8_t(v));
    }
    int memRead(int addr) { return m_.mem_read(uint16_t(addr & 0xFFFF)); }

private:
    dkong::Machine m_;
    arcade::RgbaFrame<dkong::kUprightW, dkong::kUprightH> frame_;
};

EMSCRIPTEN_BINDINGS(dkong) {
    emscripten::class_<Machine>("Machine")
        .constructor<>()
        .function("reset", &Machine::reset)
        .function("loadHwtest", &Machine::loadHwtest)
        .function("loadRomSet", &Machine::loadRomSet)
        .function("runCycles", &Machine::runCycles)
        .function("frameBuffer", &Machine::frameBuffer)
        .function("setIn0", &Machine::setIn0)
        .function("setIn1", &Machine::setIn1)
        .function("setIn2", &Machine::setIn2)
        .function("setDip", &Machine::setDip)
        .function("setCoin", &Machine::setCoin)
        .function("setAudioHz", &Machine::setAudioHz)
        .function("in0", &Machine::in0)
        .function("in1", &Machine::in1)
        .function("in2", &Machine::in2)
        .function("dip", &Machine::dip)
        .function("drainAudio", &Machine::drainAudio)
        .function("state", &Machine::state)
        .function("totalCycles", &Machine::totalCycles)
        .function("screenWidth", &Machine::screenWidth)
        .function("screenHeight", &Machine::screenHeight)
        .function("ramByte", &Machine::ramByte)
        .function("setRamByte", &Machine::setRamByte)
        .function("setMemByte", &Machine::setMemByte)
        .function("memRead", &Machine::memRead);
}
