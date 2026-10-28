#include <gtest/gtest.h>

#include "cpu80286.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using cpu80286::Bus;
using cpu80286::Cpu;

constexpr uint32_t kGdt = 0x20000, kIdt = 0x21000, kLdt = 0x22000;
constexpr uint32_t kCode0 = 0x30000, kCode3 = 0x31000, kData0 = 0x40000, kData3 = 0x41000;
constexpr uint32_t kStack0 = 0x50000, kTssA = 0x60000, kTssB = 0x60100;
constexpr uint16_t kHandler = 0x0800;

enum Sel : uint16_t {
    CODE0 = 0x08, DATA0 = 0x10, STACK0 = 0x18, TSS_A = 0x20, TSS_B = 0x28,
    CODE3 = 0x30, DATA3 = 0x38, GATE3 = 0x40, LDT = 0x48, DOWN = 0x50,
    RODATA = 0x58, XCODE = 0x60, ABSENT = 0x68, HIGH = 0x70, TINY = 0x78,
};

class Cpu80286PmTest : public ::testing::Test {
protected:
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x1000000, 0);
    std::unique_ptr<Cpu> cpu;
    uint8_t next_in = 0x5A;

    void SetUp() override {
        Bus bus;
        bus.read = [this](uint32_t a) { return mem[a & 0xFFFFFF]; };
        bus.write = [this](uint32_t a, uint8_t v) { mem[a & 0xFFFFFF] = v; };
        bus.in = [this](uint16_t) { return next_in; };
        bus.out = [](uint16_t, uint8_t) {};
        bus.in16 = [](uint16_t) -> uint16_t { return 0; };
        bus.out16 = [](uint16_t, uint16_t) {};
        cpu = std::make_unique<Cpu>(bus);
        cpu->reset();

        desc(CODE0, kCode0, 0xFFFF, 0x9B);
        desc(DATA0, kData0, 0xFFFF, 0x93);
        desc(STACK0, kStack0, 0xFFFF, 0x93);
        desc(TSS_A, kTssA, 0x2B, 0x81);
        desc(TSS_B, kTssB, 0x2B, 0x81);
        desc(CODE3, kCode3, 0x0FFF, 0xFB);
        desc(DATA3, kData3, 0x0FFF, 0xF3);
        gate(GATE3, CODE0, 0x0200, 0xE4, 1);
        desc(LDT, kLdt, 0x000F, 0x82);
        desc(DOWN, 0x42000, 0x0FFF, 0x97);
        desc(RODATA, 0x43000, 0xFFFF, 0x91);
        desc(XCODE, kCode0, 0xFFFF, 0x99);
        desc(ABSENT, 0x44000, 0xFFFF, 0x13);
        desc(HIGH, 0x123450, 0xFFFF, 0x93);
        desc(TINY, 0x45000, 0x000F, 0x93);
        w16(kTssA + 4, STACK0); w16(kTssA + 2, 0x7000);  // SS0:SP0
        for (int v = 0; v < 256; ++v) gate(0, CODE0, kHandler, 0x86, 0, kIdt + v * 8);
        w8(kCode0 + kHandler, 0xF4);  // HLT
    }

    void w8(uint32_t a, uint8_t v) { mem[a] = v; }
    void w16(uint32_t a, uint16_t v) { mem[a] = uint8_t(v); mem[a + 1] = uint8_t(v >> 8); }
    uint16_t r16(uint32_t a) const { return uint16_t(mem[a] | (mem[a + 1] << 8)); }
    void desc(uint16_t sel, uint32_t base, uint16_t limit, uint8_t access, uint32_t table = kGdt) {
        uint32_t a = table + (sel & 0xFFF8);
        w16(a, limit);
        w16(a + 2, uint16_t(base));
        w8(a + 4, uint8_t(base >> 16));
        w8(a + 5, access);
        w16(a + 6, 0);
    }
    void gate(uint16_t sel, uint16_t target, uint16_t off, uint8_t access, uint8_t words, uint32_t at = 0) {
        uint32_t a = at ? at : kGdt + (sel & 0xFFF8);
        w16(a, off);
        w16(a + 2, target);
        w8(a + 4, words);
        w8(a + 5, access);
        w16(a + 6, 0);
    }
    void idt_gate(int vec, uint16_t target, uint16_t off, uint8_t access) { gate(0, target, off, access, 0, kIdt + vec * 8); }
    void put(uint32_t a, std::initializer_list<uint8_t> code) { for (uint8_t b : code) mem[a++] = b; }

    // Real-mode LGDT, LIDT, LMSW and JMP 08:0000, then DS, SS:SP and TR from
    // protected mode. `code` follows at CS:0011.
    static constexpr uint16_t kBody = 0x0016;
    void pm(std::initializer_list<uint8_t> code, int n = 1) {
        w16(0x0500, 0x00FF); w16(0x0502, uint16_t(kGdt)); w8(0x0504, uint8_t(kGdt >> 16));
        w16(0x0506, 0x07FF); w16(0x0508, uint16_t(kIdt)); w8(0x050A, uint8_t(kIdt >> 16));
        put(0x1000, {0x0F, 0x01, 0x16, 0x00, 0x05,   // LGDT [0500h]
                     0x0F, 0x01, 0x1E, 0x06, 0x05,   // LIDT [0506h]
                     0xB8, 0x01, 0x00,               // MOV AX,1
                     0x0F, 0x01, 0xF0,               // LMSW AX
                     0xEA, 0x00, 0x00, CODE0, 0x00}); // JMP 08:0000
        put(kCode0, {0xB8, DATA0, 0x00, 0x8E, 0xD8,   // MOV AX,DATA0 ; MOV DS,AX
                     0xB8, STACK0, 0x00, 0x8E, 0xD0,  // MOV AX,STACK0 ; MOV SS,AX
                     0xBC, 0x00, 0x80,                // MOV SP,8000h
                     0xB8, TSS_A, 0x00, 0x0F, 0x00, 0xD8,  // MOV AX,TSS_A ; LTR AX
                     0x31, 0xC0, 0x90});              // XOR AX,AX ; NOP
        put(kCode0 + kBody, code);
        mem[kGdt + TSS_A + 5] = 0x81;
        cpu->reset();
        cpu->cs = 0x0100; cpu->ip = 0; cpu->ds = 0;
        for (int i = 0; i < 5 + 9; ++i) cpu->step();
        ASSERT_EQ(cpu->cs, CODE0);
        ASSERT_EQ(cpu->ip, kBody);
        for (int i = 0; i < n; ++i) cpu->step();
    }

    bool in_handler() const { return cpu->cs == CODE0 && cpu->ip == kHandler + 1 && cpu->halted; }
    // Top of the stack the handler ran on, in SS0.
    uint16_t stack(int i) const { return r16(kStack0 + cpu->sp + 2 * i); }

    // Drops to ring 3 with IRET onto CODE3:0000 and DATA3 as stack and DS.
    void to_ring3(std::initializer_list<uint8_t> code, int n = 1) {
        put(kCode3, code);
        pm({0x6A, DATA3 | 3,      // PUSH SS
            0x68, 0x00, 0x0F,     // PUSH SP 0F00h
            0x68, 0x02, 0x00,     // PUSH FLAGS (IOPL 0, IF 0)
            0x6A, CODE3 | 3,      // PUSH CS
            0x6A, 0x00,           // PUSH IP
            0xCF}, 6);            // IRET
        ASSERT_EQ(cpu->cpl(), 3);
        for (int i = 0; i < n; ++i) cpu->step();
    }
};

