// GoogleTest suite for mcs48::Cpu (8035 subset). Reference: Grokking the
// MCS-48 System instruction set listing (dkong/docs/mcs48-isa.txt), restating
// the Intel MCS-48 Users Manual and 8048/8035 datasheet.

#include <gtest/gtest.h>

#include "cpu_mcs48.h"

#include <array>
#include <memory>

namespace {

constexpr uint8_t NOP = 0x00;

class Mcs48 : public ::testing::Test {
protected:
    std::array<uint8_t, 0x1000> prog{};   // 12-bit external program space
    std::array<uint8_t, 0x100> ext{};     // external data memory (MOVX)
    uint8_t p1_latch = 0, p2_latch = 0;
    bool t0_level = false, t1_level = false, int_low = false;
    std::unique_ptr<mcs48::Cpu> cpu;

    void SetUp() override {
        mcs48::Bus bus;
        bus.read_program = [this](uint16_t a) { return prog[a & 0xFFF]; };
        bus.read_data_ext = [this](uint8_t a) { return ext[a]; };
        bus.write_data_ext = [this](uint8_t a, uint8_t v) { ext[a] = v; };
        bus.read_p1 = [this] { return p1_latch; };
        bus.write_p1 = [this](uint8_t v) { p1_latch = v; };
        bus.read_p2 = [this] { return p2_latch; };
        bus.write_p2 = [this](uint8_t v) { p2_latch = v; };
        bus.read_t0 = [this] { return t0_level; };
        bus.read_t1 = [this] { return t1_level; };
        bus.int_asserted = [this] { return int_low; };
        cpu = std::make_unique<mcs48::Cpu>(bus);
        cpu->reset();
    }

    // Loads code at PC=0 and runs exactly `n` step()s.
    void run(std::initializer_list<uint8_t> code, int n = 1) {
        uint16_t addr = 0;
        for (uint8_t b : code) prog[addr++] = b;
        cpu->pc = 0;
        for (int i = 0; i < n; i++) cpu->step();
    }
};

TEST_F(Mcs48, ResetPcZero) {
    cpu->pc = 0x123;
    cpu->reset();
    EXPECT_EQ(cpu->pc, 0);
    EXPECT_EQ(cpu->sp, 0);
    EXPECT_FALSE(cpu->cy);
}

TEST_F(Mcs48, AddRegisterSetsCarryAndAux) {
    cpu->a = 0xFF;
    cpu->ram[0] = 0x01;             // R0 = 1 (bank 0)
    run({0x68});                    // ADD A,R0
    EXPECT_EQ(cpu->a, 0x00);
    EXPECT_TRUE(cpu->cy);
    EXPECT_TRUE(cpu->ac);
}

TEST_F(Mcs48, AddImmediateTwoCycles) {
    cpu->a = 0x10;
    prog[0] = 0x03; prog[1] = 0x05;  // ADD A,#5
    cpu->pc = 0;
    int c = cpu->step();
    EXPECT_EQ(cpu->a, 0x15);
    EXPECT_EQ(c, 2);
}

TEST_F(Mcs48, MovRegisterRoundTrip) {
    cpu->a = 0x42;
    run({0xA8});                    // MOV R0,A
    EXPECT_EQ(cpu->ram[0], 0x42);
    cpu->a = 0;
    run({0xF8});                    // MOV A,R0
    EXPECT_EQ(cpu->a, 0x42);
}

TEST_F(Mcs48, MovImmediateToRegister) {
    run({0xB9, 0x7E});               // MOV R1,#0x7E
    EXPECT_EQ(cpu->ram[1], 0x7E);
}

TEST_F(Mcs48, CallPushesReturnAddressAndPswToRamStack) {
    cpu->cy = true;
    cpu->ac = true;
    prog[0] = 0x14; prog[1] = 0x20;   // CALL 0x020 (page 0)
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x020);
    EXPECT_EQ(cpu->sp, 1);
    EXPECT_EQ(cpu->ram[8], 0x02);     // return PCL (CALL is 2 bytes, from PC=0)
    EXPECT_EQ(cpu->ram[9] & 0xF, 0);  // return PCH (page 0)
    EXPECT_EQ((cpu->ram[9] >> 7) & 1, 1);  // saved CY
    EXPECT_EQ((cpu->ram[9] >> 6) & 1, 1);  // saved AC
}

TEST_F(Mcs48, RetPopsReturnAddressButNotPsw) {
    cpu->ram[8] = 0x05;
    cpu->ram[9] = 0xF0;               // PC8-11=0, PSW4-7 all set
    cpu->sp = 1;
    cpu->cy = false;
    run({0x83});                      // RET
    EXPECT_EQ(cpu->pc, 0x05);
    EXPECT_EQ(cpu->sp, 0);
    EXPECT_FALSE(cpu->cy);            // RET does not restore PSW
}

