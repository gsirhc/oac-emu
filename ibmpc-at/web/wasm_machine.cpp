// Emscripten wrapper: binds the 80286 core + AT chipset into one Machine for the browser.
// JS surface via embind:
//   const m = new Module.Machine(extKb, baseKb); // memory in KB, fixed for this power-on
//   m.loadRom(0xF0000, biosBytes);       // BIOS-bochs-legacy at the reset vector
//   m.loadRom(0xC0000, egaBiosBytes);    // egabios.bin extension ROM
//   m.mountHdd(hddBytes);                // C: image, chosen before power-on
//   m.hddDirty() / m.clearHddDirty() / m.hddImage()  // persist C: across power cycles
//   m.hddDirtyPatches()                  // [{offset, bytes}, ...] only what changed
//   m.mountFloppy(0, imgBytes);          // drive A:
//   m.runCycles(66667);                  // one frame at 8 MHz
//   const frame = m.renderFrame(); // RGBA; then renderWidth()/renderHeight()
//   m.injectScancode(0x1E);               // Set 1 scan code (see i8042.h)
//   m.setRtc(1986, 10, 8, 12, 0, 0, 4);   // battery-backed clock, weekday 1 = Sunday
//   const edges = m.speakerEdges();       // {cycles: Float64Array, levels: Uint8Array}
//   m.setDriveSound(true); m.driveSoundEvents();  // Float64Array, 5 per event (see below)
//   m.textScreen();                       // test-only: text-mode screen as a string

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../chipset.h"
#include "../ega_render.h"
#include "../machine.h"

using emscripten::val;
using ibmpcat::Machine;

class WasmMachine {
public:
    WasmMachine(double ext_kb, double base_kb) : m_(uint32_t(ext_kb), uint32_t(base_kb)) { m_.reset(); }

    // CMOS is battery-backed and survives
    void reset() { m_.reset(); }

    // BIOS-bochs-legacy at 0x100000-size (reset vector F000:FFF0 needs a 64KB image) or the EGA BIOS at 0xC0000
    void loadRom(double addr, val bytes) {
        std::vector<uint8_t> data = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
        m_.chipset.load_rom(uint32_t(addr), data.data(), data.size());
    }

    // Runs in sub-chunks so activity LEDs latch; one frame can span a whole HDD BSY window.
    void runCycles(double cycles) {
        int64_t remaining = int64_t(cycles);
        constexpr int64_t kSubChunk = 2000;
        while (remaining > 0) {
            int64_t step = remaining < kSubChunk ? remaining : kSubChunk;
            m_.run_cycles(step);
            remaining -= step;
            if (m_.chipset.hdd.busy()) hdd_activity_latch_ = true;
            if (m_.chipset.fdc.drives[0].motor_on) floppy_activity_latch_[0] = true;
            if (m_.chipset.fdc.drives[1].motor_on) floppy_activity_latch_[1] = true;
        }
    }

    double totalCycles() const { return double(m_.total_cycles()); }
    bool halted() const { return m_.cpu.halted; }

    // Whether this build carries the emulator's own counters. The shipped binary answers false.
    bool perfBuild() const {
#ifdef IBMPCAT_PERF
        return true;
#else
        return false;
#endif
    }
#ifdef IBMPCAT_PERF
    // Counters since the last call, as "name=value" pairs. Reading resets them.
    std::string perfStats() {
        auto &p = m_.cpu.perf;
        char buf[640];
        std::snprintf(buf, sizeof buf,
                      "alu=%llu mov=%llu stack=%llu flow=%llu string=%llu io=%llu misc=%llu hlt=%llu "
                      "ram=%llu rom=%llu ega=%llu io8=%llu io16=%llu slot=%llu "
                      "stall=%llu qtax=%llu refresh=%llu dma=%llu",
                      (unsigned long long)p.cls[cpu80286::kPerfAlu],
                      (unsigned long long)p.cls[cpu80286::kPerfMov],
                      (unsigned long long)p.cls[cpu80286::kPerfStack],
                      (unsigned long long)p.cls[cpu80286::kPerfFlow],
                      (unsigned long long)p.cls[cpu80286::kPerfString],
                      (unsigned long long)p.cls[cpu80286::kPerfIo],
                      (unsigned long long)p.cls[cpu80286::kPerfMisc],
                      (unsigned long long)p.cls[cpu80286::kPerfHlt],
                      (unsigned long long)p.wait_ram, (unsigned long long)p.wait_rom,
                      (unsigned long long)p.wait_ega, (unsigned long long)p.wait_io8,
                      (unsigned long long)p.wait_io16, (unsigned long long)p.wait_slot,
                      (unsigned long long)p.stall, (unsigned long long)p.qtax,
                      (unsigned long long)m_.chipset.perf_refresh,
                      (unsigned long long)m_.chipset.perf_dma);
        p = {};
        m_.chipset.perf_refresh = 0;
        m_.chipset.perf_dma = 0;
        return buf;
    }
#endif