TEST_F(Cpu80286PmTest, JumpIntoProtectedModeLoadsCsFromTheGdt) {
    pm({0x8B, 0x06, 0x10, 0x00});  // MOV AX,[0010h]
    w16(kData0 + 0x10, 0xBEEF);
    cpu->ip = kBody;
    cpu->step();
    EXPECT_EQ(cpu->ax & 0xFFFF, 0xBEEFu);
    EXPECT_EQ(cpu->cpl(), 0);
    EXPECT_EQ(cpu->msw() & 1, 1);
}

TEST_F(Cpu80286PmTest, DescriptorBasesReachAbove1MB) {
    pm({0xB8, HIGH, 0x00, 0x8E, 0xC0,       // MOV AX,HIGH ; MOV ES,AX
        0xB8, 0x34, 0x12,                   // MOV AX,1234h
        0x26, 0xA3, 0x06, 0x00}, 4);        // MOV ES:[0006h],AX
    EXPECT_EQ(r16(0x123456), 0x1234);
}

TEST_F(Cpu80286PmTest, LoadingASegmentSetsTheAccessedBit) {
    mem[kGdt + RODATA + 5] = 0x90;
    pm({0xB8, RODATA, 0x00, 0x8E, 0xC0}, 2);  // MOV AX,RODATA ; MOV ES,AX
    EXPECT_EQ(mem[kGdt + RODATA + 5], 0x91);
}

