// WD1003-WA2-compatible MFM hard disk controller. Ports 0x1F0-0x1F7 (command
// block) and 0x3F6 (control block), IRQ14. The floppy controller owns 0x3F7.
// Drive C: ships preloaded with the OS and has no swap UI, like a real AT.
//
// Simplifications (IBM_PCAT_REVIEW.md):
//  - READ, READ VERIFY and WRITE wait for each sector to pass under the head:
//    3600 RPM, 17 slots a track, 3:1 interleave unless FORMAT TRACK set one.
//  - DIAGNOSE completes at once. Buffered seeks take the time from a curve through
//    the ST4038's maximums, not a measured profile.
//  - Bad-block marks, WRITE LONG's stored ECC and FORMAT TRACK's interleave
//    live in memory only; the raw image has nowhere to keep them.
#ifndef IBMPCAT_WD1003_H
#define IBMPCAT_WD1003_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "../shared/pc/drive_sound.h"

namespace ibmpcat {

class Wd1003 {
public:
    struct Drive {
        std::vector<uint8_t> image;
        bool present = false;
        bool dirty = false;
        int cylinders = 0, heads = 0, sectors_per_track = 0;
        std::set<long> bad_sectors;  // sector index, set by FORMAT TRACK
        std::map<long, uint32_t> ecc_override;  // sector index -> ECC written by WRITE LONG
        // track index -> physical slot of each sector number (-1: not formatted), and slot count
        std::map<long, std::pair<std::vector<int>, int>> layout;

        static constexpr int kBytesPerSector = 512;
        long offset_for(int cyl, int head, int sector) const {
            return ((long(cyl) * heads + head) * sectors_per_track + (sector - 1)) * kBytesPerSector;
        }
        long capacity_sectors() const { return long(cylinders) * heads * sectors_per_track; }

        // Pages of `image` written since clear_dirty(); see dirty_ranges().
        static constexpr std::size_t kDirtyPageSize = 4096;
        std::vector<bool> dirty_page;
        void mark_dirty(long offset, std::size_t len) {
            dirty = true;
            std::size_t last = (std::size_t(offset) + len - 1) / kDirtyPageSize;
            for (std::size_t p = std::size_t(offset) / kDirtyPageSize; p <= last && p < dirty_page.size(); ++p)
                dirty_page[p] = true;
        }
    };
    Drive drives[2];  // 0 = C: (the only populated drive), 1 = D: (always absent)
    pc::DriveSoundLog sound;

    Wd1003() { reset(); }

    void reset();

    bool owns(uint16_t port) const;
    uint8_t in(uint16_t port);
    void out(uint16_t port, uint8_t v);
    // The data register (0x1F0) needs an atomic 16-bit path (cpu80286.h Bus::in16/out16).
    uint16_t data_in16();
    void data_out16(uint16_t v);

    void tick(uint64_t cpu_cycles);

    // IEN- gates IRQ14 without clearing it (WD1003-WA2 OEM manual, Fixed Disk register).
    bool irq_pending() const { return irq_pending_ && !nien_; }

    // Preloads the drive at setup, like a factory-formatted fixed disk.
    void mount(int drive, const uint8_t *data, std::size_t len);

    // True while a command is serviced (BSY) or a paced transfer is in flight.
    bool busy() const { return (status_ & ST_BSY) != 0 || xfer_active_; }

    // The 4 ECC bytes the WD11C00 records after a data field (WD11C00-13 data sheet).
    static uint32_t data_ecc(const uint8_t* data);

    // C:'s current image and whether it changed since mount(), so the front end
    // can persist writes across a power cycle.
    const std::vector<uint8_t> &image(int drive) const { return drives[drive & 1].image; }
    bool dirty(int drive) const { return drives[drive & 1].dirty; }
    void clear_dirty(int drive) {
        Drive &d = drives[drive & 1];
        d.dirty = false;
        std::fill(d.dirty_page.begin(), d.dirty_page.end(), false);
    }