TEST_F(Mcs48, RetrRestoresPswAndClearsInService) {
    cpu->ram[8] = 0x06;
    cpu->ram[9] = 0x80;                // CY set, rest clear
    cpu->sp = 1;
    cpu->in_service = true;
    run({0x93});                       // RETR
    EXPECT_EQ(cpu->pc, 0x06);
    EXPECT_TRUE(cpu->cy);
    EXPECT_FALSE(cpu->in_service);
}

TEST_F(Mcs48, DjnzLoopsUntilRegisterIsZero) {
    cpu->ram[0] = 3;                   // R0 = 3
    prog[0] = 0xE8; prog[1] = 0x00;    // DJNZ R0, 0x00
    cpu->pc = 0;
    cpu->step();                       // R0=2, jump back to 0
    EXPECT_EQ(cpu->ram[0], 2);
    EXPECT_EQ(cpu->pc, 0x00);
    cpu->step();                       // R0=1, jump back
    cpu->step();                       // R0=0, fall through
    EXPECT_EQ(cpu->ram[0], 0);
    EXPECT_EQ(cpu->pc, 0x02);
}

TEST_F(Mcs48, TimerIncrementsAfter32MachineCycles) {
    run({0x55});                       // STRT T (1 cycle)
    cpu->t = 0;
    for (int i = 0; i < 31; i++) run({NOP});
    EXPECT_EQ(cpu->t, 0);
    run({NOP});                        // 32nd cycle since STRT T
    EXPECT_EQ(cpu->t, 1);
}

TEST_F(Mcs48, TimerOverflowSetsTf) {
    run({0x55});                       // STRT T
    cpu->t = 0xFF;
    for (int i = 0; i < 32; i++) run({NOP});
    EXPECT_EQ(cpu->t, 0x00);
    EXPECT_TRUE(cpu->tf);
}

TEST_F(Mcs48, JtfClearsFlagRegardlessOfBranch) {
    cpu->tf = true;
    prog[0] = 0x16; prog[1] = 0x10;    // JTF 0x10
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x10);
    EXPECT_FALSE(cpu->tf);
}

TEST_F(Mcs48, CounterCountsT1FallingEdges) {
    run({0x45});                       // STRT CNT
    cpu->t = 0;
    t1_level = true;
    run({NOP});                        // sample high, no edge yet
    t1_level = false;
    run({NOP});                        // high-to-low edge
    EXPECT_EQ(cpu->t, 1);
}

TEST_F(Mcs48, ExternalInterruptVectorsToLoc3WhenEnabled) {
    cpu->int_enabled = true;
    int_low = true;
    prog[0] = NOP;
    cpu->pc = 0;
    int c = cpu->step();
    EXPECT_EQ(cpu->pc, 3);
    EXPECT_TRUE(cpu->in_service);
    EXPECT_EQ(c, 2);
    EXPECT_EQ(cpu->ram[8], 0x00);       // interrupted PC (0) saved
}

TEST_F(Mcs48, ExternalInterruptIgnoredWhenDisabled) {
    cpu->int_enabled = false;
    int_low = true;
    run({NOP});
    EXPECT_EQ(cpu->pc, 1);
    EXPECT_FALSE(cpu->in_service);
}

TEST_F(Mcs48, TimerInterruptVectorsToLoc7AndClearsTf) {
    cpu->tcnti_enabled = true;
    cpu->tf = true;
    prog[0] = NOP;
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 7);
    EXPECT_TRUE(cpu->in_service);
    EXPECT_FALSE(cpu->tf);
}

TEST_F(Mcs48, InterruptStaysLockedOutUntilRetr) {
    cpu->int_enabled = true;
    int_low = true;
    run({NOP});                         // takes the interrupt, PC=3
    EXPECT_TRUE(cpu->in_service);
    prog[3] = 0x93;                     // RETR at the vector
    cpu->step();
    EXPECT_FALSE(cpu->in_service);
}

TEST_F(Mcs48, MovpReadsCurrentPageProgramByte) {
    prog[0] = 0xA3;                     // MOVP A,@A
    prog[0x10] = 0x99;
    cpu->a = 0x10;
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->a, 0x99);
    EXPECT_EQ(cpu->pc, 1);              // PC restored (page 0, +1 past opcode)
}

TEST_F(Mcs48, Movp3ReadsPage3RegardlessOfCurrentPage) {
    prog[0x300 + 0x20] = 0x77;
    cpu->a = 0x20;
    run({0xE3});                        // MOVP3 A,@A
    EXPECT_EQ(cpu->a, 0x77);
}