TEST_F(Cpu80286PmTest, OffsetPastTheLimitIsGpWithErrorCodeZero) {
    pm({0xB8, TINY, 0x00, 0x8E, 0xD8,       // MOV AX,TINY ; MOV DS,AX
        0x8B, 0x1E, 0x0F, 0x00}, 3);        // MOV BX,[000Fh]: word runs past limit 0Fh
    cpu->step();
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), 0);                 // error code
    EXPECT_EQ(stack(1), kBody + 5);         // the faulting MOV
}

TEST_F(Cpu80286PmTest, StackPastItsLimitIsSs) {
    idt_gate(12, CODE0, kHandler, 0x86);
    desc(STACK0, kStack0, 0x0FFF, 0x93);
    pm({0xBC, 0x00, 0x20,                   // MOV SP,2000h
        0x50}, 2);                          // PUSH AX at 1FFEh, past 0FFFh
    // The handler's own pushes fault the same way: #SS twice is #DF, then shutdown.
    EXPECT_TRUE(cpu->shutdown());
}

TEST_F(Cpu80286PmTest, NullDsLoadsButFaultsOnUse) {
    pm({0x31, 0xC0, 0x8E, 0xD8,             // XOR AX,AX ; MOV DS,AX
        0x8B, 0x1E, 0x00, 0x00}, 2);        // MOV BX,[0000h]
    EXPECT_EQ(cpu->ds, 0);
    EXPECT_FALSE(cpu->halted);
    cpu->step();
    cpu->step();
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), 0);
}

TEST_F(Cpu80286PmTest, NullSsIsGp) {
    pm({0x31, 0xC0, 0x8E, 0xD0}, 3);        // XOR AX,AX ; MOV SS,AX
    EXPECT_EQ(cpu->ss, STACK0);
    EXPECT_TRUE(in_handler());
}

TEST_F(Cpu80286PmTest, WritesToReadOnlyDataAndCodeAreGp) {
    pm({0xB8, RODATA, 0x00, 0x8E, 0xC0,     // MOV AX,RODATA ; MOV ES,AX
        0x26, 0xA2, 0x00, 0x00}, 4);        // MOV ES:[0000h],AL
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(mem[0x43000], 0);
    pm({0x2E, 0xA2, 0x00, 0x00}, 2);        // MOV CS:[0000h],AL
    EXPECT_TRUE(in_handler());
}

TEST_F(Cpu80286PmTest, ExecuteOnlyCodeCannotBeLoadedIntoDs) {
    pm({0xB8, XCODE, 0x00, 0x8E, 0xD8}, 3); // MOV AX,XCODE ; MOV DS,AX
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), XCODE);
}

TEST_F(Cpu80286PmTest, ExpandDownAcceptsOnlyOffsetsAboveTheLimit) {
    pm({0xB8, DOWN, 0x00, 0x8E, 0xC0,       // MOV AX,DOWN ; MOV ES,AX
        0x26, 0xA2, 0x00, 0x10,             // MOV ES:[1000h],AL
        0x26, 0xA2, 0xFF, 0x0F}, 4);        // MOV ES:[0FFFh],AL
    EXPECT_EQ(mem[0x43000], DOWN);
    cpu->step();
    EXPECT_TRUE(in_handler());
}