    // ---- EGA screen ----
    // Call renderFrame() before renderWidth()/renderHeight().
    val renderFrame() {
        ibmpcat::RenderScreen(m_.chipset.ega, last_frame_);
        const auto &rgba = last_frame_.rgba;
        val out = val::global("Uint8ClampedArray").new_(rgba.size());
        if (!rgba.empty())
            out.call<void>("set", val(emscripten::typed_memory_view(rgba.size(), rgba.data())));
        return out;
    }
    int renderWidth() const { return last_frame_.width; }
    int renderHeight() const { return last_frame_.height; }

    // ---- keyboard ----
    // raw Set 1 byte; the front end owns the key table
    void injectScancode(int code) { m_.chipset.kbc.inject_scancode(uint8_t(code)); }
    int keyboardLeds() const { return m_.chipset.kbc.leds(); }
    bool keysPending() const { return m_.chipset.kbc.keys_pending(); }

    // ---- serial mouse (COM1) ----
    // Relative move; buttons bit0=left, bit1=right. Y+ is down (Microsoft serial).
    void injectSerialMouse(int dx, int dy, int buttons) {
        m_.chipset.serial_mouse.inject(dx, dy, uint8_t(buttons));
    }
    // Packed COM1/mouse snapshot for tests: powered, ier, mcr, lsr, rbr_full, irq, qsize.
    val serialMouseDebug() {
        auto &u = m_.chipset.com1;
        val o = val::object();
        o.set("powered", m_.chipset.serial_mouse.powered());
        o.set("ier", u.ier());
        o.set("mcr", u.mcr());
        o.set("lcr", u.lcr());
        o.set("lsr", u.lsr_peek());
        o.set("divisor", u.divisor());
        o.set("rbrFull", u.rbr_full());
        o.set("irq", u.irq_pending());
        o.set("queued", int(u.rx_queued()));
        o.set("picImr", m_.chipset.pic_master.imr());
        o.set("picIrr", m_.chipset.pic_master.irr());
        o.set("cpuIf", m_.cpu.flag(cpu80286::FLAG_IF));
        return o;
    }

    // ---- LPT1 capture ----
    int lptSize() const { return int(m_.chipset.lpt1.capture_size()); }
    void lptClear() { m_.chipset.lpt1.clear_capture(); }
    val lptBytes() {
        const auto &cap = m_.chipset.lpt1.capture();
        val out = val::global("Uint8Array").new_(cap.size());
        if (!cap.empty())
            out.call<void>("set", val(emscripten::typed_memory_view(cap.size(), cap.data())));
        return out;
    }
    int ramByte(int addr) const {
        return addr >= 0 && std::size_t(addr) < m_.chipset.mem.size() ? m_.chipset.mem[std::size_t(addr)] : 0;
    }

    void setRtc(int year, int month, int day, int hour, int minute, int second, int weekday) {
        m_.chipset.cmos.set_time(year, month, day, hour, minute, second, weekday);
    }

    // ---- floppy drives (fdc765) ----
    void mountFloppy(int drive, val bytes) {
        std::vector<uint8_t> data = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
        m_.chipset.fdc.mount(drive, data.data(), data.size());
    }
    void unmountFloppy(int drive) { m_.chipset.fdc.unmount(drive); }
    bool floppyPresent(int drive) const { return m_.chipset.fdc.drives[drive & 1].present; }
    bool floppyDirty(int drive) const { return m_.chipset.fdc.drives[drive & 1].dirty; }
    void clearFloppyDirty(int drive) { m_.chipset.fdc.drives[drive & 1].dirty = false; }
    // motor was on at any point since the last call
    bool floppyMotorOn(int drive) {
        bool v = floppy_activity_latch_[drive & 1];
        floppy_activity_latch_[drive & 1] = false;
        return v;
    }
    // current image, possibly written to
    val floppyImage(int drive) {
        const std::vector<uint8_t> &img = m_.chipset.fdc.drives[drive & 1].image;
        val out = val::global("Uint8Array").new_(img.size());
        if (!img.empty())
            out.call<void>("set", val(emscripten::typed_memory_view(img.size(), img.data())));
        return out;
    }