TEST_F(Mcs48, JmppJumpsWithinCurrentPage) {
    prog[0] = 0xB3;                     // JMPP @A
    prog[0x40] = 0x50;                  // target low byte, same page
    cpu->a = 0x40;
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x50);
}

TEST_F(Mcs48, SelRegisterBankSwitchesR0R7Mapping) {
    run({0xD5});                        // SEL RB1
    EXPECT_TRUE(cpu->bs);
    cpu->a = 0x55;
    run({0xA8});                        // MOV R0,A -> RAM[24]
    EXPECT_EQ(cpu->ram[24], 0x55);
    EXPECT_EQ(cpu->ram[0], 0x00);
    run({0xC5});                        // SEL RB0
    EXPECT_FALSE(cpu->bs);
}

TEST_F(Mcs48, SelMb1AffectsOnlyNextJmpOrCall) {
    prog[0] = 0xF5;                     // SEL MB1
    prog[1] = 0x04; prog[2] = 0x10;     // JMP 0x010 (page 0 field)
    cpu->pc = 0;
    cpu->step();                        // SEL MB1
    EXPECT_TRUE(cpu->mb_ff);
    cpu->step();                        // JMP
    EXPECT_EQ(cpu->pc, 0x810);          // bit 11 forced by DBF
}

TEST_F(Mcs48, LogicalOpsLeaveCarryUntouched) {
    cpu->cy = true;
    cpu->a = 0xF0;
    cpu->ram[0] = 0x0F;
    run({0x58});                        // ANL A,R0
    EXPECT_EQ(cpu->a, 0x00);
    EXPECT_TRUE(cpu->cy);               // ANL never clears carry on MCS-48
}

TEST_F(Mcs48, SwapAExchangesNibbles) {
    cpu->a = 0xA5;
    run({0x47});                        // SWAP A
    EXPECT_EQ(cpu->a, 0x5A);
}

TEST_F(Mcs48, DaaAdjustsToBcd) {
    cpu->a = 0x0B;                      // low nibble > 9
    cpu->ac = false;
    cpu->cy = false;
    run({0x57});                        // DA A
    EXPECT_EQ(cpu->a, 0x11);
    EXPECT_FALSE(cpu->cy);
}

TEST_F(Mcs48, RlcAndRrcRotateThroughCarry) {
    cpu->a = 0x80;
    cpu->cy = false;
    run({0xF7});                        // RLC A
    EXPECT_EQ(cpu->a, 0x00);
    EXPECT_TRUE(cpu->cy);
    run({0x67});                        // RRC A
    EXPECT_EQ(cpu->a, 0x80);
    EXPECT_FALSE(cpu->cy);
}

TEST_F(Mcs48, XchdSwapsOnlyLowNibbles) {
    cpu->a = 0x3A;
    cpu->ram[0] = 0x05;                 // R0 points at RAM address 5
    cpu->ram[5] = 0x7C;
    run({0x30});                        // XCHD A,@R0
    EXPECT_EQ(cpu->a, 0x3C);
    EXPECT_EQ(cpu->ram[5], 0x7A);
}

TEST_F(Mcs48, MovxReadsAndWritesExternalDataMemory) {
    cpu->ram[0] = 0x10;                 // R0 = external address 0x10
    cpu->a = 0x99;
    run({0x90});                        // MOVX @R0,A
    EXPECT_EQ(ext[0x10], 0x99);
    cpu->a = 0;
    run({0x80});                        // MOVX A,@R0
    EXPECT_EQ(cpu->a, 0x99);
}

TEST_F(Mcs48, OutlAndInPortsRoundTrip) {
    cpu->a = 0x3C;
    run({0x39});                        // OUTL P1,A
    EXPECT_EQ(p1_latch, 0x3C);
    cpu->a = 0;
    run({0x09});                        // IN A,P1
    EXPECT_EQ(cpu->a, 0x3C);
}

TEST_F(Mcs48, JbbTestsAccumulatorBit) {
    cpu->a = 0x04;                      // bit 2 set
    prog[0] = 0x52; prog[1] = 0x20;     // JB2 0x20
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x20);
}

TEST_F(Mcs48, JcAndJncBranchOnCarry) {
    cpu->cy = true;
    prog[0] = 0xF6; prog[1] = 0x30;     // JC 0x30
    cpu->pc = 0;
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x30);

    cpu->cy = false;
    prog[0x30] = 0xE6; prog[0x31] = 0x40; // JNC 0x40
    cpu->step();
    EXPECT_EQ(cpu->pc, 0x40);
}

} // namespace