TEST_F(Cpu80286PmTest, NotPresentSegmentIsNpWithTheSelector) {
    idt_gate(11, CODE0, kHandler, 0x86);
    pm({0xB8, ABSENT, 0x00, 0x8E, 0xC0}, 3);
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), ABSENT);
}

TEST_F(Cpu80286PmTest, SelectorPastTheGdtLimitIsGp) {
    pm({0xB8, 0xF8, 0x07, 0x8E, 0xC0}, 3);  // MOV AX,07F8h ; MOV ES,AX
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), 0x07F8);
}

TEST_F(Cpu80286PmTest, LldtMakesLdtSelectorsLoadable) {
    desc(0x0C, 0x46000, 0xFFFF, 0x93, kLdt);   // LDT entry 1
    pm({0xB8, LDT, 0x00, 0x0F, 0x00, 0xD0,     // MOV AX,LDT ; LLDT AX
        0xB8, 0x0C, 0x00, 0x8E, 0xC0,          // MOV AX,000Ch ; MOV ES,AX
        0x26, 0xA2, 0x00, 0x00,                // MOV ES:[0],AL
        0x0F, 0x00, 0xC1}, 6);                 // SLDT CX
    EXPECT_EQ(mem[0x46000], 0x0C);
    EXPECT_EQ(cpu->cx & 0xFFFF, LDT);
}

TEST_F(Cpu80286PmTest, LtrMarksTheTssBusy) {
    pm({0x0F, 0x00, 0xC9});                    // STR CX
    EXPECT_EQ(cpu->cx & 0xFFFF, TSS_A);
    EXPECT_EQ(mem[kGdt + TSS_A + 5], 0x83);
}

TEST_F(Cpu80286PmTest, PrivilegedInstructionsAndIoAtRing3AreGp) {
    for (auto code : {std::initializer_list<uint8_t>{0xF4},            // HLT
                      std::initializer_list<uint8_t>{0xFA},            // CLI
                      std::initializer_list<uint8_t>{0xE4, 0x60},      // IN AL,60h
                      std::initializer_list<uint8_t>{0x0F, 0x06},      // CLTS
                      std::initializer_list<uint8_t>{0xF0, 0x90}}) {   // LOCK NOP
        to_ring3(code, 2);
        EXPECT_TRUE(in_handler()) << int(*code.begin());
        EXPECT_EQ(stack(0), 0);
    }
}

TEST_F(Cpu80286PmTest, Ring3CannotLoadARing0DataSegment) {
    to_ring3({0xB8, DATA0, 0x00, 0x8E, 0xD8}, 3);
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), DATA0);
    EXPECT_EQ(stack(4), 0x0F00);               // ring 3 SP, saved on the ring 0 stack
    EXPECT_EQ(stack(5), DATA3 | 3);
}

TEST_F(Cpu80286PmTest, PopfAtRing3KeepsIoplAndIf) {
    to_ring3({0x68, 0x00, 0x32,                // PUSH 3200h (IOPL 3, IF)
              0x9D}, 2);                       // POPF
    EXPECT_EQ(cpu->flags & 0x3200, 0);
}

TEST_F(Cpu80286PmTest, PopfAtRing0SetsIopl) {
    pm({0x68, 0x00, 0x30, 0x9D}, 2);           // PUSH 3000h ; POPF
    EXPECT_EQ(cpu->flags & 0x3000, 0x3000);
}

