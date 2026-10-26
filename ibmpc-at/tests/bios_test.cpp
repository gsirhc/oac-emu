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

