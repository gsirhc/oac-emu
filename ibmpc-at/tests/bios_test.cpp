#include <gtest/gtest.h>

#include "machine.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace {

using ibmpcat::Machine;

constexpr uint32_t kStub = 0x0500, kGdt = 0x0700, kSrc = 0x0800, kOut = 0x0600;

// POST with nothing bootable, then a real-mode stub calling into the BIOS.
class BiosTest : public ::testing::Test {
protected:
    std::unique_ptr<Machine> m;

    void boot(uint32_t ext_kb) {
        std::ifstream f(BIOS_PATH, std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ASSERT_EQ(rom.size(), 65536u) << "make -C ibmpc-at bios";
        m = std::make_unique<Machine>(ext_kb);
        m->chipset.load_rom(0xF0000, rom.data(), rom.size());
        m->reset();
        for (int i = 0; i < 4000 && !(m->cpu.halted && !m->cpu.flag(cpu80286::FLAG_IF)); ++i) m->run_cycles(100000);
        ASSERT_TRUE(m->cpu.halted) << "POST never reached its no-boot-device halt";
    }

    void run_stub(const std::vector<uint8_t>& code) {
        for (size_t i = 0; i < code.size(); ++i) m->chipset.mem[kStub + i] = code[i];
        m->cpu.halted = false;
        m->cpu.cs = 0; m->cpu.ip = uint16_t(kStub);
        m->cpu.ss = 0; m->cpu.sp = 0x7000;
        m->cpu.ds = 0; m->cpu.es = 0;
        for (int i = 0; i < 1000 && !m->cpu.halted; ++i) m->run_cycles(1000);
        ASSERT_TRUE(m->cpu.halted);
    }

    uint16_t word(uint32_t a) const { return uint16_t(m->chipset.mem[a] | (m->chipset.mem[a + 1] << 8)); }
    void desc(uint32_t at, uint32_t base, uint16_t limit) {
        auto& mem = m->chipset.mem;
        mem[at] = uint8_t(limit); mem[at + 1] = uint8_t(limit >> 8);
        mem[at + 2] = uint8_t(base); mem[at + 3] = uint8_t(base >> 8); mem[at + 4] = uint8_t(base >> 16);
        mem[at + 5] = 0x93;
    }
};

TEST_F(BiosTest, PostFillsCom1AndLpt1InTheBda) {
    boot(0);
    // BIOS-bochs-legacy probe: COM1 at 40:00, LPT1 at 40:08 (IBM BDA).
    EXPECT_EQ(word(0x400), 0x3F8);
    EXPECT_EQ(word(0x408), 0x378);
    // Equipment word 40:10: bits 11-9 = serial count, 15-14 = printer count.
    uint16_t eq = word(0x410);
    EXPECT_GE((eq >> 9) & 7, 1u) << "at least one serial port";
    EXPECT_GE((eq >> 14) & 3, 1u) << "at least one parallel port";
}

TEST_F(BiosTest, Int15h88hReportsTheExtendedMemoryInCmos) {
    boot(1600);
    run_stub({0xB4, 0x88, 0xCD, 0x15,          // MOV AH,88h ; INT 15h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0xFA, 0xF4});                    // CLI ; HLT
    EXPECT_EQ(word(kOut), 1600);
}

TEST_F(BiosTest, Int15h87hBlockMoveCopiesIntoExtendedMemoryThroughProtectedMode) {
    boot(1024);
    for (int i = 0; i < 16; ++i) m->chipset.mem[kSrc + i] = uint8_t(0xA0 + i);
    for (int i = 0; i < 0x30; ++i) m->chipset.mem[kGdt + i] = 0;
    desc(kGdt + 0x10, kSrc, 0xFFFF);
    desc(kGdt + 0x18, 0x180000, 0xFFFF);
    run_stub({0xBE, 0x00, 0x07,                // MOV SI,0700h (ES:SI = GDT)
              0xB9, 0x08, 0x00,                // MOV CX,8 words
              0xB4, 0x87, 0xCD, 0x15,          // MOV AH,87h ; INT 15h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0x9C, 0x8F, 0x06, 0x02, 0x06,    // PUSHF ; POP [0602h]
              0xFA, 0xF4});                    // CLI ; HLT
    EXPECT_EQ(word(kOut) >> 8, 0) << "AH is the status";
    EXPECT_EQ(word(kOut + 2) & 1, 0) << "CF clear";
    for (int i = 0; i < 16; ++i) EXPECT_EQ(m->chipset.mem[0x180000 + i], 0xA0 + i) << i;
    EXPECT_EQ(m->cpu.msw() & 1, 0) << "back in real mode";
    EXPECT_FALSE(m->cpu.shutdown());
}

TEST_F(BiosTest, Int15hE801ReportsTheSameExtendedMemory) {
    boot(2048);
    run_stub({0xB8, 0x01, 0xE8, 0xCD, 0x15,    // MOV AX,E801h ; INT 15h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0x89, 0x1E, 0x02, 0x06,          // MOV [0602h],BX
              0x89, 0x0E, 0x04, 0x06,          // MOV [0604h],CX
              0x9C, 0x8F, 0x06, 0x06, 0x06,    // PUSHF ; POP [0606h]
              0xFA, 0xF4});
    EXPECT_EQ(word(kOut), 2048) << "KB between 1MB and 16MB";
    EXPECT_EQ(word(kOut + 2), 0) << "64KB blocks above 16MB";
    EXPECT_EQ(word(kOut + 4), 2048);
    EXPECT_EQ(word(kOut + 6) & 1, 0);
}

}  // namespace

TEST_F(BiosTest, BiosPrintCharToLpt1) {
    boot(0);
    run_stub({0xB4, 0x00,                // MOV AH,00h print
              0xB0, 0x41,                // MOV AL,'A'
              0xBA, 0x00, 0x00,          // MOV DX,0 LPT1
              0xCD, 0x17,                // INT 17h
              0x88, 0x26, 0x00, 0x06,    // MOV [0600h],AH
              0xFA, 0xF4});              // CLI ; HLT
    uint8_t ah = m->chipset.mem[kOut];
    EXPECT_EQ(ah & 0x01, 0) << "timeout, AH=" << std::hex << int(ah);
    EXPECT_EQ(ah & 0x08, 0) << "I/O error, AH=" << std::hex << int(ah);
    ASSERT_EQ(m->chipset.lpt1.capture_size(), 1u);
    EXPECT_EQ(m->chipset.lpt1.capture()[0], 'A');
}