TEST_F(Cpu80286PmTest, CallGateToRing0SwitchesStackAndCopiesParameters) {
    put(kCode0 + 0x0200, {0xCA, 0x02, 0x00});  // RETF 2
    to_ring3({0x68, 0xCD, 0xAB,                // PUSH ABCDh (the parameter)
              0x9A, 0x00, 0x00, GATE3 | 3, 0x00}, 2);  // CALL GATE3:0000
    EXPECT_EQ(cpu->cs, CODE0);
    EXPECT_EQ(cpu->ip, 0x0200);
    EXPECT_EQ(cpu->ss, STACK0);
    EXPECT_EQ(cpu->sp, 0x7000u - 10);
    EXPECT_EQ(stack(0), 0x0008);               // return IP
    EXPECT_EQ(stack(1), CODE3 | 3);
    EXPECT_EQ(stack(2), 0xABCD);               // copied parameter
    EXPECT_EQ(stack(3), 0x0EFE);               // ring 3 SP
    EXPECT_EQ(stack(4), DATA3 | 3);
    cpu->step();                               // RETF 2
    EXPECT_EQ(cpu->cs, CODE3 | 3);
    EXPECT_EQ(cpu->ip, 0x0008);
    EXPECT_EQ(cpu->ss, DATA3 | 3);
    EXPECT_EQ(cpu->sp, 0x0F00u);
    EXPECT_EQ(cpu->cpl(), 3);
}

TEST_F(Cpu80286PmTest, ReturningOutwardNullsDataSegmentsTheOuterLevelCannotUse) {
    put(kCode0 + 0x0200, {0xB8, DATA0, 0x00, 0x8E, 0xC0,  // MOV AX,DATA0 ; MOV ES,AX
                          0xCA, 0x02, 0x00});              // RETF 2
    to_ring3({0x6A, 0x00, 0x9A, 0x00, 0x00, GATE3 | 3, 0x00}, 5);
    EXPECT_EQ(cpu->cpl(), 3);
    EXPECT_EQ(cpu->es, 0);
}

TEST_F(Cpu80286PmTest, SoftwareIntChecksTheGateDpl) {
    to_ring3({0xCD, 0x21}, 2);                 // INT 21h, gate DPL 0
    EXPECT_TRUE(in_handler());
    EXPECT_EQ(stack(0), 0x21 * 8 + 2);
}

TEST_F(Cpu80286PmTest, InterruptGateClearsIfAndTrapGateKeepsIt) {
    idt_gate(0x40, CODE0, 0x0300, 0x86);
    idt_gate(0x41, CODE0, 0x0300, 0x87);
    pm({0xFB, 0xCD, 0x40}, 2);                 // STI ; INT 40h
    EXPECT_EQ(cpu->ip, 0x0300);
    EXPECT_FALSE(cpu->flag(cpu80286::FLAG_IF));
    EXPECT_EQ(stack(0), kBody + 3);
    EXPECT_EQ(stack(1), CODE0);
    EXPECT_TRUE(stack(2) & 0x0200);
    pm({0xFB, 0xCD, 0x41}, 2);
    EXPECT_TRUE(cpu->flag(cpu80286::FLAG_IF));
}

TEST_F(Cpu80286PmTest, HardwareInterruptIgnoresTheGateDplAndIretReturns) {
    idt_gate(0x50, CODE0, 0x0300, 0x86);
    put(kCode0 + 0x0300, {0xCF});              // IRET
    to_ring3({0x90}, 0);
    cpu->hardware_interrupt(0x50);
    EXPECT_EQ(cpu->ip, 0x0300);
    EXPECT_EQ(cpu->cpl(), 0);
    cpu->step();
    EXPECT_EQ(cpu->cs, CODE3 | 3);
    EXPECT_EQ(cpu->ss, DATA3 | 3);
    EXPECT_EQ(cpu->cpl(), 3);
}

TEST_F(Cpu80286PmTest, InterruptVectorPastTheIdtLimitIsDoubleFaultThenShutdown) {
    pm({0x0F, 0x01, 0x1E, 0x0C, 0x00,          // LIDT [000Ch] in DATA0: limit 0
        0xCD, 0x20}, 1);                       // INT 20h
    EXPECT_FALSE(cpu->shutdown());
    cpu->step();
    EXPECT_TRUE(cpu->shutdown());
    EXPECT_TRUE(cpu->halted);
    cpu->reset();
    EXPECT_FALSE(cpu->shutdown());
    EXPECT_EQ(cpu->msw() & 1, 0);
}