    // Byte ranges changed since clear_dirty(), coalesced into runs, so the
    // front end stores only what was written.
    struct DirtyRange { uint32_t offset, length; };
    std::vector<DirtyRange> dirty_ranges(int drive) const {
        std::vector<DirtyRange> out;
        const Drive &d = drives[drive & 1];
        std::size_t i = 0, n = d.dirty_page.size();
        while (i < n) {
            if (!d.dirty_page[i]) { ++i; continue; }
            std::size_t j = i;
            while (j < n && d.dirty_page[j]) ++j;
            uint32_t off = uint32_t(i * Drive::kDirtyPageSize);
            uint32_t end = uint32_t(std::min<std::size_t>(j * Drive::kDirtyPageSize, d.image.size()));
            out.push_back({off, end - off});
            i = j;
        }
        return out;
    }

private:
    enum Status : uint8_t {
        ST_ERR = 0x01, ST_CORR = 0x04, ST_DRQ = 0x08, ST_DSC = 0x10, ST_DF = 0x20, ST_DRDY = 0x40,
        ST_BSY = 0x80,
    };
    enum Error : uint8_t { ERR_ABRT = 0x04, ERR_IDNF = 0x10, ERR_ECC = 0x40, ERR_BB = 0x80 };
    enum class PioMode { kNone, kReadDrain, kWriteFill };
    enum class Xfer { kNone, kRead, kVerify, kWrite, kFormat, kSeek };

    uint8_t error_ = 0;
    uint8_t precomp_ = 0;         // 0x1F1 when written: write precomp cylinder / 4
    uint16_t sector_count_ = 1;   // 0 means 256
    uint8_t sector_number_ = 1;
    uint8_t cyl_low_ = 0, cyl_high_ = 0;
    uint8_t drive_head_ = 0xA0;   // bits 5 and 7 always read 1
    uint8_t status_ = ST_DRDY | ST_DSC;
    bool nien_ = false;           // Device Control bit1: host interrupts disabled
    bool srst_prev_ = false;
    int selected_drive_ = 0;
    bool irq_pending_ = false;

    std::vector<uint8_t> pio_buffer_;
    std::size_t pio_pos_ = 0;
    PioMode pio_mode_ = PioMode::kNone;

    // Command in progress. xfer_active_ means the disk side is busy until due_.
    Xfer xfer_ = Xfer::kNone;
    bool xfer_active_ = false;
    long xfer_offset_ = 0;
    int sectors_left_ = 0;
    bool long_ = false;        // L bit: 4 ECC bytes follow each sector's data
    bool no_retry_ = false;    // T bit
    bool id_missing_ = false;  // the current sector's ID never comes round
    bool corrected_ = false;
    int retries_ = 0;
    double due_ = 0.0;
    uint64_t prev_cycles_ = 0;
    int head_cyl_ = 0;          // drive 0's heads
    uint8_t step_rate_ = 0x0D;  // rate field of the last RESTORE or SEEK, 6.5 ms after reset
    bool rate_set_ = false;

    static constexpr double kCpuHz = 8000000.0;
    static constexpr double kBytesPerSec = 625000.0;  // 5 Mbit/s (WD1003-WA2 OEM manual)
    static constexpr double kRevsPerSec = 60.0;       // 3600 RPM (Seagate ST4038 product manual)
    static constexpr double kCyclesPerRev = kCpuHz / kRevsPerSec;
    static constexpr int kFactoryInterleave = 3;  // IBM adapter default (retrocmp.de 5170 notes)
    static constexpr double kTrackToTrackMs = 11.0;  // slow-step, max (Seagate ST4038 product manual 4.3.2)

    // Drive 1 is never fitted, whatever is mounted; drive 0 answers even empty.
    bool selected_drive_absent() const { return (selected_drive_ & 1) != 0; }

    // Byte offset of the CHS in Sector Number, Cylinder Low/High and Drive/Head.
    long offset_for_current_registers() const;

    void run_command(uint8_t cmd);
    double seek_cycles(int from, int to) const;
    void log_seek(double start, int from, int to);
    void log_restore(double start, int from);
    double implied_seek(int cyl);
    void begin_seek(double cycles, int cyl);
    void complete(uint8_t error = 0);
    void do_diagnose();
    void begin_read(Xfer kind, uint8_t cmd);
    void begin_write(uint8_t cmd);
    void begin_format();
    void seek_sector();
    void sector_arrived();
    void sector_written();
    void finish_format();
    // Cycle at which the sector's data field has passed under the head.
    double sector_due(long sector, double now) const;
    // Index of the first bad-marked sector in [first, first + count), or -1.
    long first_bad(long first, int count) const;
    uint8_t pio_read_byte();
    void pio_write_byte(uint8_t v);
};

}  // namespace ibmpcat

#endif  // IBMPCAT_WD1003_H