    // ---- hard disk (wd1003) ----
    void mountHdd(val bytes) {
        std::vector<uint8_t> data = emscripten::convertJSArrayToNumberVector<uint8_t>(bytes);
        m_.chipset.hdd.mount(0, data.data(), data.size());
    }
    // busy at any point since the last call
    bool hddBusy() {
        bool v = hdd_activity_latch_;
        hdd_activity_latch_ = false;
        return v;
    }
    // written since last mount, like floppyDirty()
    bool hddDirty() { return m_.chipset.hdd.dirty(0); }
    void clearHddDirty() { m_.chipset.hdd.clear_dirty(0); }
    // current image, possibly written to
    val hddImage() {
        const std::vector<uint8_t> &img = m_.chipset.hdd.image(0);
        val out = val::global("Uint8Array").new_(img.size());
        if (!img.empty())
            out.call<void>("set", val(emscripten::typed_memory_view(img.size(), img.data())));
        return out;
    }
    // The byte ranges dirty_ranges() reports, each as its own Uint8Array (wd1003.h).
    val hddDirtyPatches() {
        auto ranges = m_.chipset.hdd.dirty_ranges(0);
        const std::vector<uint8_t> &img = m_.chipset.hdd.image(0);
        val out = val::array();
        for (const auto &r : ranges) {
            val entry = val::object();
            entry.set("offset", r.offset);
            val bytes = val::global("Uint8Array").new_(r.length);
            bytes.call<void>("set", val(emscripten::typed_memory_view(r.length, img.data() + r.offset)));
            entry.set("bytes", bytes);
            out.call<val>("push", entry);
        }
        return out;
    }

    // ---- PC speaker ----
    // current level, for frames with no edges
    bool speakerLevel() const { return m_.chipset.speaker.level(); }

    // ---- test-only: text-mode screen as a string ----
    // Mirrors ega_render.cpp RenderTextScreen addressing: plane 0 characters, one row per line, "" outside text mode.
    std::string textScreen() const {
        const auto &ega = m_.chipset.ega;
        if (ibmpcat::DetectScreenMode(ega) != ibmpcat::ScreenMode::kText) return "";
        int cols = int(ega.crtc_horizontal_display_end()) + 1;
        int stride = ega.crtc_scanline_stride() > 0 ? ega.crtc_scanline_stride() : cols;
        int rows = (int(ega.crtc_vertical_display_end()) + 1) / (int(ega.crtc_max_scan_line()) + 1);
        std::string out;
        out.reserve(std::size_t(cols + 1) * std::size_t(rows));
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                uint32_t plane_off = ega.display_address((uint32_t(ega.start_offset()) + uint32_t(row * stride + col)) & 0xFFFF, 0);
                out += char(ega.vram[(plane_off << 2) + 0]);
            }
            out += '\n';
        }
        return out;
    }

    // drains the (cpu_cycle, level) edges since the last call; cycles/8e6 is seconds
    val speakerEdges() {
        std::vector<ibmpcat::PcSpeaker::Edge> edges = m_.chipset.speaker.drain_edges();
        std::vector<double> cycles(edges.size());
        std::vector<uint8_t> levels(edges.size());
        for (std::size_t i = 0; i < edges.size(); ++i) {
            cycles[i] = double(edges[i].cpu_cycle);
            levels[i] = edges[i].level ? 1 : 0;
        }
        val c = val::global("Float64Array").new_(cycles.size());
        if (!cycles.empty())
            c.call<void>("set", val(emscripten::typed_memory_view(cycles.size(), cycles.data())));
        val l = val::global("Uint8Array").new_(levels.size());
        if (!levels.empty())
            l.call<void>("set", val(emscripten::typed_memory_view(levels.size(), levels.data())));
        val out = val::object();
        out.set("cycles", c);
        out.set("levels", l);
        return out;
    }

    // ---- drive noise ----
    void setDriveSound(bool on) {
        m_.chipset.fdc.sound.enabled = on;
        m_.chipset.hdd.sound.enabled = on;
        if (!on) {
            m_.chipset.fdc.sound.drain();
            m_.chipset.hdd.sound.drain();
            return;
        }
        for (int i = 0; i < 2; ++i)
            if (m_.chipset.fdc.drives[i].motor_on)
                m_.chipset.fdc.sound.push(double(m_.total_cycles()), pc::DriveSoundEvent::kMotorOn, i);
    }
    // [cycle, kind, source, steps, period] per event; source 0 = A:, 1 = B:, 2 = C:; kinds per pc::DriveSoundEvent
    val driveSoundEvents() {
        std::vector<double> out;
        auto add = [&](const std::vector<pc::DriveSoundEvent>& evs, int base) {
            for (const auto& e : evs) {
                out.insert(out.end(), {e.cycle, double(e.kind), double(base + e.drive), double(e.steps), e.period});
            }
        };
        add(m_.chipset.fdc.sound.drain(), 0);
        add(m_.chipset.hdd.sound.drain(), 2);
        val a = val::global("Float64Array").new_(out.size());
        if (!out.empty()) a.call<void>("set", val(emscripten::typed_memory_view(out.size(), out.data())));
        return a;
    }