TEST_F(Cpu80286PmTest, TwoContributoryFaultsMakeADoubleFault) {
    idt_gate(11, CODE0, kHandler, 0x06);       // #NP's own gate is not present
    idt_gate(8, CODE0, 0x0400, 0x86);
    pm({0xB8, ABSENT, 0x00, 0x8E, 0xC0}, 2);   // MOV AX,ABSENT ; MOV ES,AX
    EXPECT_EQ(cpu->ip, 0x0400);
    EXPECT_EQ(stack(0), 0);
    EXPECT_EQ(stack(1), kBody + 3);
}

TEST_F(Cpu80286PmTest, JmpToATssSwitchesTasks) {
    w16(kTssB + 14, 0x0000);                   // IP
    w16(kTssB + 16, 0x0002);                   // FLAGS
    w16(kTssB + 18, 0x1111);                   // AX
    w16(kTssB + 26, 0x6000);                   // SP
    w16(kTssB + 34, DATA3 | 3); w16(kTssB + 36, CODE3 | 3);
    w16(kTssB + 38, DATA3 | 3); w16(kTssB + 40, DATA3 | 3);
    w16(kTssB + 26, 0x0E00);
    pm({0xBB, 0x22, 0x22,                      // MOV BX,2222h
        0xEA, 0x00, 0x00, TSS_B, 0x00}, 2);    // JMP TSS_B:0
    EXPECT_EQ(cpu->ax & 0xFFFF, 0x1111u);
    EXPECT_EQ(cpu->cs, CODE3 | 3);
    EXPECT_EQ(cpu->cpl(), 3);
    EXPECT_EQ(cpu->ss, DATA3 | 3);
    EXPECT_EQ(cpu->sp, 0x0E00u);
    EXPECT_EQ(r16(kTssA + 24), 0x2222);        // old BX saved
    EXPECT_EQ(r16(kTssA + 14), kBody + 8);     // old IP: after the JMP
    EXPECT_EQ(mem[kGdt + TSS_A + 5], 0x81);    // old task no longer busy
    EXPECT_EQ(mem[kGdt + TSS_B + 5], 0x83);
    EXPECT_TRUE(cpu->msw() & 0x0008);          // TS
}

TEST_F(Cpu80286PmTest, CallToATssNestsAndIretReturns) {
    w16(kTssB + 14, 0x0500);
    w16(kTssB + 16, 0x0002);
    w16(kTssB + 26, 0x6000);
    w16(kTssB + 34, DATA0); w16(kTssB + 36, CODE0);
    w16(kTssB + 38, STACK0); w16(kTssB + 40, DATA0);
    put(kCode0 + 0x0500, {0xCF});              // IRET
    pm({0x9A, 0x00, 0x00, TSS_B, 0x00}, 1);    // CALL TSS_B:0
    EXPECT_EQ(cpu->ip, 0x0500);
    EXPECT_TRUE(cpu->flags & cpu80286::FLAG_NT);
    EXPECT_EQ(r16(kTssB), TSS_A);              // back link
    EXPECT_EQ(mem[kGdt + TSS_A + 5], 0x83);    // both busy
    EXPECT_EQ(mem[kGdt + TSS_B + 5], 0x83);
    cpu->step();
    EXPECT_EQ(cpu->ip, kBody + 5);
    EXPECT_EQ(mem[kGdt + TSS_B + 5], 0x81);
    EXPECT_FALSE(cpu->flags & cpu80286::FLAG_NT);
}

TEST_F(Cpu80286PmTest, ANotPresentLdtInTheNewTaskIsTs) {
    desc(0x80, 0x47000, 0x000F, 0x02);         // LDT, not present
    w16(kTssB + 14, 0x0500);
    w16(kTssB + 16, 0x0002);
    w16(kTssB + 26, 0x6000);
    w16(kTssB + 34, DATA0); w16(kTssB + 36, CODE0);
    w16(kTssB + 38, STACK0); w16(kTssB + 40, DATA0);
    w16(kTssB + 42, 0x80);
    idt_gate(10, TSS_A, 0, 0x85);              // #TS through a task gate back to task A
    pm({0xEA, 0x00, 0x00, TSS_B, 0x00}, 1);    // JMP TSS_B:0
    EXPECT_EQ(cpu->ip, kBody + 5);
    EXPECT_EQ(stack(0), 0x80) << "error code: the LDT selector";
    EXPECT_EQ(r16(kTssA), TSS_B) << "back link to the faulting task";
    EXPECT_EQ(r16(kTssB + 14), 0x0500) << "the fault belongs to the new task";
}

