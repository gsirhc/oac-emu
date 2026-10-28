#include <gtest/gtest.h>

#include "machine.h"

#include <algorithm>
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
    std::vector<std::pair<uint8_t, uint8_t>> cmos;  // applied before POST
    uint32_t base_kb = 640;

    void boot(uint32_t ext_kb, const std::vector<uint8_t>* hdd = nullptr, bool vga = false,
              const std::vector<uint8_t>* fd = nullptr) {
        std::ifstream f(BIOS_PATH, std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ASSERT_EQ(rom.size(), 65536u) << "make -C ibmpc-at bios";
        m = std::make_unique<Machine>(ext_kb, base_kb);
        m->chipset.load_rom(0xF0000, rom.data(), rom.size());
        if (hdd || vga) {
            // POST halts at "No VGABIOS ROM" before it sets up the disks.
            std::ifstream v(EGABIOS_PATH, std::ios::binary);
            std::vector<uint8_t> vbios((std::istreambuf_iterator<char>(v)), std::istreambuf_iterator<char>());
            ASSERT_FALSE(vbios.empty()) << "make -C ibmpc-at egabios";
            m->chipset.load_rom(0xC0000, vbios.data(), vbios.size());
            if (hdd) m->chipset.hdd.mount(0, hdd->data(), hdd->size());
        }
        if (fd) m->chipset.fdc.mount(0, fd->data(), fd->size());
        // with no disk, a fixed disk type in CMOS keeps POST retrying for 60 s
        if (!hdd) set_cmos(0x12, 0x00);
        for (auto [reg, v] : cmos) set_cmos(reg, v);
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
        for (int i = 0; i < 40000 && !m->cpu.halted; ++i) m->run_cycles(1000);
        ASSERT_TRUE(m->cpu.halted);
    }

    // INT 13h AH=02h (or ah), one sector to 0:0800. Returns AX; CF lands in cf.
    uint16_t read_floppy(int drive, int cyl, int head, int sector, bool* cf, uint8_t ah = 0x02) {
        for (int i = 0; i < 512; ++i) m->chipset.mem[kSrc + i] = 0xEE;
        run_stub({0xB8, 0x01, ah,                                     // MOV AX
                  0xB9, uint8_t(sector), uint8_t(cyl),                // MOV CX
                  0xBA, uint8_t(drive), uint8_t(head),                // MOV DX
                  0xBB, 0x00, 0x08,                                   // MOV BX,0800h
                  0xCD, 0x13,                                         // INT 13h
                  0xA3, 0x00, 0x06,                                   // MOV [0600h],AX
                  0x9C, 0x8F, 0x06, 0x02, 0x06,                       // PUSHF ; POP [0602h]
                  0xFA, 0xF4});
        *cf = (word(kOut + 2) & 1) != 0;
        return word(kOut);
    }

    void set_cmos(uint8_t reg, uint8_t v) {
        auto& c = m->chipset.cmos;
        c.poke(reg, v);
        uint16_t sum = 0;
        for (uint16_t r = 0x10; r <= 0x2D; ++r) sum = uint16_t(sum + c.peek(uint8_t(r)));
        c.poke(0x2E, uint8_t(sum >> 8));
        c.poke(0x2F, uint8_t(sum & 0xFF));
    }

    struct Regs { uint16_t ax, cx, dx; bool cf; uint16_t es, di; };
    // INT 13h with ES:BX = 0:0800
    Regs int13(uint16_t ax, uint16_t cx = 0, uint16_t dx = 0x0080) {
        run_stub({0xB8, uint8_t(ax), uint8_t(ax >> 8),
                  0xB9, uint8_t(cx), uint8_t(cx >> 8),
                  0xBA, uint8_t(dx), uint8_t(dx >> 8),
                  0xBB, 0x00, 0x08,                    // MOV BX,0800h
                  0xCD, 0x13,
                  0xA3, 0x00, 0x06,                    // MOV [0600h],AX
                  0x89, 0x0E, 0x02, 0x06,              // MOV [0602h],CX
                  0x89, 0x16, 0x04, 0x06,              // MOV [0604h],DX
                  0x9C, 0x8F, 0x06, 0x06, 0x06,        // PUSHF ; POP [0606h]
                  0x8C, 0x06, 0x08, 0x06,              // MOV [0608h],ES
                  0x89, 0x3E, 0x0A, 0x06,              // MOV [060Ah],DI
                  0xFA, 0xF4});
        return {word(kOut), word(kOut + 2), word(kOut + 4), (word(kOut + 6) & 1) != 0, word(kOut + 8), word(kOut + 10)};
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

TEST_F(BiosTest, Int15h89hReturnsInProtectedModeThroughLmsw) {
    boot(1024);
    for (int i = 0; i < 0x40; ++i) m->chipset.mem[kGdt + i] = 0;
    desc(kGdt + 0x08, kGdt, 0x3F);
    desc(kGdt + 0x10, 0x0900, 0x7FF);
    for (uint32_t s : {0x18u, 0x20u, 0x28u, 0x30u}) desc(kGdt + s, 0, 0xFFFF);
    m->chipset.mem[kGdt + 0x30 + 5] = 0x9B;
    run_stub({0xBE, 0x00, 0x07,                // MOV SI,0700h (ES:SI = GDT)
              0xBB, 0x28, 0x20,                // MOV BX,2028h (PIC bases)
              0xB4, 0x89, 0xCD, 0x15,          // MOV AH,89h ; INT 15h
              0xFA, 0xF4});                    // CLI ; HLT
    EXPECT_EQ(m->cpu.msw() & 1, 1);
    EXPECT_EQ(m->cpu.cs, 0x30);
    EXPECT_EQ(m->cpu.ss, 0x28);
    EXPECT_FALSE(m->cpu.shutdown());
}

TEST_F(BiosTest, Int15h83hPostsItsFlagFromTheRtcInterrupt) {
    boot(0);
    m->chipset.mem[kOut] = 0;
    run_stub({0xBB, 0x00, 0x06,                // MOV BX,0600h (ES:BX = flag)
              0xB9, 0x00, 0x00,                // MOV CX,0
              0xBA, 0xD0, 0x07,                // MOV DX,2000 us
              0xB8, 0x00, 0x83, 0xCD, 0x15,    // MOV AX,8300h ; INT 15h
              0xFB,                            // STI
              0xF6, 0x06, 0x00, 0x06, 0x80,    // TEST byte [0600h],80h
              0x74, 0xF9,                      // JZ back to the TEST
              0xFA, 0xF4});                    // CLI ; HLT
    EXPECT_EQ(m->chipset.mem[kOut], 0x80);
}

// E801h and E820h came after the AT; its BIOS answers 86h, unsupported.
TEST_F(BiosTest, Int15hE801IsUnsupported) {
    boot(2048);
    run_stub({0xB8, 0x01, 0xE8, 0xCD, 0x15,    // MOV AX,E801h ; INT 15h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0x9C, 0x8F, 0x06, 0x06, 0x06,    // PUSHF ; POP [0606h]
              0xFA, 0xF4});
    EXPECT_EQ(word(kOut) >> 8, 0x86);
    EXPECT_EQ(word(kOut + 6) & 1, 1);
}

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

TEST_F(BiosTest, Int13h03hWritesThroughThePacedController) {
    std::vector<uint8_t> img(std::size_t(733) * 5 * 17 * 512, 0);
    boot(0, &img);
    for (int i = 0; i < 1024; ++i) m->chipset.mem[kSrc + i] = uint8_t(i * 3 + 1);
    run_stub({0xB8, 0x02, 0x03,                // MOV AX,0302h (write 2 sectors)
              0xB9, 0x02, 0x00,                // MOV CX,0002h (cylinder 0, sector 2)
              0xBA, 0x80, 0x00,                // MOV DX,0080h (head 0, drive C:)
              0xBB, 0x00, 0x08,                // MOV BX,0800h
              0xCD, 0x13,                      // INT 13h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0x9C, 0x8F, 0x06, 0x02, 0x06,    // PUSHF ; POP [0602h]
              0xFA, 0xF4});
    EXPECT_EQ(word(kOut), 0x0002) << "AH status 0, AL sectors written";
    EXPECT_EQ(word(kOut + 2) & 1, 0) << "CF clear";
    const auto& d = m->chipset.hdd.drives[0];
    for (int i = 0; i < 1024; ++i) ASSERT_EQ(d.image[512 + std::size_t(i)], uint8_t(i * 3 + 1)) << i;
}

// A boot sector that stores its tag at 0:0600 and halts.
void BootSector(std::vector<uint8_t>& img, char tag) {
    const uint8_t code[] = {0x31, 0xC0, 0x8E, 0xD8, 0xC6, 0x06, 0x00, 0x06, uint8_t(tag), 0xFA, 0xF4};
    std::copy(std::begin(code), std::end(code), img.begin());
    img[510] = 0x55;
    img[511] = 0xAA;
}

TEST_F(BiosTest, BootsFromDriveABeforeTheHardDisk) {
    std::vector<uint8_t> hdd(std::size_t(733) * 5 * 17 * 512, 0), fd(std::size_t(80) * 2 * 15 * 512, 0);
    BootSector(hdd, 'C');
    BootSector(fd, 'A');
    boot(0, &hdd, false, &fd);
    EXPECT_EQ(m->chipset.mem[kOut], 'A');
}

TEST_F(BiosTest, BootsTheHardDiskWithDriveAEmpty) {
    std::vector<uint8_t> hdd(std::size_t(733) * 5 * 17 * 512, 0);
    BootSector(hdd, 'C');
    boot(0, &hdd);
    EXPECT_EQ(m->chipset.mem[kOut], 'C');
}

// Each sector starts with its index on the image.
std::vector<uint8_t> HardDisk() { return std::vector<uint8_t>(std::size_t(733) * 5 * 17 * 512, 0); }

TEST_F(BiosTest, PostTakesBaseMemoryFromCmos) {
    boot(0);
    EXPECT_EQ(word(0x413), 640) << "no 1KB reserved at the top";
    base_kb = 512;
    boot(0);
    EXPECT_EQ(word(0x413), 512);
}

TEST_F(BiosTest, Int1EhPointsAtIbmsDiskBase) {
    boot(0);
    EXPECT_EQ(word(0x1E * 4), 0xEFC7);
    EXPECT_EQ(word(0x1E * 4 + 2), 0xF000);
    const uint8_t disk_base[] = {0xDF, 0x02, 0x25, 0x02, 0x0F, 0x1B, 0xFF, 0x54, 0xF6, 0x0F, 0x08};
    for (unsigned i = 0; i < sizeof disk_base; ++i) EXPECT_EQ(m->chipset.mem[0xFEFC7 + i], disk_base[i]) << i;
}

TEST_F(BiosTest, Int13h08hReturnsEachDrivesOwnMediaTable) {
    boot(0);
    Regs a = int13(0x0800, 0, 0x0000);
    ASSERT_FALSE(a.cf);
    EXPECT_EQ(a.cx, 0x4F0F);
    ASSERT_EQ(a.es, 0xF000);
    uint32_t t = 0xF0000 + a.di;
    EXPECT_EQ(m->chipset.mem[t], 0xDF);
    EXPECT_EQ(m->chipset.mem[t + 4], 15) << "1.2MB in a 1.2MB drive";
    EXPECT_EQ(m->chipset.mem[t + 11], 79);
    EXPECT_EQ(m->chipset.mem[t + 12], 0x00) << "500 kb/s";

    Regs b = int13(0x0800, 0, 0x0001);
    ASSERT_FALSE(b.cf);
    EXPECT_EQ(b.cx, 0x2709);
    t = 0xF0000 + b.di;
    EXPECT_EQ(m->chipset.mem[t + 4], 9) << "360KB in a 360KB drive";
    EXPECT_EQ(m->chipset.mem[t + 5], 0x2A);
    EXPECT_EQ(m->chipset.mem[t + 11], 39);
    EXPECT_EQ(m->chipset.mem[t + 12], 0x80) << "250 kb/s";
}

TEST_F(BiosTest, Int13h18hPointsAtTheTableForTheFormat) {
    std::vector<uint8_t> fd(std::size_t(80) * 2 * 15 * 512, 0);
    boot(0, nullptr, false, &fd);
    Regs hd = int13(0x1800, 0x4F0F, 0x0000);
    ASSERT_FALSE(hd.cf);
    EXPECT_EQ(hd.es, 0xF000);
    EXPECT_EQ(m->chipset.mem[0xF0000 + hd.di + 4], 15);
    EXPECT_EQ(m->chipset.mem[0xF0000 + hd.di + 7], 0x54);
    Regs dd = int13(0x1800, 0x2709, 0x0000);
    ASSERT_FALSE(dd.cf);
    EXPECT_EQ(m->chipset.mem[0xF0000 + dd.di + 4], 9) << "360KB in a 1.2MB drive";
    EXPECT_EQ(m->chipset.mem[0xF0000 + dd.di + 7], 0x50);
    EXPECT_EQ(m->chipset.mem[0xF0000 + dd.di + 12], 0x40) << "300 kb/s";
}

TEST_F(BiosTest, Int41hPointsAtType8InTheRomTable) {
    auto img = HardDisk();
    boot(0, &img);
    EXPECT_EQ(word(0x41 * 4), 0xE471);
    EXPECT_EQ(word(0x41 * 4 + 2), 0xF000);
    EXPECT_EQ(m->chipset.mem[0x475], 1) << "one fixed disk";
    EXPECT_EQ(word(0xFE471), 733);
    EXPECT_EQ(m->chipset.mem[0xFE473], 5);
    EXPECT_EQ(m->chipset.mem[0xFE47F], 17);
}

TEST_F(BiosTest, AnExtendedDriveTypeComesFromCmos19h) {
    auto img = HardDisk();
    cmos = {{0x12, 0xF0}, {0x19, 20}};
    boot(0, &img);
    EXPECT_EQ(word(0x41 * 4), 0xE531);
    EXPECT_EQ(word(0xFE531 + 5), 300) << "type 20 precompensates from cylinder 300";
    EXPECT_EQ(m->chipset.mem[0x475], 1);
}

TEST_F(BiosTest, Int13h08hLeavesTheLastCylinderForDiagnostics) {
    auto img = HardDisk();
    boot(0, &img);
    Regs r = int13(0x0800);
    EXPECT_FALSE(r.cf);
    EXPECT_EQ(r.ax, 0);
    EXPECT_EQ(r.cx, 0xDB91) << "cylinder 731, 17 sectors";
    EXPECT_EQ(r.dx, 0x0401) << "head 4, one drive";
}

TEST_F(BiosTest, Int13h15hCountsAllButOneCylinder) {
    auto img = HardDisk();
    boot(0, &img);
    Regs r = int13(0x1500);
    EXPECT_FALSE(r.cf);
    EXPECT_EQ(r.ax >> 8, 3) << "fixed disk";
    EXPECT_EQ((uint32_t(r.cx) << 16) | r.dx, 732u * 5 * 17);
    r = int13(0x1500, 0, 0x0081);
    EXPECT_EQ(r.ax, 0) << "no drive D:";
}

TEST_F(BiosTest, Int13h01hReturnsTheLastStatusInAl) {
    auto img = HardDisk();
    boot(0, &img);
    Regs r = int13(0x0201, 0xE8C1);  // cylinder 1000
    EXPECT_TRUE(r.cf);
    EXPECT_EQ(r.ax >> 8, 0x04) << "record not found";
    r = int13(0x0100);
    EXPECT_FALSE(r.cf);
    EXPECT_EQ(r.ax, 0x0004);
    EXPECT_EQ(m->chipset.mem[0x474], 0);
}

TEST_F(BiosTest, Int13hReadLongReturnsTheEccBytes) {
    auto img = HardDisk();
    for (int i = 0; i < 512; ++i) img[17 * 512 + i] = uint8_t(i);
    boot(0, &img);
    Regs r = int13(0x0A01, 0x0001, 0x0180);  // head 1, sector 1
    EXPECT_FALSE(r.cf);
    for (int i = 0; i < 512; ++i) ASSERT_EQ(m->chipset.mem[kSrc + i], uint8_t(i)) << i;
    uint32_t ecc = uint32_t(m->chipset.mem[kSrc + 512]) << 24 | uint32_t(m->chipset.mem[kSrc + 513]) << 16 |
                   uint32_t(m->chipset.mem[kSrc + 514]) << 8 | m->chipset.mem[kSrc + 515];
    EXPECT_EQ(ecc, ibmpcat::Wd1003::data_ecc(&img[17 * 512]));
}

TEST_F(BiosTest, Int15hC1hIsUnsupportedWithoutAnEbda) {
    boot(0);
    run_stub({0xB4, 0xC1, 0xCD, 0x15,          // MOV AH,C1h ; INT 15h
              0xA3, 0x00, 0x06,                // MOV [0600h],AX
              0x9C, 0x8F, 0x06, 0x02, 0x06,    // PUSHF ; POP [0602h]
              0xFA, 0xF4});
    EXPECT_EQ(word(kOut) >> 8, 0x86);
    EXPECT_EQ(word(kOut + 2) & 1, 1);
    EXPECT_EQ(word(0x40E), 0);
}

std::vector<uint8_t> FloppyImage(int cyl, int spt) {
    std::vector<uint8_t> img(std::size_t(cyl) * 2 * spt * 512, 0);
    for (std::size_t i = 0; i < img.size(); i += 512) {
        img[i] = uint8_t(i / 512);
        img[i + 1] = uint8_t((i / 512) >> 8);
    }
    return img;
}

class BiosFloppyTest : public BiosTest {
protected:
    std::vector<uint8_t> hd = FloppyImage(80, 15), dd = FloppyImage(40, 9);
    void SetUp() override { boot(0, nullptr, true); }
    int sector_read() const { return word(kSrc); }
};

TEST_F(BiosFloppyTest, A12MDiskReadsInDriveA) {
    m->chipset.fdc.mount(0, hd.data(), hd.size());
    bool cf;
    EXPECT_EQ(read_floppy(0, 5, 1, 3, &cf), 0x0001);
    EXPECT_FALSE(cf);
    EXPECT_EQ(sector_read(), (5 * 2 + 1) * 15 + 2);
    EXPECT_EQ(m->chipset.fdc.data_rate(), 0);
}

TEST_F(BiosFloppyTest, A360KDiskInDriveAIsDoubleSteppedAt300Kbps) {
    m->chipset.fdc.mount(0, dd.data(), dd.size());
    bool cf;
    EXPECT_EQ(read_floppy(0, 20, 1, 4, &cf), 0x0001);
    EXPECT_FALSE(cf);
    EXPECT_EQ(sector_read(), (20 * 2 + 1) * 9 + 3);
    EXPECT_EQ(m->chipset.fdc.data_rate(), 1);
    EXPECT_EQ(m->chipset.fdc.drives[0].current_cylinder, 40);
}

TEST_F(BiosFloppyTest, A360KDiskInDriveBReadsAt250Kbps) {
    m->chipset.fdc.mount(1, dd.data(), dd.size());
    bool cf;
    EXPECT_EQ(read_floppy(1, 30, 0, 9, &cf), 0x0001);
    EXPECT_FALSE(cf);
    EXPECT_EQ(sector_read(), 30 * 2 * 9 + 8);
    EXPECT_EQ(m->chipset.fdc.data_rate(), 2);
    EXPECT_EQ(m->chipset.fdc.drives[1].current_cylinder, 30);
}

TEST_F(BiosFloppyTest, SwappingA360KDiskForA12MDiskSensesTheNewMedia) {
    m->chipset.fdc.mount(0, dd.data(), dd.size());
    bool cf;
    read_floppy(0, 1, 0, 1, &cf);
    ASSERT_FALSE(cf);
    m->chipset.fdc.mount(0, hd.data(), hd.size());
    EXPECT_EQ(read_floppy(0, 40, 0, 1, &cf), 0x0001);
    EXPECT_FALSE(cf);
    EXPECT_EQ(sector_read(), 40 * 2 * 15);
    EXPECT_EQ(m->chipset.fdc.data_rate(), 0);
}

TEST_F(BiosFloppyTest, A12MDiskInThe360KDriveIsAnUnknownMediaType) {
    m->chipset.fdc.mount(1, hd.data(), hd.size());
    bool cf;
    EXPECT_EQ(read_floppy(1, 0, 0, 1, &cf) >> 8, 0x0C);
    EXPECT_TRUE(cf);
}

TEST_F(BiosFloppyTest, VerifyReadsTheSectorWithoutTouchingMemory) {
    m->chipset.fdc.mount(0, hd.data(), hd.size());
    bool cf;
    EXPECT_EQ(read_floppy(0, 12, 1, 15, &cf, 0x04), 0x0001);
    EXPECT_FALSE(cf);
    EXPECT_EQ(m->chipset.mem[kSrc], 0xEE);
    EXPECT_EQ(m->chipset.fdc.drives[0].current_cylinder, 12);
}

TEST_F(BiosFloppyTest, VerifyOfAMissingSectorFails) {
    m->chipset.fdc.mount(0, hd.data(), hd.size());
    bool cf;
    EXPECT_EQ(read_floppy(0, 3, 0, 16, &cf, 0x04) >> 8, 0x04);
    EXPECT_TRUE(cf);
}

TEST_F(BiosFloppyTest, AnEmptyDriveIsNotReady) {
    bool cf;
    EXPECT_EQ(read_floppy(0, 0, 0, 1, &cf) >> 8, 0x80);
    EXPECT_TRUE(cf);
}

}  // namespace
