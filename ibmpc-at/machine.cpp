#include "machine.h"

namespace ibmpcat {

void Machine::configure_factory_cmos() {
    auto &c = chipset.cmos;
    // Floppy types (0x10): high nibble A:, low nibble B:. 1=360KB, 2=1.2MB.
    c.poke(0x10, 0x21);
    // Equipment byte (0x14): bits 7-6 = drives - 1, 5-4 = 0 for an EGA, bit0 = diskettes fitted.
    // rombios.c copies it straight into BDA 40:10.
    c.poke(0x14, 0x41);
    // Base memory 640KB, word at 0x15/0x16.
    c.poke(0x15, 0x80);
    c.poke(0x16, 0x02);
    // Extended memory in KB: Setup's copy at 0x17/0x18, POST's at 0x30/0x31, which
    // is the one BIOS-bochs-legacy's INT 15h AH=88h returns.
    uint32_t ext = chipset.ext_kb();
    c.poke(0x17, uint8_t(ext));
    c.poke(0x18, uint8_t(ext >> 8));
    c.poke(0x30, uint8_t(ext));
    c.poke(0x31, uint8_t(ext >> 8));

    // Fixed disk types (0x12): C: is type 8, the ST-4038's 733/5/17 (IBM PC/AT Technical Reference, FD_TBL)
    c.poke(0x12, 0x80);

    // CMOS checksum over 0x10-0x2D, big-endian at 0x2E/0x2F.
    uint16_t sum = 0;
    for (uint16_t reg = 0x10; reg <= 0x2D; ++reg) sum = uint16_t(sum + c.peek(uint8_t(reg)));
    c.poke(0x2E, uint8_t(sum >> 8));
    c.poke(0x2F, uint8_t(sum & 0xFF));
}

void Machine::run_cycles(int64_t cycles) {
    int64_t target = int64_t(total_cycles_) + cycles;
    while (int64_t(total_cycles_) < target) {
        if (chipset.kbc.reset_requested()) {
            chipset.kbc.clear_reset_request();
            cpu.reset();
        }
        // The board decodes the shutdown cycle and pulses RESET (IBM AT Technical Reference, "Shutdown").
        if (cpu.shutdown()) cpu.reset();
        uint32_t from = (uint32_t(cpu.cs) << 4) + cpu.ip;
        int spent = cpu.step();
        // IBM's POST closes A20 before booting; the stand-in BIOS never does, so
        // the machine closes it when ROM code enters a boot sector at 0000:7C00.
        if ((uint32_t(cpu.cs) << 4) + cpu.ip == 0x7C00 && chipset.is_rom(from)) chipset.kbc.set_a20(false);
        total_cycles_ += uint64_t(spent);
        chipset.tick(total_cycles_, kCpuHz);
        total_cycles_ += uint64_t(chipset.take_held_clocks());
        // An INTA cycle only starts when IF is set; polling earlier would consume IRQs the CPU never served.
        if (cpu.flag(cpu80286::FLAG_IF) && !cpu.interrupt_shadow()) {
            int vec = chipset.poll_interrupt();
            if (vec >= 0) total_cycles_ += uint64_t(cpu.hardware_interrupt(uint8_t(vec)));
        }
    }
}

}  // namespace ibmpcat