TEST_F(Cpu80286PmTest, LarLslVerrVerwReportThroughZf) {
    pm({0xB8, DATA3 | 3, 0x00,
        0x0F, 0x02, 0xD8,                      // LAR BX,AX
        0x0F, 0x03, 0xC8,                      // LSL CX,AX
        0x0F, 0x00, 0xE8,                      // VERW AX
        0xB8, XCODE, 0x00,
        0x0F, 0x00, 0xE0}, 4);                 // VERR AX
    EXPECT_EQ(cpu->bx & 0xFFFF, 0xF300u);
    EXPECT_EQ(cpu->cx & 0xFFFF, 0x0FFFu);
    EXPECT_TRUE(cpu->flag(cpu80286::FLAG_ZF));
    cpu->step();
    cpu->step();
    EXPECT_FALSE(cpu->flag(cpu80286::FLAG_ZF)) << "execute-only code isn't readable";
}

TEST_F(Cpu80286PmTest, ArplRaisesTheRpl) {
    pm({0xB8, 0x10, 0x00, 0xBB, 0x03, 0x00,    // MOV AX,0010h ; MOV BX,3
        0x63, 0xD8}, 3);                       // ARPL AX,BX
    EXPECT_EQ(cpu->ax & 0xFFFF, 0x0013u);
    EXPECT_TRUE(cpu->flag(cpu80286::FLAG_ZF));
}

TEST_F(Cpu80286PmTest, SegmentLoadAndFarJumpCostTheProtectedModeCounts) {
    pm({0x8E, 0xD8, 0xEA, 0x00, 0x01, CODE0, 0x00}, 0);
    cpu->ax = DATA0;
    EXPECT_EQ(cpu->step(), 17);                // MOV DS,AX
    EXPECT_EQ(cpu->step(), 23 + 2);            // JMP far, plus the queue refill
}

TEST_F(Cpu80286PmTest, RealModeInterruptPastTheIdtLimitIsException8) {
    w16(8 * 4, 0x0000); w16(8 * 4 + 2, 0x0700);
    w16(0x0500, 0x007F); w16(0x0502, 0); w8(0x0504, 0);
    put(0x1000, {0x0F, 0x01, 0x1E, 0x00, 0x05,   // LIDT [0500h]: 32 vectors
                 0xCD, 0x20});                   // INT 20h
    cpu->cs = 0x0100; cpu->ip = 0; cpu->ds = 0; cpu->ss = 0; cpu->sp = 0x8000;
    cpu->step();
    cpu->step();
    EXPECT_EQ(cpu->cs, 0x0700);
    EXPECT_EQ(cpu->ip, 0);
}

TEST_F(Cpu80286PmTest, PmOnlyInstructionsAreUdInRealMode) {
    w16(6 * 4, 0x0000); w16(6 * 4 + 2, 0x0700);
    put(0x1000, {0x63, 0xD8});                   // ARPL
    cpu->cs = 0x0100; cpu->ip = 0; cpu->ss = 0; cpu->sp = 0x8000;
    cpu->step();
    EXPECT_EQ(cpu->cs, 0x0700);
}

TEST_F(Cpu80286PmTest, MovCr0IsInvalidOnA286) {
    w16(6 * 4, 0x0000); w16(6 * 4 + 2, 0x0700);
    put(0x1000, {0x0F, 0x20, 0xC0});
    cpu->cs = 0x0100; cpu->ip = 0; cpu->ss = 0; cpu->sp = 0x8000;
    cpu->step();
    EXPECT_EQ(cpu->cs, 0x0700);
}

}  // namespace