private:
    Machine m_;
    ibmpcat::RenderedFrame last_frame_;
    bool hdd_activity_latch_ = false;
    bool floppy_activity_latch_[2] = {false, false};
};

EMSCRIPTEN_BINDINGS(ibmpcat_machine) {
    emscripten::class_<WasmMachine>("Machine")
        .constructor<double, double>()
        .function("reset", &WasmMachine::reset)
        .function("loadRom", &WasmMachine::loadRom)
        .function("runCycles", &WasmMachine::runCycles)
        .function("totalCycles", &WasmMachine::totalCycles)
        .function("halted", &WasmMachine::halted)
        .function("perfBuild", &WasmMachine::perfBuild)
#ifdef IBMPCAT_PERF
        .function("perfStats", &WasmMachine::perfStats)
#endif
        .function("renderFrame", &WasmMachine::renderFrame)
        .function("renderWidth", &WasmMachine::renderWidth)
        .function("renderHeight", &WasmMachine::renderHeight)
        .function("injectScancode", &WasmMachine::injectScancode)
        .function("keyboardLeds", &WasmMachine::keyboardLeds)
        .function("keysPending", &WasmMachine::keysPending)
        .function("injectSerialMouse", &WasmMachine::injectSerialMouse)
        .function("serialMouseDebug", &WasmMachine::serialMouseDebug)
        .function("lptSize", &WasmMachine::lptSize)
        .function("lptClear", &WasmMachine::lptClear)
        .function("lptBytes", &WasmMachine::lptBytes)
        .function("ramByte", &WasmMachine::ramByte)
        .function("setRtc", &WasmMachine::setRtc)
        .function("mountFloppy", &WasmMachine::mountFloppy)
        .function("unmountFloppy", &WasmMachine::unmountFloppy)
        .function("floppyPresent", &WasmMachine::floppyPresent)
        .function("floppyDirty", &WasmMachine::floppyDirty)
        .function("clearFloppyDirty", &WasmMachine::clearFloppyDirty)
        .function("floppyMotorOn", &WasmMachine::floppyMotorOn)
        .function("floppyImage", &WasmMachine::floppyImage)
        .function("mountHdd", &WasmMachine::mountHdd)
        .function("hddBusy", &WasmMachine::hddBusy)
        .function("hddDirty", &WasmMachine::hddDirty)
        .function("clearHddDirty", &WasmMachine::clearHddDirty)
        .function("hddImage", &WasmMachine::hddImage)
        .function("hddDirtyPatches", &WasmMachine::hddDirtyPatches)
        .function("speakerLevel", &WasmMachine::speakerLevel)
        .function("speakerEdges", &WasmMachine::speakerEdges)
        .function("setDriveSound", &WasmMachine::setDriveSound)
        .function("driveSoundEvents", &WasmMachine::driveSoundEvents)
        .function("textScreen", &WasmMachine::textScreen);
}
