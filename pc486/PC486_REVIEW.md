# 486 DX2-66 Gaming PC: review

This machine is not a recreation of a specific historical model, the way
`ibmpc-at` recreates a genuine IBM 5170. It's a 1993-94 gamer's 486 as a
well-appointed enthusiast might have assembled or bought it: a real Intel
80486 DX2-66 (33MHz bus, clock-doubled to 66MHz internally, on-die FPU),
32MB RAM, a 256MB Western Digital Caviar AC2250 IDE hard disk (1010 cyl x
9 head x 55 sec, §3.1), a single 3.5" 1.44MB floppy, a 2x ATAPI CD-ROM, VGA
with VESA BIOS Extensions, a PS/2 mouse, and a Sound Blaster 16.

The core covers real-mode and protected-mode execution, paging, task
switching and the FPU (§6), VGA mode 13h and the VESA BIOS Extensions (§7),
and the PS/2 mouse and Sound Blaster 16 (§10-§12). The base disk image is
FreeDOS. The shipped image is FreeDOS's trimmed Base set, not a game bundle:
a barebones DOS environment with no games, apps, or dev tools, meant as a
starting point for installing your own OS (§19.5). The core was exercised
with BOOM, a DJGPP/CWSDPMI DOS Doom source port (§9, §13), and the image no
longer includes it. A genuine MS-DOS 6.22 install runs end to end (§20), and
so does a real commercial DOS game, DOOM 1.2, on top of it (§21).

Each section states a design decision as fact, why it matters, and what the
code does about it, with citations to source material (Intel manuals,
ATA/ATAPI/SCSI-3 MMC specs, period drive datasheets) wherever a specific
quirk is preserved rather than guessed at. The structure follows
`ibmpc-at/IBM_PCAT_REVIEW.md`.

Open parity gaps and test gaps are tracked as a checklist in
[`PC486_PARITY.md`](PC486_PARITY.md).

## 1. ISA-standard chipset devices reused from ibmpc-at

A real 486 gaming PC of this era used the same programming-model ISA
parts as the AT, 8259 PIC, 8253/8254 PIT, 8237 DMA, 8042 keyboard
controller, CMOS RTC, almost always integrated into a Super I/O chip by
1993-94 rather than discrete parts, but software-identical to the AT's
own discrete implementation. `pic8259`, `pit8253`, `dma8237`, `i8042`,
`cmos_rtc`, and `pcspeaker` are adapted from `ibmpc-at` (namespace/doc
references only, no behavioral changes) on exactly that basis: this is
genuine period hardware-compatibility reuse, not a shortcut.

## 2. Floppy: single 3.5" 1.44MB bay

`fdc765` is adapted from ibmpc-at's two-drive (1.2MB/360KB, 5.25")
implementation to this machine's single 3.5" 1.44MB drive (80 cyl/2
head/18 sec/track, 500 kbit/s, 300 RPM), matching the user's explicit
"1990s period-correct 3.5 inch floppy" requirement, singular, no second
drive. The drive also accepts genuine double-density 720KB 3.5" media (80
cyl/2 head/9 sec/track, 250 kbit/s), the same real "one physical drive,
two densities" fact ibmpc-at's A: already modeled for 1.2MB/360KB.

## 3. Storage: a 256MB IDE disk and an ATAPI CD-ROM, on two channels

A period-correct 1993-94 gaming PC has two IDE channels: the hard disk on
the primary (0x1F0-0x1F7 / 0x3F6 / IRQ14) and the CD-ROM on the secondary
(0x170-0x177 / 0x376 / IRQ15), separate cables and separate controllers
sharing one register-layout convention. That split is not cosmetic, a 2x
CD-ROM's ~250 ms access time would otherwise hold the shared cable and stall
every disk request queued behind it, which is exactly why boards of this era
cabled them apart. `wd1003.h`/`.cpp` (adapted from `ibmpc-at`) is the
primary channel; `atapi_cdrom.h`/`.cpp` is new work for the secondary.

Specs worked from: **ATA/ATAPI-4** (NCITS 317-1998, X3T13/1153D) for the
packet protocol and register semantics; **ATA/ATAPI-6** (T13/1410D revision
3a) for the one rule ATA-4 left under-specified (§3.4 below); **SCSI-3 MMC**
(NCITS 304) and **SFF-8020i** ("ATA Packet Interface for CD-ROMs") for the
command set; **SPC** (NCITS 301) for the commands MMC inherits from the
general SCSI command set. Behavior was additionally checked against the real
DOS-side consumer, FreeDOS `ATAPICDD.SYS` (`FDOS/cdrom`,
`driver/atapicdd/atapicdd.asm`), since that driver, not a spec, is what
will actually probe this device at boot.

### 3.1 The drive is a WD Caviar AC2250

`Wd1003::mount()` models a **Western Digital Caviar AC2250**, 1993-94, sold
as 256MB. It is not the AT's ST-4038 (733 cyl / 5 head / 17 sec) or a
1024 / 16 / 63 drive (504MB, the pre-EIDE INT 13h CHS ceiling). Its
documented translated setup geometry is
**1010 cyl / 9 head / 55 sec = 499,950 sectors = 255,974,400 bytes**.
Physically it's 2233 cylinders and 3 heads with zoned recording; the
1010/9/55 figure is what WD tells you to type into BIOS setup, and what the
drive's IDENTIFY reports as its default logical geometry. Sources: WD's
Caviar drive-parameter table (members.tripod.com/plan_9_inc/wdc.html) and
the AC-2250 entry in stason.org's hard-drive reference.

The size is chosen for the browser as well as the period: the whole image
lives in the wasm heap (§37 covers the storage path). A 256MB drive is also
the more typical fit for a 1993 DX2-66 than one at the CHS ceiling.

The geometry lives in one place, `Wd1003::kCylinders` / `kHeads` /
`kSectorsPerTrack` / `kImageBytes`. `mount()`, IDENTIFY (words 1/3/6/60-61,
model `WDC AC2250`), `Machine::configure_factory_cmos()`'s Type 47 bytes,
`build_freedos_hdd`, and `hdd_boot_check` all read them. The BIOS reads the
CMOS table and the IDENTIFY response through two separate code paths, so
they have to agree or they describe two different drives. CMOS control byte
bit 3 (more than 8 heads) stays set for the AC2250's 9 heads.

FreeDOS 1.3's own FDISK and FORMAT, run by `make hdd-image` on this
geometry, lay the disk out as one FAT16 partition at LBA 55 (C/H/S 0/1/1)
through C/H/S 1009/8/55, 499,895 sectors, 4KB clusters, two 244-sector FATs,
512 root entries. `apply-dos-config.sh` and `build-ctmouse-floppy.sh` read
the partition offset from the MBR instead of assuming LBA 63.

One test-hygiene note: the test images are deliberately truncated prefixes
of the real capacity (a few MB), because the controller bounds-checks
requests against the mounted image's actual length while IDENTIFY reports
`mount()`'s fixed constants, so geometry coverage is unaffected, and no
test has to allocate a quarter gigabyte.

### 3.2 ATAPI device architecture

`AtapiCdrom` is the sole device on the secondary channel (device 0/master,
2x, tray loader), with removable media (`mount()`/`eject()`/
`media_present()`) like `fdc765`'s floppies rather than `wd1003`'s fixed
disk. The public surface is deliberately the same shape as `wd1003`'s
(`reset`/`owns`/`in`/`out`/`data_in16`/`data_out16`/`tick`/`irq_pending`/
`busy`) so the chipset decodes and cascades it identically.

The PACKET protocol (ATA/ATAPI-4 §9.6) runs as four phases: the host writes
a per-block byte-count limit and issues PACKET (0xA0); the device raises DRQ
with Interrupt Reason = 01h (C/D=1, I/O=0) to request the 12-byte CDB; the
CDB arrives as six 16-bit words through the data register; the device
asserts BSY, executes, then hands the result back as one or more DRQ data
blocks with Interrupt Reason = 02h (I/O=1, C/D=0) and the block's real size
published in the byte-count registers, finishing with Interrupt Reason = 03h
("command complete") and DRQ clear. Data transfers are inherently 16-bit,
so the data register needs the same genuinely-atomic path as the primary
channel's 0x1F0 (0x171 is the Error/Features register, not "the high byte of
0x170").

Transfer pacing follows the same tradeoff `wd1003.h` and `fdc765.h` already
document, respect the real total transfer *time*, not the byte-by-byte bus
handshake, via one accumulated cycle-credit target in `tick()`. The rates
are a real 2x drive's: 1x is 75 sectors/sec × 2048 bytes = 153,600 bytes/sec,
so 2x is exactly 307,200 bytes/sec (not a rounded "300 KB/s"), plus real access
time on anything the drive's read-ahead buffer cannot serve. Period
Mitsumi/Sony/Matsushita 2x datasheets quote 250-350 ms *average* access, and
"average" there means a one-third-stroke seek, so access time is modelled as
a 64KB read-ahead buffer (requests landing inside it cost nothing but transfer)
plus a distance-dependent seek calibrated so a one-third-stroke move costs
exactly that published 250 ms. This is why sequential CD reads feel an order of
magnitude faster than seeky ones, a difference a flat per-command latency would
erase, and charging that flat average to *every* non-contiguous read would
make the drive slower than the real thing (§5.7). There is no speed override:
`Read10IsPacedToRealTwoSpeedDriveTiming` asserts a one-sector read *cannot*
complete earlier than real hardware could deliver it.

### 3.3 Detection: the signature is the whole mechanism

The secondary channel's register block is byte-for-byte identical to the
primary's, so "is this a disk or a CD-ROM?" is answered entirely by what the
device puts in the task file after a reset. ATA/ATAPI-4 §9.1 ("Signature and
persistence") specifies Sector Count = 01h, Sector Number = 01h, Cylinder
Low = **14h**, Cylinder High = **EBh** for a packet device, against
01h/01h/00h/00h for an ATA disk. `ATAPICDD.SYS` implements literally that
comparison (`cmp AX, 14EBh` after reading CL/CH), as does the Bochs BIOS's
`ata_detect()`, so this is the single most load-bearing fact in the file.

Two further pieces of the same mechanism are implemented rather than
approximated:

**Status reads 00h after reset, DRDY is deliberately clear.** §9.1's
signature table gives a packet device Status = 00h, and a packet device
never reports itself "ready" the way a disk does. Software that waits for
DRDY on a CD-ROM waits forever, which is precisely why ATAPI drivers poll
BSY instead. Setting DRDY here "to be helpful" would also let a host's ATA
branch (`cl==0 && ch==0 && st!=0`) misclassify the drive.

**IDENTIFY DEVICE (0xEC) is aborted, with the signature placed in the task
file** (ATA/ATAPI-4 §8.12.1), the second half of detection, for a host
that issues the ATA identify before checking the reset signature. It returns
ABRT with the sense key in the Error register's high nibble and no data,
instead of a plausible-looking all-zero geometry. `IDENTIFY PACKET DEVICE`
(0xA1) is the command that actually answers, with ATAPI's own field layout:
word 0 = 0x85C0 (bits 15:14 = 10b ATAPI, bits 12:8 = 05h CD-ROM command
set, bit 7 removable, bits 6:5 = 10b accelerated DRQ, bits 1:0 = 00b 12-byte
packet), word 49 advertising LBA but **not** DMA, word 63 no multiword DMA
modes, words 82-85 declaring the PACKET and DEVICE RESET feature sets.

### 3.4 "Device 0 responding for Device 1": where this device correctly disagrees with `wd1003`

`wd1003` models its permanently-absent slave with a two-read window in which
Cylinder Low/High float to 0xFF/0xFF, tuned to the Bochs BIOS's probe. The
ATAPI device does not copy that. The spec text says otherwise, and the
difference matters.

**ATA/ATAPI-6 (T13/1410D revision 3a), Table 18, "Device 1 is selected and
Device 0 is responding for Device 1"** (which folded in proposal E00118R0's
ATAPI half) specifies this case completely, and specifies it *differently
for a packet device than for a disk*:

- Reads of Sector Count, LBA Low/Mid/High and the Device register: "If the
  device does not implement the PACKET Command feature set, the device shall
  place the contents of the Device 0 ... register on the data bus. If the
  device implements the PACKET Command feature set, the device shall place
  00h on the data bus."
- Reads of Status and Alternate Status: "Place 00H on the data bus" for
  either kind of device.
- The Error register still reports Device 0's real contents.
- Every register *write* lands in Device 0's register, command register
  included, "Place new data into the Command register of Device 0. Do not
  respond unless the command is EXECUTE DEVICE DIAGNOSTICS."

So `wd1003`'s ungated writes and its contents-of-device-0 reads are exactly
right *for a disk*, and 00h is exactly right *for this device*, the two
implementations are not inconsistent, they are two rows of the same table.

This is load-bearing rather than pedantic. `ATAPICDD.SYS` probes device 1
twice: first by writing 0x55/0xAA to Sector Count/Number and requiring the
read-back, then (after a reset) by requiring Sector Count/Number = 01h/01h
before it even looks for the 14h/EBh signature. Handing back this device's
own shared-latch values would pass *both* probes and then return the real
master's 14h/EBh, inventing a phantom ATAPI slave that the driver would
register and then time out against on every access. The floated-0xFF model
would pass the first probe too. Only the spec's 00h answer makes the real
driver conclude, correctly, that nothing is there.

The EXECUTE DEVICE DIAGNOSTIC exception is implemented as stated (it is how
a host collects a diagnostic result covering both device positions from a
single-device channel), and a soft reset issued while device 1 is selected
still resets device 0, per the same table's Device Control row.

### 3.5 Shared latches at offsets 2-5

The register at offset 2 is one physical latch that the host writes as
Sector Count (unused by ATAPI) and reads as the **Interrupt Reason**
register; offsets 4/5 are likewise written as the host's per-block **byte
count limit** and read back as the **actual byte count** of the current
data block. The device only overwrites them at a phase transition. This is
not a modeling convenience: `ATAPICDD.SYS`'s (and the Bochs BIOS's) device
probe writes 0x55/0xAA to offsets 2/3 and requires them to read back before
it will attempt anything else, and that probe works on real drives precisely
because these are shared latches. Modeling offset 2 as a read-only phase
register would make this device invisible to its own driver.

A related preserved quirk: an **odd byte-count limit drops to the next even
value** (ATA/ATAPI-4 §7.3.2), the classic case being a host that writes
0xFFFF and gets 0xFFFE. `ATAPICDD.SYS` does exactly that, and says so in its
own source (`mov AX, 0FFFFh ; set cyl value to max transmit size per PIO DRQ
transfer (==0xFFFE)`). A limit of zero is treated as "no limit" rather than
deadlocking a driver that never set one.

Two more real ATAPI register facts, easy to get wrong by analogy with the
disk: the **Error register on a packet device carries the SCSI sense key in
bits 7:4** (with MCR/ABRT/EOM/ILI in the low nibble), not a disk's
IDNF/UNC bit field (ATA/ATAPI-4 §7.6.1); and **Status bit 4 is SERVICE and
bit 5 is DMA-READY**, not the disk's DSC (ATA/ATAPI-4 §7.15.6.3). `wd1003`
sets 0x10 (DSC) on every completion; this device deliberately never does,
because here that bit would falsely claim a service request is pending for
an overlapped command.

### 3.6 The command set: which CDBs, and why exactly these

Implemented (all 12-byte CDBs; SCSI multi-byte fields are big-endian per SPC
§3.4.2):

| CDB | Command | Why |
| --- | --- | --- |
| 00h | TEST UNIT READY (SPC §7.25) | The universal "is there a disc?" poll; every driver's media loop starts here. |
| 03h | REQUEST SENSE (SPC §7.20) | Carries the sense key/ASC/ASCQ that make every other failure legible. |
| 12h | INQUIRY (SPC §7.5) | Device-type identification (peripheral type 05h, RMB set); succeeds with an empty tray. |
| 1Bh | START STOP UNIT (MMC §6.1.13) | Eject/load -- the tray, from the software side. |
| 1Eh | PREVENT ALLOW MEDIUM REMOVAL (SPC §7.12) | Drivers lock the tray before reading; refusing an eject while locked is real behavior. |
| 25h | READ CAPACITY (MMC §6.1.10) | Reports last LBA + the 2048-byte block size a DOS driver must translate against. |
| 28h | READ(10) (MMC §6.1.7) | The actual data path, 2048-byte sectors. |
| 2Bh | SEEK(10) (MMC §6.1.15) | `ATAPICDD.SYS` issues it for the DOS seek device command. |
| 43h | READ TOC/PMA/ATIP (MMC §6.1.12) | `ATAPICDD.SYS` reads the TOC during its disc-present/disc-type check. |
| 5Ah | MODE SENSE(10) (MMC §6.1.6 / SPC §7.10) | The page 2Ah capabilities probe: speed, loader type, eject/lock support. |

The brief's required seven are all present; SEEK(10), READ TOC and PREVENT
ALLOW MEDIUM REMOVAL were added because reading the real driver showed it
issuing them on paths that would otherwise fail. The single-session,
single-track MODE1 data disc these images always are makes the TOC exactly
track 1 plus the lead-out; MSF addressing carries the Red Book 2-second (150
frame) pregap, so LBA 0 reports as 00:02:00, a driver that treats an MSF
address as an LBA without removing the pregap lands 150 sectors off.

Deliberately **not** implemented, each for a reason rather than as a gap:

- **MODE SENSE(6)/MODE SELECT(6) (1Ah/55h)**, SFF-8020i defines only the
  10-byte forms, so a real ATAPI CD-ROM rejects the 6-byte ones with INVALID
  COMMAND OPERATION CODE and drivers probe with them precisely to learn they
  must use the 10-byte form. Refusing them is the accurate answer.
- **GET EVENT STATUS NOTIFICATION (4Ah)**, an MMC-2 (1997) command that a
  1993-94 2x drive genuinely predates. `ATAPICDD.SYS` asks for it first in
  `getMediaStatus()` but handles refusal explicitly (its
  `@@assumeNotSupported` path returns `STATUS_MEDIA_UNKNOWN`) and falls back
  to polling TEST UNIT READY, which is what a real period drive forced it
  to do. Implementing it would be an anachronism that changes driver
  behavior, not a convenience.
- **Audio playback (PLAY AUDIO, READ CD-DA)**, not implemented, and the
  page 2Ah capabilities byte honestly reports no audio support rather than
  advertising a feature whose CDBs would then fail.
- **DMA**, the PACKET command's Features-register DMA bit is aborted with
  ABRT rather than silently falling back to PIO, so a driver's negotiation
  notices; IDENTIFY advertises no DMA modes for the same reason. There is no
  bus-master controller on this machine to program.
- **Data-out CDBs**, this is a read-only drive; no implemented command
  transfers data to the device. Unknown opcodes get a genuine CHECK
  CONDITION / ILLEGAL REQUEST / INVALID COMMAND OPERATION CODE.

### 3.7 Unit attention is the media-change mechanism

Per SPC §5.6 a unit-attention condition is raised on reset (sense key 06h,
ASC 29h "power on, reset, or bus device reset occurred") and on every media
change (ASC 28h "not ready to ready change, medium may have changed"), and
is reported as CHECK CONDITION on the next command *other than* INQUIRY or
REQUEST SENSE, then cleared. That exemption matters: a host has to be able
to identify the drive and read the sense data without the report getting in
the way. REQUEST SENSE itself therefore never fails, including with an
empty tray, since it is the command a driver uses to find out *why*
something failed, and reading the sense data clears it back to NO SENSE
(SPC §7.20).

The consequence most likely to surprise anyone integrating this: **the first
command after every reset returns CHECK CONDITION**, with 06h/29h/00h. Real
drives do exactly this, and it is why DOS CD-ROM drivers issue TEST UNIT
READY twice at startup. It is genuine drive behavior, and the first one a
driver author hits.

With a disc absent, the standing condition after the change has been
reported is NOT READY / MEDIUM NOT PRESENT (02h/3Ah/00h) for every
media-requiring command; INQUIRY and MODE SENSE(10) keep working, because
they describe the drive rather than the medium. The host-side `eject()` (a
front-panel button, the user physically taking the disc, the real-world
equivalent of the emergency eject hole) deliberately overrides a driver's
PREVENT ALLOW MEDIUM REMOVAL lock; the START STOP UNIT path honors it and
returns ILLEGAL REQUEST / MEDIA REMOVAL PREVENTED (05h/53h/02h), as a real
drive does.

### 3.8 Tests

`tests/atapi_cdrom_test.cpp` (40 cases) drives the device only through
`in`/`out`/`data_in16`/`data_out16`, i.e. the real programming model the
chipset's port decode will use, matching `wd1003_test.cpp`'s convention.
Coverage: port decode; the post-reset ATAPI signature and 00h status; the
0x55/0xAA scratch probe through the shared latches; soft reset; Table 18's
00h responses for the absent device 1 and its EXECUTE DEVICE DIAGNOSTIC
exception; IDENTIFY PACKET DEVICE's field layout; IDENTIFY DEVICE's abort;
DEVICE RESET raising no interrupt; every implemented CDB including its
no-media and out-of-range failures; byte-count-limited multi-block PIO with
an interrupt per block; the odd-limit rounding; unit attention on reset and
across eject/re-insert; the lock/override pair; nIEN masking; and the
2x pacing floor. `tests/wd1003_test.cpp` (13 cases) is the adapted disk
suite described in §3.1.

### 3.9 Open items for the chipset/driver integration

- **Cycle units, resolved.** `AtapiCdrom::kCpuHz` (66,000,000, the DX2-66's
  internal clock) assumes `tick()` is fed the CPU's internal cycle count.
  `wd1003.h` and `fdc765.h` inherited the AT's `kCpuHz = 8000000.0` unchanged
  from the AT; `Chipset::tick()` feeds all three devices the same
  `cpu_cycles` counter, which `Machine` advances at the real 66MHz clock
  (`machine.h`'s `kCpuHz`). Both constants are `66000000.0` to match. At the
  AT's value, `wd1003` and `fdc765` would pace their transfers as if each
  tick were 8.25x further apart than it is, finishing floppy/HDD transfers
  8.25x faster than real time.
- **`wd1003`'s transfer rate is anachronistic but not a violation.** Its
  625,000 bytes/sec is a representative ST-506/412 MFM figure; period WD
  Caviar drives of this capacity class (e.g. the AC-2540, 516MB, Jan 1994)
  quote an 11.1MB/s PIO3 *interface* burst rate, with sustained
  drive-to-buffer media rate well below that, typically in the 1-2MB/s
  range for a 3600-4500RPM drive of this era, per period reviews. Since
  625,000 B/s (~610KB/s) is already on the *slow* side of that range, this
  is a conservative inaccuracy, not a speed-up, and it stays open until a
  specific period drive's datasheet supplies a precise figure.
- **The reset unit attention** (§3.7) is the behavior most likely to
  surprise a driver author during bring-up.

## 4. CPU core: a real-mode 80486 DX2-66

> **Real-mode baseline, extended by §6.** This section describes the
> real-address-mode core: its decode shape, 32-bit registers, addressing,
> and cycle costs. Protected mode, paging, task switching and the FPU sit on
> top of it (§6). They replace §4.1's no-op list, its SMSW/CR0 reasoning, and
> §4.3's segment-limit premise (§5.4); §6.1 lists exactly what differs.

`cpu80486.h`/`.cpp` is the CPU core. It follows the same shape as
`ibmpc-at/cpu80286.h`, a `Bus` struct of `std::function` callbacks with no
back-reference to the chipset, big-switch opcode dispatch with `grp*`
helpers for the ModR/M-reg-selected groups, an `on_unimplemented`
diagnostic hook, and per-opcode cycle costs cited inline, but the parts
below are genuinely different, and the differences are the point.

### 4.1 Real mode only, and what that excludes

> §6.1 covers the current state: every opcode on the list below does its
> real work, SMSW reports hardwired ET = 1, MOV CR0 honors PE, and the
> protected-mode-only members are #UD in real mode.

What follows is the real-address-mode baseline, not the core as it stands.
§6.1 is the current behavior. The baseline has no protected mode, no paging,
no V86 and no x87, and FreeDOS and MS-DOS 6 never leave real mode. The baseline
has a specific, enumerated list of opcodes that are **documented no-ops** rather than
silent gaps. Each consumes its ModR/M (and SIB/displacement, so instruction
lengths stay correct) and charges its real published cycle cost, then does
nothing:
LGDT/SGDT/LIDT/SIDT/LLDT/SLDT/LTR/STR/ARPL/LAR/LSL/VERR/VERW/CLTS, LMSW,
MOV to/from CR0-CR3 and DR0-DR7 and TR6-TR7, INVD/WBINVD/INVLPG, and the
whole x87 ESC range 0xD8-0xDF. The header comment carries that list in the
same style as `cpu80286.h` lines 14-19.

Two of those are better than inert, because real mode has a genuinely
correct answer available:

- **SMSW** stores a real machine-status word: PE = 0 (this core is always
  in real mode) and ET = 0 (no coprocessor in the real-mode baseline).
  Real-mode software does read this, a DOS extender stub asking "am I
  already in protected mode?", and 0 is the true answer here, not a
  placeholder.
- **MOV CR0, r32** accepts the write but masks PE off, so a read-back
  keeps saying "still real mode" instead of claiming a mode switch that
  never happened. CR1-CR3 and DR0-DR7 round-trip as plain storage so
  software probing them isn't fed garbage.

`on_unimplemented` deliberately does **not** fire for any of these: they
are decided gaps, not discoveries. It fires only for genuinely
unrecognized opcodes, which is what makes it usable as the same
evidence-gathering tool the AT review uses when a BIOS assumes a 386
baseline (`IBM_PCAT_REVIEW.md`).

The one gap that is a real behavioral departure rather than an unexecuted
instruction is **#GP(0) on a segment-limit violation**, see §4.3.

### 4.2 32-bit registers are native here, not a concession

`cpu80286.h` stores AX/BX/... as `uint32_t` and documents it at length as a
*compatibility workaround*: a genuine 80286 has no EAX, no 0x66 prefix and
none of the 0x0F opcodes, and that core only has them because the prebuilt
BIOS it boots turned out to assume a 386+ baseline. None of that framing
applies here. An Intel486 has EAX/EBX/ECX/EDX/ESP/EBP/ESI/EDI, FS and GS,
the 0x66 and 0x67 prefixes, and the 0x0F escape space as architecture.
Real-mode DOS code uses them routinely, DOS extender stubs, 32-bit-aware
memory managers, anything a 386-targeting compiler emits in 16-bit mode,
without ever entering protected mode. So the comments here describe
behavior, not an apology, and `eax`/`esp`/`eip`/`eflags` are the public
field names.

The sub-register write rules come with that: writing AX never disturbs
EAX's upper half, writing AL never disturbs AH. `cpu80486_test.cpp` pins
both down.

**FS and GS** are fully implemented, the 0x64/0x65 override prefixes, MOV
to/from them, PUSH/POP FS/GS (0F A0/A1/A8/A9), and LSS/LFS/LGS (0F
B2/B4/B5).

One place where this core deliberately diverges from its sibling:
**PUSH SP/ESP**. From the 286 onward the pushed value is the register's
value *before* the decrement; the 8086 pushes the already-decremented one.
That asymmetry is the classic `push sp / pop ax / cmp ax,sp` runtime check
for 8086-vs-286+ ([OSDev CPU
Detection](https://wiki.osdev.org/User:ChosenOreo/CPU_Detection)).
`ibmpc-at/cpu80286.cpp`'s `push_reg()` implements it inverted, it pushes
the decremented value and its comment asserts that is the 286 behavior.
This core implements the documented direction and tests it. The 286 core is
a separate file and is not covered here.

### 4.3 The 0x67 address-size prefix, SIB decoding, and the 64KB limit

`cpu80286.h` explicitly declines 0x67 ("deliberately NOT supported"),
because nothing that machine boots needed 32-bit *addressing*. This core
implements it for real: the full 32-bit ModR/M table, the SIB byte
(scale 1/2/4/8, index, base), `mod=00 rm=101` as disp32-with-no-base, SIB
index field 100 as "no index" (ESP can never be an index), SIB base 101
with mod=00 as disp32-with-no-base, and the ESP/EBP-base -> SS default
segment rule (Intel 80486 PRM, "Addressing Modes" and "Default Segment
Attribute"). 0x67 also selects ECX over CX as the LOOP/JCXZ counter.

**The segment-limit decision.** A 32-bit effective address can exceed
0FFFFh, and real mode's segment limit is a fixed 64KB. On real hardware one
of two things happens: the CPU raises #GP(0), or, if an "unreal mode"
setup has already loaded a descriptor with a larger limit, the access
simply succeeds. The second is unavailable here by construction: expanding
a limit requires a protected-mode descriptor load, and this core has no
protected mode at all, so the 64KB limit is unconditional.

This core **wraps the offset into the low 16 bits**, exactly as the 16-bit
addressing path already wraps mod 64KB, and lists the missing #GP(0)
alongside the other protected-mode omissions in the header comment. The
reasoning: the fault is protected-mode machinery in a core that has none;
DOS installs no handler for vector 13, which in the real-mode IVT is IRQ5's
slot, so a "faithful" fault would vector into the IRQ5 handler and produce
chaos rather than fidelity; and an out-of-range EA in practice means a
decoding bug in this core rather than genuine guest behavior. Wrapping
keeps that visible and debuggable instead of scattering spurious
interrupts. It is labelled as a gap, not presented as correct.
`Addr32EffectiveAddressAboveSixtyFourKWrapsIntoTheSegment` pins the
decision down so it can't drift silently.

**LEA is the exception, and it matters.** `RM` carries both `off` (the
64KB-limited memory offset) and `ea32` (the untruncated 32-bit sum), and
LEA reads `ea32`. LEA touches no memory, so a 32-bit addressing form there
is pure arithmetic, the "LEA as a three-input adder" idiom every 386+
compiler emits. Truncating `LEA EAX,[EBX+ECX*4]` to 16 bits would produce a
wrong *number*, not just a wrong address. Tested.

For the same "real mode's stack segment is always 16-bit" reason, pushes
and pops move SP and wrap mod 64KB while leaving ESP's upper half alone,
even for the 32-bit push/pop forms (which move SP by 4). There is no
descriptor B bit that could make it otherwise.

### 4.4 The AC flag (EFLAGS bit 18)

Implemented as genuine storage, because it is a real period detail with a
primary-source citation. Intel's AP-485, *Intel Processor Identification
and the CPUID Instruction*, says in its "Intel386 processor check":

> The AC bit, bit #18, is a new bit introduced in the EFLAGS register on
> the Intel486 processor to generate alignment faults. This bit cannot be
> set on the Intel386 processor.

and its sample code flips AC through PUSHFD/POP/XOR/PUSH/POPFD/PUSHFD and
branches on "can't toggle AC bit, processor=80386". Before CPUID existed on
486 steppings that was *the* way software told a 386 from a 486, and a
period "gamer's dream" machine plausibly runs something that probes this
way.

So: `kPopfdMask` (0x00047FD5) lets AC round-trip through POPFD/PUSHFD; the
16-bit POPF cannot touch it (it lives above bit 15), which is precisely why
the AP-485 sequence needs the 32-bit forms. Alignment *faults* themselves
require CR0.AM and CPL 3, neither of which exists in real mode, so the bit
is observable-but-inert here exactly as on real hardware.
`AcBitTogglesAndReadsBackPerAp485` runs the AP-485 sequence as written.

Adjacent decision: **POPF/POPFD/IRET load IOPL and NT unconditionally**,
which is genuine real-mode behavior (no CPL exists to gate them).
`ibmpc-at/cpu80286.cpp` deliberately masks those to zero because letting
them round-trip reproducibly broke the FreeDOS 1.3 installer on that
machine, root cause unconfirmed (`IBM_PCAT_REVIEW.md`). This core
implements the genuine behavior, since there is no evidence of that failure
here. The POPF call site carries a comment saying so, and it is the first
thing to bisect if a FreeDOS oddity ever shows up on this machine.

### 4.5 CPUID is absent, and that is the accurate answer

**§13.1 has the current state.** The part this machine carries is an
SL-Enhanced IntelDX2-66, which does have CPUID. FreeDOS's own `VINFO`
identifies the CPU with AP-485's ID-flag/CPUID sequence, and `FDAUTO.BAT`
runs `CTMOUSE` only on the 386+ branch that answer selects, so CPUID is
needed for the mouse driver, APM and CD-ROM support. The reasoning below
holds for the earlier parts.

An early DX2-66 predates CPUID. AP-485 states plainly:

> Older versions of Intel486 SX, Intel486 DX and IntelDX2 processors do not
> support the CPUID instruction

only the later SL-Enhanced / Write-Back-Enhanced 486 parts have it, and
the timing table used for cycle costs throughout this core lists CPUID as
"(Pentium+)". On such a part `0F A2` is an unrecognized opcode, and
software that probes with CPUID falls back to the AC-flag technique above,
which works.

### 4.6 Cycle costs

> Extended by §6.8, which adds the protected-mode control-transfer rows and the
> x87 rows under the same cited-number-versus-labelled-model discipline.

Costs are the 486 column of the Quantasm *80x86 Integer Instruction Set
(8088 - Pentium)* table, which reproduces Intel's own i486 PRM timing
appendix, cited inline per instruction. Four things are worth recording:

1. **No `+m` term.** `cpu80286.h` needs `kQueueRefillTax` because every 286
   taken-branch entry is a *range* driven by prefetch-queue refill, and the
   table's `+m` appears in the 286 and 386 columns. The 486 column
   publishes flat numbers for control transfers (Jcc taken 3, JMP near 3,
   CALL near 3, RET 5, IRET 15), so this core needs no equivalent tax and
   charges the published figures directly.
2. **The one published addressing-mode adjustment is implemented.** The
   table's legend gives, for the 286-486, "base+index+disp = +1, all
   others, no penalty". `decode_modrm()` adds exactly that, which is also
   what makes LEA land correctly on its published "1-2".
3. **Genuine generational differences are preserved, not inherited from the
   286 core.** The on-chip cache makes MOV 1 clock in every direction (the
   286's directional 3/5 asymmetry is gone); the barrel shifter makes
   SHL/SHR/SAR/ROL/ROR count-*independent* (the 286 charged 5+n); the ALU
   group is not uniform (CMP/TEST read memory at 2, ADD and friends
   read-modify-write at 3); and IN/OUT are 14/16 against the 286's 5/3. A
   copied 286 table would have gotten every one of these wrong.
4. **Four families publish a data-dependent range, and each is a labelled
   model fitted to both published endpoints**, not an invented constant,
   and not a claim of pipeline-accurate simulation:
   - **MUL/IMUL** (13-18 / 13-26 / 13-42 for 8/16/32-bit): the 486 uses an
     early-out multiply whose cost depends on the multiplier's
     most-significant-bit position. Intel publishes the mechanism and both
     endpoints but no per-bit formula, so `mul_cost()` charges
     13 + max(0, msb - 3), capped at 13 + (width - 3), reproducing 18/26/42
     exactly at the top and 13 at the bottom.
   - **BSF/BSR** (6-42 / 6-103): both scan a bit at a time, so
     `bitscan_cost()` charges the published floor plus one clock per bit
     actually examined.
   - **RCL/RCR by a count** (8-30 register, 9-31 memory): still iterative
     unlike the plain shifts, so `rotate_carry_cost()` is linear in the
     count, anchored so count 1 gives the floor and it saturates at the
     ceiling.
   - **CMPXCHG against memory** ("7-10"): charged 7 for compare-only and 10
     for compare-plus-store, the only split the instruction has. Intel
     prints the range without saying which end is which, so that is a
     labelled reading.

   Two smaller labelled inferences: REP INS/OUTS have no published 486 REP
   formula, so the published per-iteration 17 is charged per iteration; and
   prefixes cost 1 clock each, which is the published LOCK figure applied
   uniformly (the table publishes no separate segment-override /
   operand-size / address-size prefix cost).

`interrupt()` charges INT3's published 26, same IVT work, no immediate to
fetch, and a private `do_interrupt()` performs the frame push and
vectoring without touching the cycle counter, so step()'s INT/INT3/INTO and
fault paths each charge their own published cost exactly once.
(`cpu80286.cpp` bills both: its `interrupt()` adds 45 to `cycles` *and*
step() adds its own 23+tax for the same instruction.)

### 4.7 Tests

> §6.10 lists the full suite across its fixtures. The figures below describe
> the real-mode and 32-bit-register fixtures.

`tests/cpu80486_test.cpp`, 80 semantic cases against a mock `Bus`,
weighted toward what is new: the 32-bit register file and sub-register
write rules, PUSHAD/POPAD, FS/GS and their override prefixes, LSS/LFS/LGS,
the whole SIB/0x67 matrix (base+index*scale, disp32-no-base, scaled index
with no base, ESP/EBP -> SS defaults, JECXZ), the limit-wrap decision, LEA
keeping its full 32-bit result, BSWAP/XADD/CMPXCHG (both branches),
MOVZX/MOVSX, BSF/BSR including the undefined-destination-on-zero contract,
two- and three-operand IMUL, 32-bit shifts, SHLD/SHRD, the BT group
including memory bit-offsets beyond the operand width, the AC flag and the
AP-485 sequence verbatim, and that every protected-mode and FPU no-op stays
silent while an undefined opcode fires `on_unimplemented` (CPUID is
implemented, §13.1).

`tests/cpu80486_timing_test.cpp`, 49 cycle-cost cases, for the same
reason ibmpc-at keeps its cost model in a separate suite: it regresses
silently and only surfaces later as period software measuring the wrong CPU
speed. Covers the ALU read-vs-read-modify-write split, the
effective-address penalty, all four data-dependent range models at both
endpoints, the count-independence of the plain shifts against RCL/RCR's
saturation, every REP string formula including the published "5 if n=0, 13
if n=1" special cases, LOOP/JCXZ per-op-per-outcome costs, control
transfers, the no-double-billing property of `interrupt()`, the per-prefix
clock, and that the documented no-ops still carry their real published
figures.

ARPL (0x63) sits in the one-byte opcode table, not the 0x0F escape space,
so in the real-mode baseline it is a documented no-op rather than an
`on_unimplemented` hit. A test pins that.

### 4.8 Sources

- Intel 80486 Programmer's Reference Manual (1990/1992), instruction
  semantics, addressing modes, EFLAGS layout, flag-affected tables.
- Intel 8086/8088 User's Manual, the shared 8086-legacy subset.
- Intel AP-485, *Intel Processor Identification and the CPUID Instruction*
 , the AC-bit 386-vs-486 check and the IntelDX2 CPUID-availability
  statement. ([mirror](https://datasheets.chipdb.org/Intel/x86/CPUID/24161817.pdf))
- Quantasm, *80x86 Integer Instruction Set (8088 - Pentium)*, the 486
  cycle-count column, reproducing Intel's i486 PRM timing appendix.
  ([mirror](https://www2.math.uni-wuppertal.de/~fpf/Uebungen/GdR-SS02/opcode_i.html))
- OSDev Wiki, CPU Detection, the PUSH SP 8086-vs-286+ behavior.
  ([link](https://wiki.osdev.org/User:ChosenOreo/CPU_Detection))

## 5. Building the shipped HDD image with the real FreeDOS 1.3 installer

`disks/build_freedos_hdd.cpp` produces `disks/freedos-hdd.img` by actually
running the genuine, unmodified FreeDOS 1.3 installer against this emulator
end to end, same technique and same standard as
`ibmpc-at/disks/build_freedos_hdd.cpp` (see `IBM_PCAT_REVIEW.md` §11), never
a hand-crafted filesystem. `disks/hdd_boot_check.cpp` then cold-boots the
finished image standalone. `make hdd-image` and `make hdd-boot-check` tie
both together; neither is part of `check`, because running a real installer
takes real minutes.

### 5.1 Boot path: the floppy, and El Torito is not merely "unverified"

The installer boots off FreeDOS's companion boot floppy, not off the CD
through the Bochs-legacy BIOS's advertised `eltorito` support. The reason
is stronger than risk-aversion.

The ISO's El Torito boot catalog (sector 17, `EL TORITO SPECIFICATION`,
pointing at `isolinux/isolinux.bin`) names a no-emulation boot image that is
**ISOLINUX**, whose `isolinux.cfg` chains to **MEMDISK** to load one of
`isolinux/fdinst.img` / `fdlive.img` / `fdx86.img` into extended memory and
present it as a virtual floppy through an INT 13h hook. MEMDISK reaches
extended memory the only ways available: protected-mode transitions or the
BIOS's own INT 15h AH=87h block move, which `rombios.c` itself performs in
protected mode. The image build does not depend on that path.

The floppy path is a supported one: FreeDOS ships `FD13BOOT.img`
specifically to boot a machine into the installer and read everything else
from the CD drive, and this machine's `configure_factory_cmos()` already
seeds CMOS 0x3D = 0x21 (floppy first, hard disk second). Both files come out
of the one SHA-256-verified zip (`disks/fetch-freedos-cd.sh`).

### 5.2 The real installer flow: one CD, no floppy swapping at all

This differs from ibmpc-at's flow, and the difference is not cosmetic.
ibmpc-at drives `FD13-FloppyEdition`'s six-disk split archive,
answering "insert diskette containing file A:\FREEDOS.NNN" 114 times. Here
there is **no media swapping whatsoever**:

1. The floppy's `FDCONFIG.SYS` loads `HIMEMX.EXE` and takes `DOS=HIGH`.
2. Its `FDAUTO.BAT` calls `FREEDOS\BIN\CDROM.BAT`, which probes CD drivers in
   a fixed order and settles on the first one present, **`UDVD2.SYS`**, not
   `ATAPICDD.SYS`. That matters (§5.5). `SHSUCDX` then publishes the drive,
   landing on **D:**.
3. `SETUP.BAT floppy` checks `%CDROM%` and, if `%CDROM%\SETUP.BAT` and
   `%CDROM%\freedos\setup\version.fdi` both exist, `pushd`es to the CD and
   re-invokes `SETUP.BAT cdrom`. **Every stage script, dialog template and
   package then comes off the CD**; the floppy's only job was to boot and
   load the CD driver.
4. `FDSETUP.BAT` runs stages 000-900: language, welcome/proceed, partition
   (`stage400`), reboot, format (`stage500`), package-media discovery
   (`stage600`), the `FDASK000`-`FDASK700` questions (`stage700`), the final
   go/no-go (`stage800`), and completion (`stage900`).

Step 3 has a silent-failure mode: if the CD is unreadable,
`if not exist %CDROM%\SETUP.BAT` simply falls through to
`:NotFloppy` and the **floppy-resident** installer runs instead. It has all
the stage scripts but no package tree, so it partitions and formats
perfectly and then dies at `stage600` with "Unable to locate the installation
packages." Nothing anywhere says "the CD failed". An unreadable CD
presents exactly this way (§5.5).

**Dialog mechanics.** Every prompt is a V8Power `vchoice` option box:
arrow keys move a highlight, Enter accepts, and the batch file's `/d N`
switch sets which entry starts highlighted. Read out of the installer's own
scripts rather than guessed, the three destructive prompts, `stage400`'s
"partition your drive?", `stage500`'s "format your drive?", `stage800`'s
"install now?", **all pass `/d 2`, i.e. they start on "No - Return to
DOS"**. Answering any of them with a bare Enter aborts the install ("The
installation of FreeDOS 1.3 has been aborted"). They need Up-then-Enter;
every other prompt's default is already the wanted answer.

**Two timing rules**, both the same class as ibmpc-at's keyboard-flush
"nag" mechanism:

- A dialog's *text* appears well before `vchoice` starts reading the
  keyboard, the batch file is still drawing the frame and option box, and
  a key pressed inside that window is discarded. The tool waits for the
  screen to go quiet (~1.2 s of emulated time) first, which is what a person
  does anyway.
- One press is not enough, and that is not a tuning artifact. `stage300`
  calls `vchoice` from inside a redraw loop (`:LanguagePrompt` re-echoes the
  prompt and re-invokes `vchoice` each iteration), so a single Enter is
  routinely consumed by the wrong invocation and the installer simply sits
  there. The tool therefore *nags*: it re-sends the rule's keys every ~3 s of
  emulated time for as long as the prompt text is still on screen, bounded at
  30 attempts so a genuinely stuck prompt fails the run instead of hammering
  keys forever. This is the same mechanism ibmpc-at needed
  (`IBM_PCAT_REVIEW.md` §11).

  Repeated presses are safe here because each rule's key sequence is
  **idempotent against a `vchoice` option box**: Up clamps at the top entry
  instead of wrapping, so an extra "Up Enter" re-selects the same "Yes", and
  extra presses cannot leak into the next dialog's default ("No", for the
  three destructive prompts). In a full run all three destructive prompts
  take 3-5 presses each and every one of them partitions / formats / installs
  rather than aborting.

Matching is also **rule-based rather than position-ordered**, because
`stage400` genuinely reboots the machine and the installer comes back through
the language and welcome screens a second time.

### 5.3 The factory CMOS reports extended memory where the BIOS reads it

`Machine::configure_factory_cmos()` seeds extended-memory size at CMOS
**0x17/0x18** and **0x30/0x31**, and the memory above 16MB at **0x34/0x35**.
The pinned `rombios.c` (bochs-emu/Bochs@`ff17a0c2`) reads **0x30/0x31** and
nothing else for extended memory: INT 15h AH=88h ("Get the amount of
extended memory") is `inb_cmos(0x30)`/`inb_cmos(0x31)`, and so is AX=E801's
CX, with memory above 16MB coming from **0x34/0x35** in 64KB units. The BIOS
never reads 0x17/0x18.

The BIOS answers every extended-memory query from those bytes, so this
32MB machine depends on them: with zero there, HimemX declines to install
("Extended memory is too small or not available. Driver won't be
installed"), `LBACACHE` fails with "XMS error. 43ff Not enough free XMS
memory", and `UDVD2.SYS` fails to load with error #255, because its own
documentation states "Without UHDD, UDVD2 takes 128K of XMS memory for its
own input buffer." No XMS means no CD-ROM driver, and `CDROM.BAT` ends at
"unable to load an appropriate CD/DVD driver".

0x30/0x31 hold the same 0x7C00 (31744 KB) as 0x17/0x18 (real AT CMOS maps
carry the figure in both places, 0x17/0x18 as the Setup-configured copy and
0x30/0x31 as the POST-verified one, holding the same value), and 0x34/0x35
hold 0x0100 (256 x 64KB = 16MB above the 16MB line). Neither pair falls
inside the 0x10-0x2D checksum range, so the checksum is unaffected.
`MachineTest.ConstructorSeedsFactoryCmosConfiguration` asserts both pairs
and that the two copies agree.

### 5.4 Unreal mode: a 32-bit effective address reaches past 64KB

HimemX's XMS block-move routine runs in "unreal mode", as its instruction
stream shows:

```
0291:045D  2E 0F 01 16 18 00   CS: LGDT [0018]
0291:0463  0F 20 C0            MOV EAX, CR0
0291:0466  40                  INC AX                 ; set PE
0291:0467  0F 22 C0            MOV CR0, EAX
0291:046A  EB 00               JMP short $+2
0291:046D  8E DA / 8E C2       MOV DS,DX / MOV ES,DX   ; DX = 0008h
0291:0471  0F 22 C0            MOV CR0, EAX            ; clear PE
...
0291:043D  33 D2 8E DA 8E C2   XOR DX,DX / MOV DS,DX / MOV ES,DX
0291:0443  66 C1 E9 02         SHR ECX, 2
0291:0447  F3 67 66 A5         REP MOVSD               ; addr32 + opsize32
```

A brief excursion through protected mode loads a 4GB-limit descriptor into
DS/ES, PE is cleared again, and, because loading a segment register in real
mode sets its base but leaves the cached limit alone, the big limit
survives. A 32-bit `REP MOVSD` with DS=ES=0 can then address all of
extended memory.

Both 32-bit addressing paths therefore pass the full offset to the bus:

- `decode_modrm()` passes `ea` through untruncated, and
- `string_op()` honors the 0x67 prefix, using ESI/EDI/ECX as pointers and
  counter and advancing the 32-bit registers, while the un-prefixed form
  still moves only SI/DI and decrements only CX, leaving the upper halves
  alone.

Truncating either to 16 bits would send every XMS block move to a wrapped
address inside the first 64KB and corrupt live memory instead of failing.

**This replaces §4.3's premise.** That section chose wrapping partly because
"an out-of-range EA in practice means a decoding bug in this core rather
than genuine guest behavior", and treated the unreal-mode case as
"unavailable here by construction: expanding a limit requires a
protected-mode descriptor load, and this core has no protected mode at all".
The guest performs the descriptor load whether or not the core models
limits, and then proceeds as if unreal mode is active. Letting the full
offset reach the bus is the faithful emulation of the state the guest
believes it is in. The protected-mode core in §6 models the limits for real.

The change is narrow, so nothing else moves:

- `cpu80486.h` has flat `phys32`/`rb32`/`wb32`/`rw32`/`ww32`/`rd32`/`wd32`
  and width-dispatching `mrb`/`mwb`/`mrw`/`mww`/`mrd`/`mwd` that use the
  ordinary 16-bit helpers for any offset a 16-bit form can produce and the
  flat ones only above 0FFFFh. Real-mode wrap-at-the-top behavior for normal
  accesses is bit-for-bit the 16-bit behavior. The flat forms do not wrap a
  multi-byte access at 0FFFFh, because with a 4GB limit there is nothing
  there to wrap at.
- The 16-bit `decode_modrm()` branch wraps every intermediate sum into a
  `uint16_t`, so it can never produce an offset above 0FFFFh.

The real-mode baseline has no #GP(0) on a segment-limit violation. §6.2
covers protected-mode limit checks and §44.5 the real-mode check against the
cached limit. Tests:
`Addr32EffectiveAddressAboveSixtyFourKReachesPastTheSegment`,
`Addr32StringOpUsesEsiEdiEcxAndReachesAboveSixtyFourK` (the exact
`F3 67 66 A5` encoding HimemX uses, with both pointers above 64KB and their
low halves differing from the full values so a truncating core fails),
`StringOpWithoutAddr32KeepsUsingTheSixteenBitPointers`, and
`SixteenBitAddressingStillWrapsModSixtyFourK` (with the address a
non-wrapping core would hit instead).

### 5.5 READ TOC format 1 (session information)

With XMS and unreal mode working, `UDVD2.SYS` loads and `SHSUCDX` publishes
the drive as D:. The driver's first three commands decide whether the CD is
readable:

```
43 00 01 00 00 00 00 00 0C 00 00 00  -> READ TOC format 1, 12-byte allocation
00 00 00 00 00 00 00 00 00 00 00 00  -> TEST UNIT READY (CHECK CONDITION 06h/29h, the reset unit attention, §3.7)
43 00 01 00 00 00 00 00 0C 00 00 00  -> READ TOC format 1 again
```

That is **READ TOC/PMA/ATIP with format 0001b ("Session Information") and a
12-byte allocation length**. A device that implements only format 0000b and
rejects everything else with INVALID FIELD IN CDB (05h/24h) leaves
`dir d:\` answering **"Error reading from drive D: data area: drive not
ready"**, every path on the CD unreachable, and `SETUP.BAT` quietly falling
back to the floppy installer (§5.2) and dying at `stage600`.

§3's command set was first chosen by reading **`ATAPICDD.SYS`**, on the
reasoning that "that driver, not a spec, is what will actually probe this
device at boot." The driver that probes this device is `UDVD2.SYS`.
`CDROM.BAT` tries `AHCICD`, `VIDE-CDD`, `OAKCDROM`, `GSCDROM`, then
**`UDVD2`**, and only reaches `ATAPICDD` fifth among the bundled drivers;
`UDVD2.SYS` is present on the boot floppy, so it always wins. The command set
is checked against that consumer.

`cmd_read_toc()` implements format 0001b per MMC: a 4-byte header (TOC data
length = 10, first complete session = 1, last complete session = 1) plus
exactly one TOC track descriptor for the first track of that session
(ADR=1/CONTROL=4 for a data track, track 1, start address 0, honoring the
CDB's MSF bit through the same `put_addr` helper and therefore the same Red
Book 150-frame pregap as format 0). That is 12 bytes total, which is why a
driver asking for this format sets the allocation length to 0Ch. Formats 2
and up are refused, because they are not implemented, and a driver probing
for them is supposed to learn that. Tests:
`ReadTocFormatOneReportsSessionInformation` and
`ReadTocStillRejectsAnUnimplementedFormat`.

With this in place the CD is fully readable: over a thousand ATAPI commands
per session, READ(10)s returning real 2048-byte sectors, and `SETUP.BAT`
handing off to the CD's own installer as designed.

### 5.6 What the installer's own output confirms about the hardware

Each line is third-party software independently agreeing with this
machine's configuration. The sample lines below are from a 504MB-geometry
run; the shipped drive is the AC2250 (§3.1).

- `ata0 master: RETROWEB IDE 504MB ATA-0 Hard-Disk ( 504 MBytes)` and
  `ata1 master: RETROWEB CD-ROM 2X ATAPI-4 CD-Rom/DVD-Rom`, the BIOS
  detecting both devices on separate channels via the genuine ATA/ATAPI
  signature protocol (§3.3).
- `HimemX ... KBC A20 method used`, the 8042 output-port A20 gate
  (`i8042.h`) driven by real third-party code, and `Kernel: allocated 46
  Diskbuffers = 24472 Bytes in HMA` confirming HMA access above the 1MB line
  actually works.
- `LBAcache ... disk 0x80 CHS=...x16x63 EDD 3.0 0504 MB ISA 01 ATA Master`,
  a third-party cache driver reading back exactly the 16-head/63-sector
  geometry §3.1 chose, through INT 13h.
- `Disk size: 504 Mbytes, FAT16.` from the real `FORMAT.EXE`, the 504MB
  CHS-ceiling geometry surviving all the way into a filesystem.
- `illegal partition table - drive 00 sector 0` (four times) on the first
  boot: the FreeDOS kernel complaining about a blank, all-zero MBR, which is
  what a factory-fresh disk has.

### 5.7 CD-ROM access time: a read-ahead buffer and a distance-dependent seek

The access-time model is calibrated against the install workload. Each
package in the installer's package-install stage costs ~2.2 billion cycles
(**33 seconds of emulated time**) if every non-contiguous read pays the
published average access, regardless of size: `append.zip` is 42,784 bytes,
which is 0.14 s of transfer at this drive's real 307,200 bytes/sec. A
full-set install would need ~440 billion cycles, hours of emulated machine
time.

Raw throughput is correct. `copy d:\packages\base\kernel.zip nul` (772,512
bytes) from a live DOS prompt takes 201,166,713 cycles = **3.05 s**, against
2.51 s of pure transfer plus a few seeks. The drive really is a 2x drive, so
the per-package cost is not data transfer.

The pacing accounting over a boot-plus-early-install window shows where the
time goes:

```
reads=2000  seeks=505  charged=140.4s  xfer=14.1s  seek=126.2s
```

**90% of all charged CD time is access penalties**, not data. The
distribution of how far those 505 "seeks" actually travel from where the
previous read ended:

| distance from previous read's end | count |
| --- | --- |
| < 32 sectors (64KB) | **216** |
| 32-127 | 4 |
| 128-511 | 34 |
| 512-4095 | 80 |
| 4096-32767 | 170 |
| >= 131072 | 1 |

Charging the **full published 250 ms average access** to every read that
does not begin exactly where the previous one ended would include a read
*thirteen sectors behind* the head. No real drive takes 250 ms to re-read
something it just passed, and 43% of the penalties are inside 64KB. This is
the access pattern of DOS running batch files off a CD: the batch file, the
utilities it invokes, and the ISO directory extents are re-read constantly
within a few tens of KB. Two properties of a real drive account for it:

1. **A read-ahead buffer.** §3.2 and `atapi_cdrom.h` say a contiguous read
   pays nothing "because a real drive's head is already there **and its
   read-ahead buffer holds the data**". That needs a buffer, not just a
   single LBA position. Period 2x drives shipped 64KB-256KB buffers.
2. **Distance-dependent access time.** A datasheet's "average" is a
   one-third-stroke figure, not the cost of every seek. Charging the average
   flat is the same class of error §4.6 avoids for the CPU by fitting
   data-dependent instruction costs to both published endpoints instead of
   charging a single number.

A flat average makes the emulated drive slower than the real hardware it
models. That is an accuracy problem, and the model here is not a speed knob:
the drive's published figures are unchanged and remain the calibration
anchor.

`atapi_cdrom.{h,cpp}` implement it with `access_seconds_for()` /
`note_transfer()` and a head position (`head_lba_`, with a `kHeadUnknown`
cold state):

- **Read-ahead buffer**: `kBufferSectors = 32` (64KB, the conservative/slowest
  end of the period range). A request starting inside `[head - 32, head]` pays
  no access time at all.
- **Distance-dependent seek**: `kSeekMinSec + (kAccessSec - kSeekMinSec) * 3 *
  (distance / capacity)`, with `kSeekMinSec = 0.08` (period 2x drives quote
  80-150 ms for a short/single-track access; the low end taken). Calibrated so
  a one-third-stroke seek costs exactly the published `kAccessSec` = 250 ms,
  making a full-stroke seek 590 ms, in line with period 2x full-stroke
  figures of ~400-600 ms.
- **Cold drive** (post-reset, or media just changed) pays the full published
  average, because a real drive has to find its place.
- `cmd_seek10()` uses the same model, so an explicit SEEK costs the real
  distance and leaves the head (and buffer) where a following READ can use it.

The realism guard `Read10IsPacedToRealTwoSpeedDriveTiming`, which asserts a
one-sector read cannot complete faster than real hardware could deliver it,
reads on a cold drive. `ReadAheadBufferServesAShortBackwardsReReadWithNoSeek`
(a read 13 sectors behind the head pays no seek) and
`SeekCostGrowsWithDistanceAndStillRespectsTheShortSeekFloor` (a short seek
costs strictly less than a long one, strictly more than the 80 ms floor, and
strictly less than the 250 ms average; a near-full-stroke seek costs more than
the average) cover the model.

### 5.8 Cost, measured

These numbers are measured. They come from a full-set run, 205 packages.
The installer's default package answer selects the **full** set
(`fdplfull.lst`, 205 packages), because every `FDASK` question's highlighted
default is already the wanted answer and the tool presses Enter. §19.5
covers the Base set the shipped image carries.

With a flat 250 ms access charge on every non-contiguous read, every package
costs ~2.2 billion cycles (~33 s of emulated time) regardless of size, and a
run exhausts a 200-billion-cycle budget at package 150 of 205. With §5.7's
read-ahead and distance model, the same packages at the same points in the
same flow:

| package | flat access charge | read-ahead and distance model | ratio |
| --- | --- | --- | --- |
| `util\fdnpkg` | 190.6e9 | 72.0e9 | 2.65x |
| `util\v8power` | 193.4e9 | 73.2e9 | 2.64x |
| `apps\fdimples` | 197.9e9 | 74.4e9 | 2.66x |

That is a consistent **2.65x reduction in total elapsed cycles**, from the
pacing model alone: same installer, same media, same dialogs.

Measured for a complete run:

- **Boot, partition, self-reboot, format and package discovery**: ~22.4
  billion cycles. (The flat model: ~50 billion.)
- **Package install**: ~0.70 billion cycles per package averaged over the 205
  packages, i.e. ~10.6 s of emulated time each.
- **Total to the installer's own completion screen**: 317.5 billion
  cycles, about 80 minutes of emulated machine time at 66 MHz, in
  40 minutes of host wall clock (this emulator sustains roughly
  80-150 million cycles per host second on this hardware, so it runs faster
  than the real machine even though nothing about the machine is sped up).

The run is dominated by genuinely-paced device work rather than CPU: CD
reads at a real 2x drive's 307,200 bytes/sec with real distance-dependent
access time (§3.2, §5.7), HDD writes at `wd1003`'s rate, and the `vchoice` /
`vdelay` waits the installer itself performs. The tool's default budget is
400 billion cycles, roughly 2.4x the measured requirement, so a slower host or
a package-set change has headroom before it trips the failure path.

This is a build-time asset generator, not the shipped emulator, so none of
the machine is sped up and none of it needs to be. §5.7's model exists
because a flat charge makes the drive *slower* than the hardware it models,
not to make the build faster.

### 5.9 The installed system's own boot menu selects a V86 memory manager

FreeDOS's installer writes its own FDCONFIG.SYS boot menu, and the default
entry loads JemmEx, a V86 monitor. §19 implements V86 mode, so the installed
system boots its own default entry, and §27 covers the plain MS-DOS config
the shipped image carries. This section records why the installer cannot be
steered away from JemmEx and what JemmEx demands of the CPU.

The installer reporting success is necessary but not sufficient.
`make hdd-image` completes and produces a byte-exact 528,482,304-byte image
with a real MBR and a full 250MB FreeDOS install (the 504MB geometry; §3.1
has the shipped AC2250's), and `make hdd-boot-check` boots it. A CPU without
V86 mode freezes on

```
JemmEx v5.79 [02/02/20]
```

with a CS:IP hit histogram showing a four-instruction loop across
`0000:0008`-`0000:0017`, i.e. executing the *interrupt vector table* as code.
No opcode fires `on_unimplemented`. It is control flow that went somewhere
that does not exist, which is the signature of a V86 monitor starting up on a
CPU that has no V86 mode. JemmEx's own readme describes what it is: it
provides EMS/UMB plus "VCPI services to allow DOS applications running in
**V86-mode** to switch to protected mode" and a "VDS API ... in V86-mode",
and devotes a whole section to "Emulation of privileged Opcodes". A V86
monitor is the entire design. It builds its descriptor tables, writes CR0,
and jumps into what it believes is V86 mode. JemmEx cannot detect a missing
V86, because every real 486 has it, so it has no reason to check.

**Why the installer cannot be steered away from it.** The installer's own
scripts show it:

- `fdins900.bat` chooses its CONFIG.SYS template purely from CPU/environment
  detection: `vinfo /m`, then `FEXT=386`, `if errorlevel 4 set FEXT=486`, and so
  on, with separate branches for DosBox/QEMU/VirtualBox/VMware. It then does
  `if not exist %FINSP%\CONFIG.%FEXT% set FEXT=DEF`. There is no `CONFIG.486`
  (nor `CONFIG.EMU`) on the media, only `CONFIG.DEF`, `.286`, `.186`, `.086`,
  `.DBX`, `.VBX`, so a 486 always lands on **CONFIG.DEF**.
- `CONFIG.DEF` is the 386+ template, and it **hardcodes** `MENUDEFAULT=2,5`
  rather than using the `$FDEFMENU$` placeholder that `fdins900.bat` substitutes
  for other templates. (`CONFIG.286`, by contrast, defaults to `FDXMS286.SYS`
  and never loads a V86 monitor at all, but claiming to be a 286 to get it
  would mean lying about the CPU.)
- No `FDASK` question anywhere asks about memory management: every
  `fdask*.bat` and `en/fdsetup.def` has no jemm/ems/xms/memory question. The
  questions are keyboard layout, target directory, config-file handling,
  package set, boot sector and backups.

So no answer to the unmodified installer produces a JEMM-free configuration.
FreeDOS's own installed menu does contain an entry for hardware where the
memory manager will not work, entry 4, "Load FreeDOS low with some drivers
(Safe Mode)", which loads only `HIMEMX.EXE` (`34?DEVICE=...HIMEMX.EXE`) and
no JEMM. HIMEMX is what the installer's *own* boot floppy loads (§5.3, §5.6).
With V86 mode in the core (§19), the unmodified default entry boots, so the
installer's own output boots without a patch.

### 5.10 The finished image boots

`make hdd-boot-check` (`disks/hdd_boot_check.cpp`) is the actual bar for done,
rather than trusting the installer's own completion message. It cold-boots the
machine with **nothing but the finished HDD image mounted**, no floppy, no CD,
so the BIOS has to fall through drive A: to drive C: via the boot sequence
`configure_factory_cmos()` seeds, and it requires a `C:\>` prompt that has sat
idle (unchanged for ~5 s of emulated time), so a prompt string that merely
flashed past mid-boot cannot satisfy it. It also checks the image is exactly
528,482,304 bytes and carries a `55 AA` boot signature up front, so a bad
image fails loudly instead of after a whole cycle budget of nothing.

Result on the 504MB-geometry `disks/freedos-hdd.img` this section was
measured against:

```
OK: reached an idle C:\> prompt at cycle 1409066356
=== screen ===
  Name           Total           Conventional       Upper Memory
  --------  ----------------   ----------------   ----------------
  SYSTEM      68,336   (67K)     68,336   (67K)          0    (0K)
  HIMEMX       2,192    (2K)      2,192    (2K)          0    (0K)
  COMMAND      3,376    (3K)      3,376    (3K)          0    (0K)
  Free       580,048  (566K)    580,048  (566K)          0    (0K)

Physical hardware networking is not supported at this time.

CD-ROM not configured

Done processing startup files C:\FDCONFIG.SYS and C:\FDAUTO.BAT

Type HELP to get support on commands and navigation.

Welcome to the FreeDOS 1.3 operating system (http://www.freedos.org)

C:\>
```

Everything in that screen is the real software doing what it does on real
hardware, not a stub: `FDCONFIG.SYS` and `FDAUTO.BAT` process, `HIMEMX` loads
and reports its real 2,192-byte footprint, FreeCOM reports genuine
conventional-memory figures for `SYSTEM` (the kernel), `HIMEMX` and `COMMAND`
(itself) with 566K free, "CD-ROM not configured" is correct because this check
deliberately mounts no CD, and the shell's own welcome banner and prompt appear.

That image checks out independently: exactly **528,482,304 bytes**
(1024 x 16 x 63 x 512), a `55 AA` signature at offset 510, and an MBR partition
entry of `80 01 01 00 06 0F 3F 00 3F 00 00 00 C1 BF 0F 00`, bootable flag
`80`, type `06` (FAT16 >32MB), LBA start 63, length `0x000FBFC1` = 1,032,129
sectors, i.e. the whole 504MB disk minus the first track. Read back with
`mtools` it is a genuine FAT16 volume labelled `FREEDOS2022` holding
`KERNEL.SYS`, `COMMAND.COM`, `FDAUTO.BAT`, `FDCONFIG.SYS` and the
`FREEDOS`/`NET`/`GAMES`/`UTIL`/`APPS`/`PGME` trees, with 252MB of its 504MB
still free.

## 6. Protected mode, paging, task switching and the FPU

The real-mode baseline of §4.1 carries a specific list of opcodes as
**documented no-ops**, plus four CR0 bits that are accepted and discarded.
This section describes the full 486 programming model that replaces it,
inside the same two files (`cpu80486.h`/`.cpp`). The bar is the same shape as
"boots the FreeDOS installer to a live `C:\>`": a protected-mode program
actually running, not a passing unit suite (§6.9).

Both bars hold at once. `make pm-check` runs a 32-bit protected-mode program
through paging, a repaired page fault, a hardware task switch, a ring-3
excursion and the FPU; and `make hdd-boot-check` still reaches an idle
`C:\>` **at cycle 1409066356**, the identical figure §5.10 records for the
real-mode core. A real-mode boot that is cycle-for-cycle unchanged is the
strongest available evidence that adding protected mode does not perturb
real mode.

### 6.1 What is real, and what is still a gap

Every item on §4.1's no-op list does its published work:
LGDT/LIDT/SGDT/SIDT (real in both modes, since all four are legal in real
mode and HimemX depends on it), LLDT/LTR/SLDT/STR/VERR/VERW/LAR/LSL/ARPL
(protected-mode only), CLTS, LMSW, MOV to/from CR0-CR3, INVLPG, and the whole
x87 ESC range 0xD8-0xDF.

Three §4.1 decisions are inverted here:

- **SMSW reports ET = 1.** CR0.ET is hardwired to 1 on an Intel486 because
  the FPU is on-die; §4.1's 0 was true for an FPU-less core. `init_state()`
  sets ET and `write_cr0()` refuses to clear it.
- **MOV CR0 honors PE.** It switches mode, both directions.
- **LMSW enters protected mode, and still cannot leave it.** That asymmetry is
  genuine: a 286 could enter protected mode and never exit, and the 386/486
  kept the behavior, so `grp0f01()` re-asserts PE after an LMSW that tries to
  clear it. Leaving needs a MOV to CR0.

The protected-mode-only members of the group are **#UD in real mode**, which
is what Intel documents ("not recognized in Real Address Mode") and what
§4.1's no-ops were standing in for. `LGDT`/`LIDT`/`SGDT`/`SIDT`/`SMSW`/`LMSW`/
`MOV CRn` stay legal in real mode, because FreeDOS 1.3's HimemX executes
LGDT plus MOV CR0 in real mode on every XMS block move (§5.4) and making
those fault would break the boot outright.
`SgdtLgdtAndTheControlRegistersStayLegalInRealMode` pins that split down.

Still absent, the complete list:

- **Task-gate entry to Virtual-8086 mode.** V86 mode itself is implemented
  (§19), and that one narrow form stays a declared gap (§19.1). JemmEx is a
  V86 monitor, so §19's V86 is what lets FreeDOS's own default configuration
  run, not only a DOS extender.
- **Debug and test registers.** DR0-DR7 hardware breakpoints are §46.2.
  TR3-TR7 read 0, because there is no cache model to test and the TLB model
  here is not the silicon's structure.
- **Alignment-check faults (#AC)** are §46.1. EFLAGS.AC and CR0.AM are real
  storage (§4.4).
- **CR0.NE = 0 does not raise an external interrupt.** With NE clear a real
  486 asserts FERR# and a period board routes it to IRQ13; this chipset has
  no FERR# line, so an unmasked FPU exception is latched in the status word
  and reported only when NE is set (§6.7). Wiring FERR# is chipset work, not
  CPU work.

### 6.2 Segmentation: descriptor caches, and why real mode has no limit check here

Each of the six segment registers has a `SegDesc`, the "hidden" half real
silicon loads from the descriptor and keeps using until the selector is loaded
again. That structure, not the selector value, is what every access consults.

**Real mode derives the base from the selector, and enforces no limit at
all in this section's model (§44.5 adds the cached-limit check).** This is
the §4.3/§5.4 decision: `seg_linear()` returns `base + off` with no check when
`!protected_mode()`, so a 32-bit effective address in real mode still reaches
the bus untruncated, which is what makes HimemX's unreal-mode block moves
work. `code32()` and `stack32()` likewise consult `protected_mode()` before
reading the D/B bits, so real mode is unconditionally 16-bit-default no matter
what a previous protected-mode excursion cached. Those two facts together are
why the real-mode boot is cycle-identical.

**The cache surviving a mode change is the mechanism.** `write_cr0()` reloads *nothing* on either
PE transition. That single fact makes both directions work: on the way in, the
instructions between `MOV CR0` and the far JMP run out of the old cached CS;
on the way out, a large limit loaded in protected mode survives, so the next
real-mode segment load sets the base and leaves the limit alone. That is the
actual hardware behavior, and
`ABigLimitLoadedInProtectedModeSurvivesIntoRealModeAsUnrealMode` tests the
mechanism rather than the side effect.

**One compromise.** The selector fields (`cs`/`ds`/...) are public, and the
embedding chipset and the tests assign them directly. `refresh_real_bases()`, called at the
top of each real-mode `step()`, treats a field that no longer matches the
selector its cache was loaded from as the segment load it is, and re-derives
the base while keeping the limit. Comparing against the recorded selector is
what distinguishes "software wrote DS" from "DS still holds what a
protected-mode descriptor load put there", so a protected-mode *base* as well
as a limit survives into real mode. In protected mode there is no such sweep:
assigning a selector field directly there desynchronizes the cache, and the
header says so.

The full protection rule set is implemented and tested per rule rather than in
aggregate: expand-down limits inverting the check, null selectors legal in
DS/ES/FS/GS but never SS and faulting only on use, SS demanding a writable
segment whose RPL *and* DPL both equal CPL, execute-only code segments
refused at load time, conforming code segments reachable from outside and
keeping the caller's CPL, non-conforming ones demanding an exact match, the
descriptor Accessed bit written on a successful load, and the outward-return
sweep that nulls any segment register the outer level may not keep.

### 6.3 Paging: a correctness-first TLB, and CR0.WP as a genuine 486 addition

`translate()` is the two-level walk: CR3 to page directory to page table, with
U/S and R/W ANDed across the two levels, A set on both entries and D set on
the table entry for a write, CR2 loaded with the faulting linear address, and
a #PF error code carrying the present / write / user bits.

**CR0.WP is implemented because it is a real 486 addition, not a Pentium
one.** With WP clear, the only behavior a 386 had, a supervisor write
bypasses a page's R/W bit entirely, which is precisely why copy-on-write was
impossible before WP existed. Both halves are tested
(`SupervisorWriteToAReadOnlyPageSucceedsUntilWriteProtectIsSet`), and a WP
change flushes the TLB.

**The TLB is a 64-entry direct-mapped model, and that choice is labelled.** A
real 486 has a 32-entry 4-way set-associative TLB. What is architecturally
visible to software is not the associativity but the contract: a translation
may be cached, a CR3 write flushes everything, and INVLPG drops one page.
This model honors exactly that contract, and CLAUDE.md's "correctness over
speed" is why it is a simple direct map rather than a structural imitation.
`InvlpgDropsOneTranslationAndACr3WriteDropsThemAll` tests the *stale* case on
purpose, repointing a page table behind the TLB's back and confirming the
old translation is still used until INVLPG runs, because that is the
behavior software has to program around, and a core that "helpfully" noticed
the change would be the wrong one.

Paging requires protection: setting CR0.PG with PE clear is a #GP(0), not a
silently dropped bit, and `EnablingPagingWithoutProtectionIsAGeneralProtectionFault`
pins it down.

A 16-, 32- or 64-bit access that straddles a page or segment boundary
translates *every* byte before storing any of them, so a fault cannot leave
half a write behind. That is what makes a #PF restartable, and §6.9's stub
depends on it working: its handler repairs the mapping and IRETDs, and the
faulting instruction runs again and succeeds.

### 6.4 A fault is a non-local exit, and the wasm build needs `-fexceptions`

An x86 fault abandons a partially executed instruction and restarts it. That
is not something a return code models well in an interpreter whose accessors
return values inline in expressions, so `raise()` throws a `cpu80486::Fault`
and `step()` catches it. `deliver_fault()` then puts back the instruction's
own EIP, ESP, SS and SS descriptor before vectoring, so the handler sees
exactly what a restart would see; `AFaultRestoresTheStackPointerSoTheInstructionCanRestart`
checks that through the `on_fault` hook, which fires after the restore and
before the handler.

Nested faults escalate the way hardware does: a fault while delivering a fault
becomes #DF, and a fault while delivering *that* is shutdown, modelled as a
halt until reset. `AnUndeliverableFaultEscalatesToDoubleFaultThenShutdown`
covers the whole chain, and the §6.9 stub's harness treats `halted` as
*insufficient* evidence of success for exactly this reason, a shutdown sets
it too.

**This needs one build setting outside the CPU core.** Emscripten disables
exception catching by default, which turns every `throw` into an abort, so
`web/Makefile` passes `-fexceptions`. Without it the first page fault a DOS
extender takes would kill the whole wasm module instead of reaching its
handler. The flag is commented at its one call site with that reasoning. It
costs real code size: the exception tables plus the protected-mode code take
`pc486.wasm` from about 102KB to about 189KB, the price of the module
surviving a fault at all.

`Cpu::interrupt()`, the entry point `machine.cpp` calls from its instruction
loop, also catches. Delivery of a hardware interrupt can itself fault on a
missing or malformed gate, and that has to become a #GP/#DF rather than an
exception escaping into the chipset, which has no idea what a `Fault` is. A
test drove this one out (§6.10).

### 6.5 CPL, the TLB and CR0.WP, and the A20 gate in the protected-mode stub

Three details of the protected-mode core are pinned by evidence from real
guest code, not by review.

**CPL is not the low two bits of CS.** `make hdd-boot-check` reaches an idle
prompt only if HimemX's first unreal-mode excursion works. §5.4 records the
routine: `LGDT`, set CR0.PE, then `MOV DS,DX` with DX = 0008h, running from
real-mode CS = **0291h**. Computing CPL as `protected_mode() ? (cs & 3) : 0`,
the identity everyone quotes, gives ring 1 for 0291h (low two bits `01`), and
loading a DPL-0 data selector is then #GP(8).

The identity is real but conditional: it holds because a *protected-mode* CS
load always sets RPL = CPL, an invariant real mode does not maintain. CPL
lives in an internal register written from the code segment's descriptor on
every CS load, and a real-mode CS load sets it to 0 whatever the segment
value happens to be. That is why this trick works on every real 486, and it
has to, because essentially every period DOS memory manager does it from an
arbitrary CS. The core keeps an explicit `cpl_` field written by every CS
load and by nothing else, with the reasoning recorded at `cpl()`.

The protected-mode stub cannot catch a mistake here: its real-mode CS is
1000h, whose low two bits are `00`, so CPL is 0 either way. Only a real guest
with a real segment value surfaces it, the same lesson §5.4 draws.

**The TLB fast path honors CR0.WP's supervisor exemption.** The walk applies
the rule: a supervisor write to a read-only page succeeds unless WP is set
(`SupervisorWriteToAReadOnlyPageSucceedsUntilWriteProtectIsSet`). The TLB-hit
path applies the identical rule. If the hit path checked only
`write && !writable`, a multi-byte supervisor write would succeed on its
first byte, which walks and caches the entry, and fault on its second, which
hits the cache.

**The stub opens the A20 gate before touching extended memory.** While the
gate is closed the motherboard masks every address to 20 bits, and a store
and its read-back agree with each other at the wrong address. That is the
chipset's job, as `cpu80486.h`'s header says. The gate is open out of reset
(§53) and POST closes it, so real software opens it again through the 8042
before touching extended memory, and the stub does the same
(`mov al,0D1h / out 64h,al / mov al,0DFh / out 60h,al`), one more
period-accurate step it exercises. A guest-visible read-back is not proof,
because a wrong address is self-consistent, so every paging and
extended-memory check in the stub also verifies the *physical* frame.

### 6.6 Protected-mode interrupts, gates and task switching

The IDT holds gates, not vectors, and all five gate types work: 16- and 32-bit
interrupt gates (which clear IF) and trap gates (which do not), plus task
gates. An interrupt gate's DPL is checked against CPL for a software `INT n`
and deliberately **not** for a hardware interrupt or an exception, the
exemption exists so a user program cannot simulate a device interrupt, and
`SoftwareIntChecksTheGateDplButAHardwareInterruptDoesNot` tests both halves
against the same vector.

Inter-privilege entry takes SS and ESP from the TSS slot for the target ring
and pushes a five-dword frame (EIP, CS, EFLAGS, ESP, SS) so IRETD can restore
the interrupted stack; the error code, where the exception has one, is pushed
last. Call gates do the same for a CALL, including copying the gate's declared
parameter count from the old stack to the new one, and a JMP through a call
gate is refused if it would change privilege, only a CALL leaves a way back.

Task switching is a real hardware task switch: the outgoing register file is
written into the outgoing TSS, busy bits and the back-link are maintained
(a JMP hands over and clears the outgoing busy bit; a CALL or a task-gate
interrupt nests, sets the back-link and EFLAGS.NT; IRETD with NT set returns
through the back-link), CR3 is reloaded per task, CR0.TS is set so the first
FPU instruction in the new task traps #NM, and LDTR is loaded before the
segment registers because their selectors may be LDT selectors. Both 32-bit
(104-byte) and 16-bit (44-byte) TSS layouts are handled.

I/O privilege is real too: IN/OUT/INS/OUTS and CLI/STI are permitted at
CPL <= IOPL, and above that the TSS's I/O permission bitmap decides one port
at a time, where a *set* bit denies. HLT and the control registers are ring 0
only. POPF and IRET silently decline to change IOPL outside ring 0, or IF above
IOPL, rather than faulting, which is the documented behavior and an easy
thing to get wrong in the direction of a spurious #GP.

### 6.7 The FPU: 80-bit storage, and the one labelled precision departure

The register file holds the real 80-bit extended format (`Float80`: a 64-bit
significand with an explicit integer bit, plus a sign/exponent word), not a
host float. That is the load-bearing choice: `FLD tbyte` / `FSTP tbyte`
round-trip bit-exactly for denormals, infinities and NaN *payloads*, and two
tests use patterns (a QNaN carrying `C123456789ABCDEF`, a denormal with only
the lowest significand bit set) that no host `double` could carry through.
FCHS and FABS are bit operations on the sign field for the same reason, so
they work on a NaN exactly as hardware does.

The status, control and tag words are all real: TOP in status bits 11-13 (so
FNSTSW is how software reads the stack pointer at all), the six exception
flags with their masks, SF qualifying IE for a stack fault with C1
distinguishing overflow from underflow, C3/C2/C0 as a three-way compare
result and as FXAM's class code, and the precision- and rounding-control
fields.

**The one departure, stated rather than implied.** Arithmetic converts to the
host's `long double` and back. On x86-64 that *is* the 80-bit format and the
result is bit-exact; on arm64 and in a wasm build `long double` is a wider
IEEE binary128, so computing there and storing to 80 bits double-rounds, which
can differ from hardware's single rounding in the last significand bit for a
small fraction of operands. Two things narrow it: precision control is applied
explicitly on every result, so a program that selects single or double
precision gets exactly the width it asked for (tested), and rounding control
is honored explicitly where software actually depends on it, the integer
conversions FIST/FISTP/FRNDINT, which is what a C compiler's `(int)` cast
drives. Arithmetic always rounds to nearest, the reset default; that last part
is a documented gap, not a claim.

Three more genuine details worth keeping:

- **The DC and DE encodings reverse the subtract and divide forms.** With
  ST(i) as the destination, `DC E0+i` is FSUBR and `DC E8+i` is FSUB, the
  opposite way round from the D8 forms. Intel documents it, every assembler
  special-cases it, and `TheDcAndDeEncodingsReverseTheSubtractAndDivideForms`
  pins both directions down.
- **The exception report is deferred.** An unmasked exception does not trap
  the instruction that caused it; it surfaces on the next *waiting* FPU
  instruction. That is exactly why the seven "no-wait" encodings (FNCLEX,
  FNINIT, FNSTSW, FNSTCW, FNSTENV, FNSAVE) are specified not to check, a
  handler has to be able to inspect and clear the very error it was called
  for. Implemented and tested as a sequence.
- **FENI, FDISI and FSETPM are genuine no-ops on this part**, not gaps: 8087
  interrupt controls and the 287's protected-mode hint, which the 387 and 486
  ignore.

CR0.EM and CR0.TS both route ESC opcodes to #NM, and CR0.MP gates WAIT on TS
separately, MP exists precisely so the two can be trapped independently, and
the test checks that TS alone leaves WAIT alone.

### 6.8 Cycle costs for the new instructions

Same discipline as §4.6, with the same distinction between a cited number and
a labelled model, and the same reason for caring: the FPU cost spread is an
order of magnitude (FDIV 73 against FADD 8, FSQRT 83), so a flat per-ESC
figure would make period floating-point benchmarks read as wrongly as the
undercosted integer MUL/DIV did on ibmpc-at.

Directly charged published figures: the descriptor-table and task-management
instructions keep the costs §4.6 already carried; the protected-mode rows of
the far control transfers (JMP far 19, CALL far 20, RET far 13 within a
privilege level and 17 across one, IRET 36) against their real-mode rows (17,
18, 13, 15); and the x87 rows.

Explicitly labelled as this core's readings rather than citations:

- **Where an x87 row is a range, the floor is charged and the
  data-dependence is not modelled.** This is a deliberate departure from
  §4.6's four fitted models: there, Intel documents the *mechanism* behind
  each range (early-out multiply, bit-at-a-time scan) so a curve can be
  fitted to both endpoints. For the x87 ranges no such mechanism is
  published, and inventing a curve would be less honest than charging the
  published minimum and saying so.
- **The integer-operand arithmetic forms** (FIADD/FISUB/FIMUL/FIDIV/FICOM)
  are charged as the operation's published figure plus FILD's, a stated
  composition of two published numbers, not a citation of the combined row.
- **Call gates and task switches** get one figure each (35/32 for a gate,
  199 for a switch) covering every sub-case the hardware distinguishes.
- **Fault delivery** is 26 in real mode (INT3's figure, the same one
  `interrupt()` uses) and 44 in protected mode (the published same-privilege
  gate row); the higher inter-privilege figure is not separated.

The arithmetic ESC opcodes charge the published figure alone, with no
memory-load cost added on top: the table gives one number for "FADD ST(i),ST
/ m32real / m64real", so the load is already inside it, exactly as it is for
the integer MUL where memory and register operands cost the same.

### 6.9 The end-to-end proof

`pm_stub_check.cpp` (`make pm-check`) answers "does protected mode actually
work?", and it is deliberately a *program* rather than a test fixture: it
assembles, byte by byte, the smallest thing that does what a DOS4GW-class
extender does at startup, and runs it against the real `pc486::Chipset`, the
same 32MB memory map and bus the browser build uses. Each step leaves an
observable value in a results block, and `main()` checks all of them, so every
value asserted is one the *guest program* computed.

What it exercises, in order: the canonical real-mode entry sequence (open the
A20 gate through the 8042, LGDT, LIDT, set CR0.PE, far-JMP into a 32-bit flat
code segment); 32-bit arithmetic and a store to linear 7MB, which no real-mode
addressing form can reach; the FPU on exactly representable values (2.5 * 4.0,
sqrt(16), an integer conversion); paging, writing through a *virtual alias* so
the value only lands correctly if the walk is real; a deliberate page fault
whose handler reads CR2 and the error code, installs a page table, INVLPGs and
IRETDs, so the faulting instruction restarts and succeeds; LTR and a far CALL
to a TSS, with the second task running on its own stack, clobbering EAX and
IRETDing back through the back-link; a hand-built IRETD frame returning
outward to ring 3, a DPL-3 interrupt gate switching to the ring-0 stack out of
the TSS and back out again; and finally clearing CR0.PG, dropping to a 16-bit
code segment, clearing CR0.PE and far-JMPing back to a real-mode CS.

Two harness details that matter. `halted` is **not** accepted as evidence of
success, because an undeliverable fault escalates to #DF and then to shutdown,
which sets it too (§6.4), the run must also take exactly one fault, the
deliberate #PF. And every paging and extended-memory result is checked against
the *physical* frame as well as read back through the guest, for the A20
reason in §6.5.

It exits non-zero on any failed check, so it is a regression test as well as a
demonstration, and it is fast (no images, no BIOS), so unlike
`hdd-boot-check` it is part of `make check`.

Current result: 24 of 24 checks pass.

### 6.10 Tests

`ctest -N` lists the current cases. `tests/cpu80486_test.cpp` has four
fixtures and `tests/cpu80486_timing_test.cpp` covers the cycle costs.

- **`Cpu80486Test`**: the real-mode coverage §4.7 describes, plus eight
  cases for the instructions §4.1 carried as no-ops, LGDT/LIDT actually
  loading the registers (including the 16-bit
  form's 24-bit base truncation against the 32-bit form's full four bytes),
  SGDT/SIDT storing them back, SMSW reporting hardwired ET, LMSW's
  enter-but-never-leave asymmetry, MOV CR0 both directions, PG-without-PE
  faulting, CLTS, and the protected-mode-only opcodes taking #UD in real mode
  while the real-mode-legal half stays legal.
- **`Cpu80486PmTest`**: a fixture that enters protected mode by
  *executing the real sequence* rather than poking state, so everything it
  observes is reached the way software reaches it. Covers CS.D driving the
  operand-size default and the prefix toggling it, limit and access-rights
  faults one rule at a time, null selectors, far transfers including
  conforming segments and call gates, the LDT, VERR/VERW/LAR/LSL reporting
  through ZF instead of faulting, ARPL, every gate type, inter-privilege
  entry and IRETD's outward return, the #DF escalation chain, LTR and both
  task-switch flavors, the I/O permission bitmap, and the return to real mode
  with unreal mode intact.
- **`Cpu80486PagingTest`**: translation through a virtual alias, #PF error
  codes discriminated bit by bit, CR0.WP's supervisor exemption both ways, A
  and D bits, the stale-TLB contract with INVLPG, and a user access to a
  supervisor page.
- **`Cpu80486FpuTest`**: the 80-bit round-trips, all three real and all
  three integer widths, exact arithmetic including the reversed DC/DE forms,
  precision and rounding control, the compare and FXAM condition codes,
  ordered vs unordered compares, stack faults in both directions, the control
  and status words through memory and AX, FSAVE/FRSTOR and FSTENV, packed
  decimal, the transcendentals on exact values, #NM from EM and TS, WAIT
  gated by MP, and the deferred exception report as a sequence.
- **`Cpu80486PmTimingTest`**: the protected-mode-only opcode costs, the
  protected-mode rows of the far control transfers, the task-switch figure,
  and protected-mode fault delivery, all of which need a fixture that is
  actually in protected mode.

Four traps in writing these tests are worth keeping, because anyone would fall
into three of them again: `D8 /2` is FCOM **m32real** where `DC /2` is the
m64real form, so pointing D8 at a double silently compares the low half as a
float; an inter-privilege call gate needs LTR to have run, or it is a #TS
rather than a transfer; an outward IRETD nulls the data segment registers, so
ring-3 code has to reload DS before it can address anything; and a fixture
index past the end of the 1MB array is undefined behavior, so aliasing checks
stay in bounds.

### 6.11 Sources

- Intel 80486 Programmer's Reference Manual (1990/1992), protection,
  segmentation and descriptor formats, paging and the page-table entry bits,
  interrupt and exception delivery, task switching and the TSS layouts, the
  I/O permission bitmap, CR0 including WP and the FPU control bits, and the
  floating-point chapters (control/status/tag words, per-instruction
  behavior). The specific section is named inline at each rule.
- Intel AP-485, *Intel Processor Identification and the CPUID Instruction*,
  as in §4.8. (Its processor list is also what §13.1 reads the other way:
  the SL-Enhanced IntelDX2 has CPUID, and this core implements it.)
- Quantasm, *80x86 Integer Instruction Set (8088 - Pentium)*, the 486
  column, including its separate protected-mode rows for the far control
  transfers and its x87 rows.
  ([mirror](https://www2.math.uni-wuppertal.de/~fpf/Uebungen/GdR-SS02/opcode_i.html))

## 7. VGA mode 13h, the DAC, and the VESA BIOS Extensions

The CPU in §4-§6 runs a DOS game's code. This section covers the screen that
code draws on: the 256-colour modes period software actually uses, the real
palette hardware behind them, and the VESA interface a program asks for them
through.

The bar is the same shape as §5's "boots the FreeDOS installer to a live
`C:\>`" and §6.9's protected-mode stub: not a passing unit suite, but a
program the machine actually boots, whose pixels are then checked one by
one. `make vbe-check` is that proof, and it passes with **zero failures**
against the real, unmodified BIOS and video BIOS.

The class is called `Ega` for continuity, but its own header comment says it
represents the VGA card; §7.1-§7.3 are the parts of that card beyond EGA.

### 7.1 Chain 4, and why mode 13h "looks linear" on planar hardware

Software sees mode 13h as a flat 64,000-byte frame buffer at `A000:0000`,
one byte per pixel. The hardware underneath is the same four-plane VRAM
every other EGA/VGA mode uses. Chain 4 (Sequencer Memory Mode, SR04 bit 3)
is what reconciles the two: it makes CPU address bits 0-1 the plane select
and drops them from the per-plane offset, so four consecutive CPU bytes are
four *different* planes at one offset, and every plane contributes one
pixel per memory cycle instead of one bit.

This file's VRAM was already stored the way the real part is wired,
byte-interleaved, `vram[(plane_offset << 2) + plane]` (ibmpc-at's
IBM_PCAT_REVIEW.md §12, where that engine was built). Chain-4's
decode against that layout collapses to the identity:

```
((off >> 2) << 2) + (off & 3)  ==  off
```

so `mem_read`/`mem_write` in a chain-4 mode reach `vram[offset]` exactly.
That is not a shortcut taken to save work, it is the reason mode 13h
looks linear on planar silicon, and it falls out of the layout rather than
being asserted on top of it.

Two details are easy to get wrong and are tested directly:

- **Map Mask still applies.** Chain 4 changes *which* plane an address
  reaches; it does not bypass the Sequencer's plane-enable wires. A mode-13h
  driver that narrows Map Mask genuinely stops some pixels landing, which is
  the same silicon "mode X" planar tricks are built on.
- **Chain 4 outranks odd/even chaining.** Both bits set is a nonsense
  combination software can still program, and the real part resolves it in
  favour of chain-4's 2-bit plane select.

### 7.2 The DAC (0x3C6-0x3C9), and the one number that matters

256 colour registers, three channels each, **six** significant bits per
channel, the top two are not wired, which is why 8-bit-minded code gets a
washed-out picture on real hardware rather than an error. Implemented as
the real part behaves, not as a convenience table:

- Separate **read and write index registers**, each with its own R→G→B
  sub-counter that auto-advances to the next entry on every third access.
  That is how period code loads a whole palette with one `OUT` to 0x3C8 and
  768 more to 0x3C9, and keeping the two sides independent means a driver
  reading one entry part-way through writing another corrupts neither.
- The **DAC State register** (0x3C7 read) reports which side was addressed
  last: 3 = read mode, 0 = write mode.
- The **PEL Mask** (0x3C6) is ANDed with the pixel value *between the shift
  registers and the DAC's address lines*, it does not alter a single
  stored colour, which is exactly why period code uses it for fades and
  16-colour-in-256-mode tricks. The renderer applies it there too, rather
  than merely storing it.

Scaling 6 bits to the renderer's 8 uses `(v << 2) | (v >> 4)`, which maps
0→0 and 63→255 exactly. A real DAC drives a full-scale analog ramp, so 63
*is* maximum brightness; the plain `v << 2` tops out at
252 and quietly darkens every white on screen. This is the same full-scale
convention `DecodeEgaColor` already used for its 2-bit channels.

### 7.3 Resolution comes from the registers, not from the mode number

The renderer never learns it is "in mode 13h". It reads the same registers
a real CRT controller reads, exactly as ibmpc-at's IBM_PCAT_REVIEW.md §16
established for mode 10h. The
values below are not guesses about what mode 13h ought to program, they
are what this machine's own BIOS was observed to write, read back off the
live card by `vbe_mode13_check`:

```
CRTC R01 Horizontal Display End = 79
CRTC R12 Vertical Display End   = 399
CRTC R09 Maximum Scan Line      = 1 (scan doubling off)
CRTC R13 Offset                 = 40 -> 320 bytes/scan line (4-byte address unit)
GC   R05 Shift Register field   = 2 (2 = 256-color)
AC   R10 bit 6 (8-bit color)    = 1
```

Read naively that is a **640x400** screen with an **80-byte** scan line,
every one of those three numbers is wrong, and each is corrected by a
different real register:

- **640 → 320.** In a 256-colour mode the VGA spends two dot clocks per
  pixel. The Attribute Controller's "8-bit colour" bit (AR10 bit 6) is how
  the hardware knows, and how `attr_8bit_color()` halves the width. The CRTC
  genuinely is programmed for 640 dots of horizontal timing.
- **400 → 200.** Maximum Scan Line = 1 means two scan lines per row, so the
  400-line raster carries 200 rows. This is a *different* mechanism from
  R09 bit 7's scan doubling, which mode 13h leaves off; both exist and the
  renderer honours both.
- **80 → 320.** The Offset Register is not a byte count. The CRTC's address
  counter is scaled by two register bits, Underline Location (R14) bit 6
  selects **doubleword** mode (4 bytes/unit) and Mode Control (R17) bit 6
  selects **byte** mode (1 byte/unit), with word mode (2) the default.
  Mode 13h sets doubleword, so its Offset of 40 means 40 × 2 × 4 = 320
  bytes. Mode 10h's *identical* Offset of 40 means 40 × 2 × 1 = 80 in byte
  mode. Hardcoding either figure breaks the other mode; `crtc_address_unit_bytes()`
  derives it, and `start_byte_offset()` scales the Start Address the same
  way so a hardware scroll moves by whole scan lines.

`DetectScreenMode` branches on the Graphics Controller Mode register's
Shift Register field being 2, the real CRT controller's own 256-colour
selector, not on the Sequencer's Chain-4 bit, which is an addressing
choice software can make independently of how pixels are shifted out. Field
value 3 is not a mode real VGA silicon defines and still renders as an
honest black frame.

### 7.4 VESA: the card's extension registers, and a labelled substitution

VBE lives in the video card's own ROM. This machine's card ROM is the
freely-licensed VGA BIOS substituted for IBM's still-copyrighted one
(§6, `roms/fetch-bios.sh`), and like every real SVGA card's ROM it
implements VBE by driving a set of **vendor-specific extension registers**
that only it knows about, an index port and a data port exposing a bank
of 16-bit registers. Real cards all worked this way (Tseng's ET4000
extended CRTC set, Cirrus's, S3's).

**This is a departure and is labelled as one**, in `ega.h` and here. The
*shape* is period-correct; the specific register numbers are not any real
1993 card's. They are the interface this substitute firmware expects, and
they exist here only because that firmware is this card's ROM. The card is
a compatible stand-in, not a clone of a named part, the same rule
CLAUDE.md applies to the BIOS and to FreeDOS.

Without those registers the ROM's entire `AH=4Fh` handler is inert: it
gates on the card answering the probe, and a card without them returns from
all four calls with **AX unchanged** (0x4F00, 0x4F01, 0x4F02, 0x4F03), video
memory at 0xA0000 reading 0xFF, and the legacy `AH=0Fh` cross-check still
reporting text mode 3.

Two register semantics carry real weight:

- **The ID register is a probe, not a version field.** Writes stick only
  for revisions this card implements; an unknown revision must *not* stick,
  or the probe wrongly concludes the card speaks it.
- **GETCAPS is a query mode.** See §7.5.

`VideoMemory` reports the card's genuine 1MB and is read-only, software
cannot solder on more RAM.

### 7.5 GETCAPS, and the VESA mode list

The Enable register's GETCAPS bit (0x02) is not a display mode. While it is
set, reading the XRES/YRES/BPP registers reports the card's **maxima**
rather than the current mode's values. The card's ROM uses exactly that to
decide which modes to advertise, in `mode_info_check_mode`:

```
  call dispi_get_max_xres
  cmp  [si+2], ax
  ja   vbe_mode_unsup
  ...
```

`dispi_get_max_xres` sets GETCAPS, reads XRES, and restores Enable. A card
that ignores the bit compares every candidate mode against the *current*
geometry, which is zero on a card no one has programmed yet, so every mode
is rejected, the list comes out empty, and 4F01 fails for every mode number
even though 4F00, 4F02 and 4F03 answer correctly and mode 13h displays.
The gate is the GETCAPS query and not video memory, and not a PCI
linear-framebuffer probe (the ROM does build as the PCI variant). The
firmware's source at the pinned revision shows it in four lines.

With GETCAPS implemented the card advertises exactly the modes that fit its
VRAM and its reported maxima. At a 256KB / 640x400 ceiling that is three,
all 8bpp: **100h** (640x400), **150h** (320x200) and **151h** (320x240).
640x480x256 needs 307,200 bytes and is not offered.

The card carries **1MB**, the realistic amount on a 1993 SVGA board, and
reports maxima of **1024x768**. 1024x768x8 is 786,432 bytes, so the largest
8bpp frame fits. `kVbeMaxBpp` is 8. With no change to the ROM binary the
firmware advertises five more entries from its own built-in table: **101h**
(640x480x8), **102h** (800x600x4), **103h** (800x600x8), **104h**
(1024x768x4) and **105h** (1024x768x8). 1280x1024 stays blocked by xres;
every 15/16/24/32bpp mode stays blocked by bpp.

### 7.5.1 104h: planar banking at bpp=4

The ROM gates advertising only on xres, yres, bpp and capacity
(`mode_info_check_mode`, plus `mode_info_number_of_image_pages` against
VideoMemory), so there is no way to admit 800x600x8 and 1024x768x8 while
excluding the 4bpp entries that come with them, not without patching the
ROM binary.

Of those two, **102h** (800x600x4) is correct for the same reason **104h**
is. For bpp=4 the firmware's `dispi_set_mode` calls
`_biosfn_set_video_mode(0x6A)` and then `vga_compat_setup`, which
reprograms the real CRTC, Sequencer and Graphics Controller, and keeps the
planar engine in charge. bpp other than 4 uses the flat VBE window.

**104h** (1024x768x4) needs 98,304 bytes per plane, past the standard 64KB
aperture. The Bank register is `bank << 16` added to the planar plane offset
(`vbe_planar_banked()` / `vbe_planar_plane_off()`), not a switch to the
linear 8bpp path (`vbe_mode_active()` still requires bpp == 8). A 1MB card
has four such planar banks at 64KB (eight at 32KB). A window address selects
a byte in all four planes (VBE 2.0 planar memory model), so the bank space is
one plane (256KB). The firmware agrees: `mode_info_number_of_image_pages`
shifts the 64KB-unit VRAM count right by 2 for 4bpp before dividing by line
bytes and yres, and its 4F05 (`set_display_window`) writes a bank, reads it
back and fails the call if it differs, so a past-end bank has to be rejected.
The RD/WR
flag bits the firmware's 4F05 path ORs into the Bank register are masked
off on write.

Geometry for a bpp=4 DISPI mode comes from the extension registers, not
the CRTC. `vga_compat_setup` does reprogram the CRTC, but this card's
`crtc_vertical_display_end()` deliberately reads only EGA's 9-bit overflow
(Prince of Persia; see `ega.h`) and cannot express 600 or 768 lines, so
`RenderEgaNative16Screen` trusts DISPI XRES/YRES/VIRT_WIDTH when
`vbe_planar_banked()` is set.

Verified against the pinned firmware source
(`bochs-emu/VGABIOS@12d978bd` `vgabios/vbe.c` `dispi_set_mode` /
`vga_compat_setup`).

### 7.5.2 Departure: 32KB bank granularity

SimCity 2000's VESA title screen shows what a 64KB-only Bank register does:
horizontal content strips separated by black bands, the classic doubled-bank
symptom. The pinned VGABIOS's `4F05` always writes `guest_bank << 1` because
it expects the card's Bank register to step in 32KB units when
`dispi_support_bank_granularity_32k` succeeds (GETCAPS on BANK returns
`VBE_DISPI_BANK_GRANULARITY_32K` in the high byte; mode set ORs the same
bit into Enable). Against a 64KB step the firmware would double the index:
guest bank N would land at `2N * 64KB`. The card advertises the flag,
honours Enable bit `0x10` as a 32KB step (`vbe_bank_bytes()`), and rejects
past-end bank numbers for every depth.

Sources: `dispi_support_bank_granularity_32k` (GETCAPS on BANK, high byte
masked with the 32K flag), `dispi_set_mode` (ORs the flag into Enable),
`dispi_set_bank_farcall`, `vbe_biosfn_display_window_control` and
`vesa_pm_set_window` (all shift the window number left by 1, OR the RD/WR
bits, and the first two read BANK back and compare). ModeInfo reports
WinGranularity and WinSize as 64K. A change of the Enable granularity bit
does not touch BANK: `dispi_set_mode` writes BANK = RW (bank 0) before it
sets the bit, and nothing in `vbe.c` reads BANK after a toggle expecting
a reset. A mode set does not zero BANK on the card; `dispi_set_mode`
writes it itself.

### 7.5.3 DISPI register sources

Sources are the guest VGABIOS `vbe.c`, `vbe.h` and `vbe_display_api.txt`
only.

- **ID.** `vbe_init` writes `VBE_DISPI_ID0`, reads it back and gives up
  VBE if it differs, then writes `VBE_DISPI_ID5`. `vbe.h` lines 275-280
  define B0C0-B0C5 and `vbe_display_api.txt` lines 16-46 list the
  revisions. Unsourced: that a value outside B0C0-B0C5 does not stick.
  Nothing in `vbe.c` writes one.
- **BANK under GETCAPS.** `dispi_support_bank_granularity_32k` sets
  GETCAPS, reads BANK, takes the high byte and ANDs `0x10`. XRES, YRES
  and BPP under GETCAPS: `vbe_display_api.txt` lines 210-213 and
  `mode_info_check_mode`.
- **ENABLE clears VRAM unless NOCLEARMEM.** `vbe_display_api.txt` lines
  206-207 (the flag preserves video memory) and `dispi_set_mode`, which
  ORs it in when 4F02 bit 7 asks for it.
- **Virtual width and height.** ENABLE sets virtual width to XRES
  (`vbe_display_api.txt` 182-184). Virtual height is read-only and is
  recomputed whenever the width is set, to stay within video memory
  (lines 186-190). A write to VIRT_WIDTH while enabled is adjusted the
  same way, and a width whose one line exceeds video memory is clamped
  (line 186, "adjusted if current setting is not possible"). 4F06 returns
  width, `width * bytes per pixel` (width / 8 at 4bpp) and virtual height
  as the maximum scan lines (`vbe.c` `get_logical_scan_line_length`).
  Lines are whole lines in video memory: bytes per pixel is (bpp + 7) / 8,
  and 4bpp counts one plane (a quarter of VRAM), like
  `mode_info_number_of_image_pages`. On the 1MB card that gives 1638 lines
  at 640x480x8, 655 at 800x600x16 and 3276 at 640x480x4. The exact
  rounding and the clamp value are Unsourced.
- **Pan offsets reset on enable.** Unsourced. Neither `vbe.c` nor the API
  text says so, but a previous mode's pan left in place would show a fresh
  mode set shifted.
- **Removed.** Zeroing BANK on enable. `dispi_set_mode` writes BANK itself
  before enabling.

### 7.5.3 The linear frame buffer the ROM advertises

Allegro's `GFX_AUTODETECT` prefers "VESA 2.0 (linear)", and the card ROM
offers one: for every non-planar mode `vbe.c` (VGABIOS@12d978bd, around line
1990) puts `VBE_DISPI_LFB_PHYSICAL_ADDRESS` (`E000_0000h`) in `PhysBasePtr`
and sets mode attribute bit 7. Wing (Allegro 3.9.32, DJGPP, from the Disk
library) takes that path and never checks `set_gfx_mode`'s result, so the
address has to be decoded or every pixel goes nowhere.

`Ega::owns_mem` claims `E000_0000h` + VRAM size, and in an 8bpp DISPI mode
that range is VRAM byte for byte, with no banking. Outside an SVGA mode it
reads open bus and drops writes. `linear_page()` hands the CPU a direct
pointer for it, invalidated by the mapping epoch like the A0000h window.
Tests: `EgaTest.LinearFrameBufferAtE0000000IsTheWholeVramInAnSvgaMode`,
`ChipsetTest.TheVbeLinearFrameBufferIsDecodedAboveRam`.

`Chipset::mem_read`/`mem_write` mask an address to 20 bits while A20 is
off. A real 486's A20M# only forces bit 20, so that mask is too broad above
1MB; it doesn't matter here because a DOS extender turns A20 on first.

### 7.6 The one thing that does not work: 4F01 for mode 13h

`4F01` for mode **13h** returns `AX=0x014F`, function supported, call
failed. That is the firmware, not this card. Its `mode_info_find_mode`
searches `vbe_mode_list`, which holds VESA-defined mode numbers (≥100h)
only; 13h is a VGA mode number and is simply not in the table. The VBE spec
defines function 01h over its own mode numbers, so declining is within
spec, and there is nothing the hardware side can do about it short of
patching a third-party ROM.

Mode 13h is still fully reachable over VESA: `4F02` with `BX=13h` sets it
(the ROM routes a sub-100h mode number to the legacy mode-set path), and
`4F03` reports it back. The harness asserts the `0x014F` answer explicitly
rather than skipping it, so a firmware that starts answering would show up.

### 7.7 SVGA linear modes

Because the card advertises 100h/150h/151h, it can show them. Advertising a
mode the screen cannot display would be a quiet lie. In an SVGA mode the planar engine is out
of the picture entirely: no latches, no Map Mask, no Graphics Controller
ALU. The 64KB aperture at 0xA0000 is a plain window onto a linear
byte-per-pixel frame buffer, slid by the Bank register, and geometry comes
from the extension registers rather than the CRTC (the ROM leaves the CRTC
holding whatever the previous mode left, so reading it there would read
stale values). VirtWidth and the X/Y offset registers pan a wider logical
line, the SVGA equivalent of the CRTC Start Address and how period software
double-buffers.

Leaving an SVGA mode has to hand the memory window back to the planar
engine intact, or every subsequent text/EGA mode reads the wrong bytes;
that round trip is tested directly.

### 7.8 The end-to-end proof

`vbe_mode13_check.cpp` assembles a **445-byte** 16-bit program, writes it
into the first sector of a 1.44MB floppy image with the `0xAA55` signature,
and lets the real BIOS boot it off the real floppy controller, an
ordinary DOS boot disk, nothing injected. Nothing in the results is
asserted by the emulator about itself; every value is what the guest
program computed or what the shared renderer produced.

The guest runs in two phases, parking on a memory poll between them so the
SVGA screen can be captured before the mode-13h set wipes it:

1. `4F00` with a "VBE2"-tagged buffer; `4F01` for 13h and for 100h; `4F02`
   into 100h; `4F03`; palette entries 64-66; a 640x400 pattern; a read-back
   through the window.
2. `4F02` into 13h; `4F03`; legacy `AH=0Fh` cross-check; palette entries
   32-37 loaded through 0x3C8/0x3C9 and entry 32 read straight back through
   0x3C7/0x3C9; a 320x200 pattern; two pixels read back.

Both patterns are corners, a centre point, and a ten-pixel horizontal run,
plus assertions that the pixels immediately around the run are background.
That combination is deliberate: an off-by-one scan-line stride, a wrong
chain-4 decode, or a mis-scaled DAC channel each fail at least one of them
rather than looking close enough.

Results, at the 256KB / three-mode configuration, at cycle 223,000,174 (about 1.4s of host time):

```
  4F00 Return Controller Information -> AX       79           ok
      signature="VESA" version=2.0 total memory=256 KB
      OemStringPtr -> "Bochs VBE (C) 2002-2026 ..."
      VideoModePtr -> 3 mode(s): 100h 150h 151h
  4F01 (mode 13h, a VGA mode number) -> AX       335          ok   (0x014F, see 7.6)
  4F01 Return Mode Information (mode 100h) -> AX 79           ok
      100h: XRes=640 YRes=400 bpp=8 planes=1 model=4 bytes/line=640 winA=A000h
  4F02 Set VBE Mode (BX=13h) -> AX               79           ok
  4F03 current mode -> BX                        19           ok
  legacy INT 10h AH=0Fh cross-check -> AL        19           ok
  DAC State register reads "read mode"           3            ok
  entry 32 red/green/blue read back via 0x3C9    63/21/0      ok
  guest read back A000:0000                      32           ok

  --- rendered mode-13h screen (shared ega_render.cpp) ---
  rendered width / height                        320 / 200    ok
  pixel (0,0)     = palette 32   RGB(255, 85,  0) ok
  pixel (319,0)   = palette 33   RGB(  0,255,  0) ok
  pixel (0,199)   = palette 34   RGB(  0,  0,255) ok
  pixel (319,199) = palette 35   RGB(255,255,255) ok
  pixel (160,100) = palette 36   RGB(130, 65, 32) ok
  ... 10-pixel run on row 50, and its four background neighbours ... ok

ALL CHECKS PASSED (0 failures)
```

`RGB(255,85,0)` from DAC entry 32 = (63,21,0)/63 is the §7.2 full-scale
scaling doing its job end to end: 63→255 and 21→85, through the real port
interface, the real chain-4 memory path and the shared renderer.

The SVGA phase passes identically at 640x400 with palette entries 64-66.

`make check` runs this alongside `pm-check` and the unit suite, so a
regression fails CI rather than being noticed on screen later.

### 7.9 Regression evidence

Mode 13h and the DAC touch the memory path and the port map that every
other mode depends on, so the real-mode boot is checked directly rather than
by inspection:

- **`make hdd-boot-check` reaches a live `C:\>`** on the FreeDOS image, with
  HimemX loaded and 566K free: the full real-mode text-mode boot path.
- **`make pm-check` passes.**

`ega_render_test.cpp` treats Shift Register field 2 as a 256-colour mode that
renders, and field 3, a value real VGA silicon does not define, as
`kUnsupportedGraphics` with a black placeholder frame.

The browser front end needs no per-mode code: it sizes its canvas from
`renderWidth()`/`renderHeight()` each frame, so 320x200 and 640x400 arrive
correctly. There is deliberately no Playwright test for this: the guest
program needs ~223M CPU cycles to reach its pixels, and at this machine's
genuine 66MHz that is real seconds of wall clock that a browser test would
have to sit through at real speed (CLAUDE.md forbids speeding the CPU up
outside a test-only flag this machine does not define). The native harness is
the right home for it.

### 7.10 Tests

- **`ega_test.cpp`**: the DAC's auto-advancing write path and
  independent read index, the 6-bit truncation, the DAC State register, the
  PEL mask's default and round-trip; chain-4's four-bytes-to-four-planes
  decode in both directions, Map Mask still gating it, and its priority over
  odd/even; the extension registers' 16-bit-at-one-address access, the ID
  probe refusing an unimplemented revision, GETCAPS reporting maxima without
  losing the programmed values, the read-only VideoMemory register, the
  clear-on-enable and NoClearMem behaviours, the bank-slid linear window
  (including reading 0xFF past the card's real VRAM), the round trip back
  to planar decoding, and port ownership.
- **`ega_render_test.cpp`**: mode 13h's resolution derived from the
  CRTC *and* the Attribute Controller (proved by clearing the 8-bit-colour
  bit and watching the width become 640), byte-per-pixel decode through the
  live DAC, the doubleword address unit changing the scan-line stride, the
  PEL mask acting between pixel and DAC, the Start Address scrolling by
  whole address units, and the SVGA path's geometry and panning.

Every rendered-pixel assertion is exact. A DAC channel scaling wrong by one
step fails.

### 7.11 Sources

- **IBM VGA hardware documentation**, the Sequencer Memory Mode Chain-4
  bit, the DAC's 6-bit channels, PEL mask and index/state registers, the
  Attribute Controller Mode Control 8-bit-colour bit, and the CRTC address
  scaling from Underline Location bit 6 (doubleword) and Mode Control bit 6
  (byte). Each rule is named inline at its use site.
- **VESA BIOS Extensions (VBE) 2.0**, functions 00h-03h, the VbeInfoBlock
  and ModeInfoBlock layouts, the "VBE2" request tag, and the VGA-vs-VESA
  mode-number split behind §7.6.
- **The card's own ROM source**, `vgabios/vbe.c` at the revision this
  machine's pinned `VGABIOS-lgpl-latest.bin` is built from
  (`bochs-emu/VGABIOS`), cited for what the *firmware* requires of the
  hardware, not as a behavioural model: `mode_info_check_mode` and
  `dispi_get_max_xres/yres/bpp` (§7.5), and `mode_info_find_mode` /
  `vbe_mode_list` (§7.6).

## 8. The browser build keeps up with its own clock

The native proofs are GoogleTest, `pm-check`, `vbe-check` and
`hdd-boot-check`. None of them measures whether the **browser** build can
run this machine at 66 MHz in real time without starving the page of input.
A build that falls behind its clock freezes: keystrokes take seconds to
register or vanish entirely, with the tab pegged near 100% CPU. This
section covers the requirement and how the build meets it.

### 8.1 The requirement: the interpreter out-runs the machine it emulates

Measured in headless Chromium, at an idle FreeDOS `C:\>` prompt, by calling
`machine.runCycles(20_000_000)` directly and timing it, the baseline before
§8.3's work:

```
338 ms per 20M cycles  ->  59.2 M cycles/sec
```

The machine's own clock is **66.0 M cycles/sec**. An interpreter at 90% of
the speed of the machine it emulates can never catch up.

That single fact is why the failure mode is *freezing* rather than just
*running slow*. A frame loop that asks for `dtSeconds * 66e6` cycles once
per animation frame, with `dtSeconds` clamped to 0.25 s, and runs them in
one synchronous `runCycles()` call settles into a stable, self-sustaining
loop once the machine falls behind:

- a frame asks for the clamped maximum, 16.5M cycles,
- at 59 M/sec that call takes ~280 ms, during which the main thread
  dispatches **nothing**, no `keydown`, no `click`, not even the next
  `requestAnimationFrame` callback,
- so the next frame's `dtSeconds` is ~280 ms, clamps to 0.25 s again, and
  asks for the maximum again.

Instrumenting `requestAnimationFrame` during a real boot with that loop
shows exactly that shape, with the cost inside `runCycles`, not in
rendering, audio, or heap growth:

```
frames: 253 over 25s
frame callback duration  p50 5.6   p90 313.8   p99 456.6   max 461.0 ms
total ms:  runCycles 23083   renderFrame 99   canvas 16   audio 3
```

23.1 seconds of a 25-second run inside `runCycles`, in ~300 ms
uninterruptible blocks arriving back to back. The per-frame stall list shows
them every ~300 ms continuously after the 8-second mark, i.e. a 0% idle duty
cycle. A keystroke landing in that window waits for the current block to
finish; one landing while the 8042's single-byte output register is still
full is simply lost (§31 of ibmpc-at's review documents that register's real
behaviour).

Three other candidate explanations are not the cause:

- **wasm heap growth** (`-sALLOW_MEMORY_GROWTH=1`): the per-frame
  breakdown above accounts for essentially all of the stall time inside
  `runCycles`, leaving nothing for a growth-and-copy pause. Heap growth
  does cause a real stall on this machine, but at page load and power-on
  only (§8.5).
- **`ctx.createImageData()` allocating a fresh ~1MB object every frame**:
  16 ms total across a 25-second run (0.06 ms/frame), which is noise.
- **a protected-mode fault firing periodically in real mode**: the stalls
  are not periodic at all once measured properly. They are continuous.

### 8.2 Where the time goes

Profiled natively (`sample`, `-O2 -g`, same FreeDOS image booted to the same
idle prompt). Top of stack, 5134 samples:

```
cpu80486::Cpu::step_inner()          1070
cpu80486::Cpu::translate()            553
pc486::Pic8259::highest_pending()     435   <-- not CPU work
cpu80486::Cpu::seg_linear()           426
pc486::Pit8253::tick()                273   <-- not CPU work
std::__function::__func<...>::op()    247   <-- the memory bus
pc486::Chipset::tick()                207   <-- not CPU work
pc486::Fdc765::tick()                 206   <-- not CPU work
pc486::Ega::tick()                    148   <-- not CPU work
pc486::Wd1003::tick()                 125   <-- not CPU work
pc486::AtapiCdrom::tick()             101   <-- not CPU work
pc486::PcSpeaker::update()             82   <-- not CPU work
pc486::Chipset::poll_interrupt()       62   <-- not CPU work
```

**About 36% of all execution time in that profile is chipset bookkeeping,
not 80486 emulation**, plus another ~7% in the `std::function` memory bus.
The cause is structural: `Machine::run_cycles()` calls `chipset.tick()` and
`chipset.poll_interrupt()` after *every single instruction*, and when every
one of those device ticks sits in its own `.cpp` file, each is an
out-of-line cross-translation-unit call whose body is two loads and a
not-taken branch.

### 8.3 Making observation of the chipset cheap

Nothing here changes *when* a device is observed or *what* it reports. The
emulated machine's behaviour is identical, the strongest evidence being that
`hdd-boot-check` reaches its idle `C:\>` prompt at **cycle 1,409,073,889**,
bit for bit the same cycle with and without these changes.

- **`Pic8259::highest_pending()`** is inline in the header. The chip is
  fixed-priority with IR0 highest, so the lowest set bit of `irr_ & ~imr_`
  is exactly `__builtin_ctz`, where an 8-iteration search called (twice, via
  both PICs) after every instruction costs 435 samples.
- **`Machine::run_cycles()`** guards the out-of-line
  `chipset.poll_interrupt()` behind the inline `chipset.has_interrupt()`.
  This is exact, not an approximation: when neither PIC has an unmasked
  pending line, `poll_interrupt()`'s only side effect is `lower(2)` on an
  already-clear master IR2, and if IR2 *were* set, the master would report a
  pending interrupt and the guard would let the call through.
- **`Pit8253::tick()`** memoizes `PIT_HZ / cpu_hz`, a floating-point
  division that would otherwise run 66 million times a second for a
  `cpu_hz` that never changes.
- **`Fdc765::tick`, `Wd1003::tick`, `AtapiCdrom::tick`, `Ega::tick`** live in
  their headers so they inline at the one call site that matters.
- **`cpu80486::Bus`** holds plain function pointers plus a `ctx` pointer,
  built through the `Bus::For(host)` helper. Every guest memory byte the
  interpreter touches crosses that boundary, and a `std::function` there
  costs a second indirect call through its type-erased thunk and blocks
  inlining at every access. Call sites go through
  `Cpu::bus_read`/`bus_write`/... either way.
- **`web/Makefile`: `-O3 -flto`**, not `-O2`. The hot loop is spread across
  `machine.cpp`, `cpu80486.cpp`, `chipset.cpp` and each device's own
  translation unit; without link-time optimization none of the above can
  actually inline across those boundaries. Worth ~10% on its own.

Measured result, same benchmark as §8.1:

|                        | before        | after          |
| ---------------------- | ------------- | -------------- |
| wasm, idle prompt      | 59.2 M cyc/s  | **76.5 M cyc/s** |
| native, idle prompt    | 84.4 M cyc/s  | **103.7 M cyc/s** |

76.5 M/s against a 66.0 M/s requirement: the machine holds real time with
~16% headroom, where before it was 10% short.

### 8.4 Never block the main thread

Raising throughput above 66 M/s is necessary but **not sufficient**. Any
host slower than this one, and any workload heavier than an idle shell,
which includes this machine's entire reason for existing, puts the emulator
right back into the state §8.1 describes. With §8.3's work alone, the boot
sequence (a heavier instruction mix than the idle prompt) still produced
150-350 ms blocks for its first ~15 seconds.

So `app.js`'s main loop is split in two:

- **`pump()`** advances the machine, in chunks bounded by *real wall-clock
  time* (`kChunkMs = 12`), yielding to the event loop between them.
- **`frame()`** (`requestAnimationFrame`) only draws the screen and LEDs.

The yield is a `MessageChannel` message: `queueMicrotask` and
`Promise.resolve()` run the next chunk in the same event-loop turn and
dispatch no input at all, while `setTimeout(0)` is clamped to ~4 ms, which
would itself cap emulated speed.

The chunk budget is a cycle count derived from measured host throughput
(an EWMA seeded at the real 66 MHz rate), so it follows the machine the
page is actually running on. **Cycles over budget are dropped, not
banked**: a host that cannot sustain the requested rate would never work a
backlog off, and each attempt to try is exactly the long, input-starving
call this bound exists to prevent.

That last point is a departure from the real hardware and is recorded as
one: on a host that cannot keep up, emulated time runs slow. It is the
chosen trade, because a 486 running 5% slow is closer to the real article
than a 486 whose keyboard stops answering. On a host that *can* keep up,
nothing is dropped and the clock is exactly real, which is what
`tests/smoke.spec.ts` verifies.

Measured over a 40-second run (2424 frames), with
`PerformanceObserver('longtask')`, which catches main-thread blocking
regardless of which loop caused it:

```
runCycles chunk duration   p50 0.1   p90 0.2   p99 0.7   max 14.0 ms
rAF frame gap              p50 16.6  p99 24.8  max 120.1 ms
long tasks (>50ms)         2 in 40 seconds  (0.5s = 120ms, 20.1s = 65ms)
```

Against the §8.1 numbers (p99 456 ms, max 461 ms, a stall every frame):

| frame-time          | blocking loop | pump/frame split |
| ------------------- | ------------- | ---------------- |
| p50                 | 5.6 ms        | 16.6 ms (a clean 60 Hz) |
| p99                 | 456.6 ms      | **24.8 ms** |
| max                 | 461.0 ms      | **120.1 ms** (page load; see §8.5) |
| blocking > 50 ms    | ~every frame  | **2 in 40 s** |

Emulated speed, measured at the idle prompt after boot settles:
**66.1 M cycles/sec against a real 66.0**, real time exactly. Across a whole
run including boot it averages 63.9 M/s, the shortfall being the boot
sequence, where this host cannot sustain 66 MHz and the pump sheds rather
than freezing.

`tests/keyboard.spec.ts` passes 5/5 against this loop.

### 8.5 Two front-end details: the CD mount and the power-cycle heap

Both show up once the page gets far enough to hit them.

**The CD-ROM mount takes a `Uint8Array`.** `powerOn()` wraps
`firmware.cdrom`, the raw `ArrayBuffer` `fetch()` returns, before
passing it to `mountCdrom()`. embind's `convertJSArrayToNumberVector` reads
`.length`; an `ArrayBuffer` only has `byteLength`, so it converts to an
**empty** vector and the drive comes up with no disc in it, silently. The
HDD path wraps in `new Uint8Array(...)` the same way. In the live page:

```
mountCdrom(new ArrayBuffer(8192)) -> cdromPresent() === false
mountCdrom(new Uint8Array(8192))  -> cdromPresent() === true
```

**A power cycle frees the previous machine's heap.** `powerOff()` sets
`machine = null`, but an embind handle owns a C++ object that outlives the
JS reference, so dropping it frees nothing. Each `powerOn()` builds a
`Machine` holding a C: image and a CD image, and a leaked handle strands that
heap so the next power-on dies trying to grow past it, throwing an uncaught
wasm exception that leaves the page half-powered. `powerOn()` `delete()`s
the previous cycle's handle before building the next one. It does that
there rather than in `powerOff()`, so the handle stays callable while the
machine is off, which is how `tests/boot.spec.ts` observes that power-off
really does stop the cycle counter.

This is also the answer to "is `-sALLOW_MEMORY_GROWTH=1` a problem?": yes,
but at power-on, not per frame. The one >50 ms long task at page load
(120 ms) is wasm instantiation plus mounting the disc images, which is
unavoidable work at that point.

### 8.6 Dirty-page persistence, CRTC-derived frame timing, and the test clock multiplier

- `persistHddIfDirty()` copies and re-persists only the 4KB pages a session
  actually wrote (`wd1003.h`'s `dirty_ranges()`), not the whole C: image
  every 5 s (§21.2).
- `Ega::recompute_timing_()` (`ega.cpp`) derives the frame period and the
  vertical-retrace window from the CRTC's own programmed timing: Horizontal
  Total, Vertical Total (+ Overflow bits), Vertical Retrace Start/End (+
  Overflow bits), the Sequencer's Clocking Mode (dots/char, dot-clock
  divide-by-2), and the Misc Output dot-clock select. Those are the same
  inputs a real CRT controller's scanout counters use. `tick()` (called every
  CPU instruction) only advances a cached credit counter; the division
  happens in `recompute_timing_()`, called solely from the specific
  CRTC/Sequencer/Misc Output write sites in `ega.cpp` that can change the
  answer, from `reset()`, and from `Ega::set_cpu_hz()` (wired into
  `Chipset::set_cpu_hz()`, so front-panel Turbo keeps vertical refresh at the
  same wall-clock rate regardless of the 66/33 MHz CPU speed, because a real
  DX2 Turbo button changes the CPU's clock, not the video card's crystal).
  An unprogrammed or mid-mode-set CRTC (all registers 0, as right after
  `reset()`) would plug into the formula as several hundred kHz; that is
  clamped to a 40-120 Hz plausible range with a 70 Hz fallback (mode 03h's
  own rate) until the guest programs real values. Checked against the
  textbook numbers: mode 03h's real register values (28.322 MHz dot clock,
  9 dots/char, HTotal=0x5F, VTotal=0xBF+Overflow -> 449 lines) land at
  ~70.09 Hz, and a 640x480 VESA mode's (25.175 MHz, 8 dots/char, 525 lines)
  lands at ~59.94 Hz. Both are measured end to end in `ega_test.cpp` by
  ticking and watching port 0x3DA bit 3 toggle, the same way a real "wait
  for vertical retrace" loop would see it.
- **`TEST_CPU_MULTIPLIER = 20` is not achievable.** This host tops out near
  76 M cycles/sec; 20x of 66 MHz would be 1.3 billion. A synchronous loop
  asked for it produces multi-second blocks with **no speed benefit at all**
  (the throughput is host-bound either way), which is what makes
  `keyboard.spec.ts` time out at the full 120 s on a single click. With
  §8.4's chunk budget the excess is shed, so `fast=1` means "run as fast as
  this host can", which is honest and harmless. The suite's wall-clock time
  equals the host ceiling.
- `hdd.spec.ts`'s "shows the factory-default label ... **on first load**"
  asserts on first load, not after a completed boot. FreeDOS writes to C:
  while running `FDAUTO.BAT`, measured at 12.9 s, about 2 s of real time
  *before* the `C:\>` prompt appears at 15.0 s, so after a completed boot
  the drive has been relabelled "saved state (changes from this session)"
  and the assertion would race the 5 s persist timer.

### 8.7 What this says about the test strategy

Every native proof can pass while the delivery vehicle is unusable. The
problem here is not in the emulated hardware.

`tests/smoke.spec.ts` measures sustained cycles/sec at real speed against a
33-99 M/sec tolerance band, which a 59 M/sec build sits inside while being
10% too slow to keep up, and the band says nothing about *how the work is
distributed*. Real-time emulation has two requirements:

1. enough throughput (`smoke.spec.ts` covers this, loosely), and
2. no single synchronous block long enough to starve input.

`tests/smoke.spec.ts` covers (2) with a `longtask` PerformanceObserver over
a real boot, asserting no task after page load exceeds 150 ms.

## 9. BOOM, and a 16-bit double shift above 15

§6.9 and §7.8 hold the machine to programs this repo wrote: `pm_stub_check`'s
hand-assembled DOS4GW-style stub and `vbe_mode13_check`'s hand-assembled
boot sector. This section is the machine held to genuine third-party
protected-mode software: **BOOM**, a DOS Doom source port built with DJGPP,
loaded by the GO32-V2 stub through the **CWSDPMI** DPMI host, running
**FreeDoom** off `C:\GAMES\BOOM\DOOM.WAD` (17,659,828 bytes). That exercises
paging, a DPMI host's own #PF handler, the LDT, ring 3, and mode 13h at
once, none of it written to this emulator's expectations. The core was
exercised with it against a Full-set image; the shipped Base set (§19.5)
does not include it, and `boom-check` and its harness are gone.

CWSDPMI builds its identity-mapping page table with a 32-bit shift helper
whose result depends on how a 16-bit `SHLD` behaves above a count of 15. A
core that gets that wrong places the page table at physical address 0, on
top of the interrupt vector table, and the failure shows up three layers
away from paging. Paging, the TLB and #PF delivery are not involved. In a
run that succeeds, CWSDPMI takes and services **2992 page faults** through
its own IDT vector-14 handler, and the game renders.

### 9.1 How the failure shows up: a 64KB block of zeros, executed

The retired harness, `disks/boom_run_check.cpp` (`make boom-check`, §19.5),
booted a Full-set HDD image, typed `cd \games\boom` and `boom`, and watched. With the wrong shift, BOOM prints
**no output at all**, never reaches graphics, and sits there for 40 billion
cycles (about ten minutes of emulated time). In the browser the same wrong
shift shows a correctly rendered FreeDoom title screen and then CWSDPMI's
own page-fault diagnostic, `Page Fault cr2=00400006 ... error=0004`
(§9.5).

A sampled CS:EIP histogram, the technique §5.9 used for JemmEx, shows the
native shape:

```
[  3487 Mcyc] mode=0 cs:eip=3725:0000D11E cpl=0 pg=0  hot:
   3725:00005310=10173 3725:00007310=10173 3725:00009310=10173
   3725:0000B310=10173 3725:00001310=10172 3725:00003310=10172
```

A perfectly regular grid of hot addresses 0x2000 apart, each hit the same
number of times, is not a loop. It is straight-line execution sweeping
through memory and wrapping at the 64KB segment boundary. The instruction
ring buffer shows it exactly:

```
3725:000012C2 lin=00038512 cpl=0 pg=0 ... | 00 00 00 00 00 00 00 00 00 00
3725:000012C4 lin=00038514 cpl=0 pg=0 ... | 00 00 00 00 00 00 00 00 00 00
```

`00 00` is `ADD [BX+SI],AL`. The CPU grinds through zeroed memory two bytes
at a time with its registers frozen. Control flow has gone somewhere that
does not exist, the same signature §5.9 describes for JemmEx, reached by a
different route.

### 9.2 The IVT as a page table

Catching the *first* few zero-instructions keeps the preceding real
instructions in the ring. The last real instruction before the runaway is:

```
1632:00000B03 ... eax=00006240 ... | CD 21 ...      ; INT 21h, AH=62h
```

an ordinary DOS call, vectoring straight into zeroed memory at `0002:1007`
(linear 0x1027). Comparing the live IVT against a snapshot taken when BOOM
was launched shows one line per vector:

```
INT 00: 00D9:12BA -> 0000:0207
INT 01: 00D9:1285 -> 0000:1007
INT 02: 0070:0008 -> 0000:2007
INT 10: C000:7D70 -> 0001:0007
INT 13: F000:E3FE -> 0001:3007
```

Every vector is replaced by a regular pattern. Decoded as a dword, vector
`V` holds `V * 0x1000 + 7`, which is not an interrupt vector at all. It is a
**page-table entry**: physical frame `V`, flags `P|W|U`. (Vector 0 reads
`0x00000207` rather than `0x00000007` because bit 9 of a PTE is one of the
three bits Intel leaves available to software, and CWSDPMI had already
marked that page; every other entry is the bare pattern.) CWSDPMI has built
its identity-mapping page table at **physical address 0**, on top of the
interrupt vector table and everything DOS keeps under it.

### 9.3 The NULL far pointer, and the instruction that produces it

A watchpoint on linear 0x84 (the INT 21h vector) names the writing loop:

```
1632:2F75  MOV  AX, DI
1632:2F77  SHL  AX, 2
1632:2F7A  LES  BX, [0x0376]      ; far pointer to the page table
1632:2F7E  ADD  BX, AX
1632:2F80  MOV  word ES:[BX+2], 0
1632:2F86  MOV  word ES:[BX], 0
1632:2F90  INC  DI
1632:2F91  CMP  DI, 0x400         ; 1024 entries
```

`ES` is `0000` with an ordinary real-mode limit of `FFFF`, and `CR0` is
`0x00000010`: no unreal mode, no protected mode. The far pointer at
`[0x376]` is `0000:0000`, so a routine zeroing 4KB of page table zeroes the
bottom of memory instead.

The store that produces it is six instructions long, and every one matters:

```
1632:2F49  CALL ...        -> AX = 0x2B        ; a physical page number
1632:2F4C  XOR  DX, DX
1632:2F4E  MOV  CL, 0x18                       ; 24
1632:2F50  CALL 1632:0184                      ; DX:AX <<= CL
1632:2F53  MOV  [0x0378], DX                   ; far pointer segment
1632:2F57  MOV  [0x0376], AX                   ; far pointer offset
```

and the helper it calls is Borland's 16-bit runtime shift:

```
1632:0184  SHLD DX, AX, CL      ; 0F A5 C2
1632:0187  XOR  BX, BX
1632:0189  SHLD AX, BX, CL      ; 0F A5 D8
1632:018C  RET
```

`0x0000002B << 24` is `0x2B000000`, so `DX:AX` comes back as `DX=0x2B00,
AX=0x0000`: far pointer `2B00:0000`, linear 0x2B000, page 0x2B. The
allocation itself is sound, and the trace shows it: `AH=48h BX=0C40h`
returns segment `0x2026` with CF clear, and the resize to `0x0BDA`
paragraphs succeeds, so the block runs from 0x20260 to 0x2C000. Page 0x2B
sits inside CWSDPMI's own memory, exactly where it belongs. A core whose
helper returns **zero** produces the NULL far pointer above.

### 9.4 SHLD with a count above 15 on a 16-bit operand

`Cpu::shld()`'s 16-bit path computes the SHLD result from the 32-bit
`dest:src` concatenation:

```cpp
cf  = ((pair >> (32 - count)) & 1) != 0;
res = uint16_t(uint32_t(pair << count) >> 16);
```

The `pair` is `(dst << 16) | src`. For a count of 1-16 this is the standard
identity: shift the 32-bit `dest:src` concatenation left by `count` and keep
the high half. A formulation as `pair >> (16 - count)` is right for those
counts, but for a count above 16, 24 here, `16 - count` is **negative**,
which is undefined behavior in C++; in practice the shift amount is masked
to five bits, so `pair >> 24` runs instead and produces 0.

Intel's text is what makes this easy to get wrong. The 80486 PRM's SHLD/SHRD
entry says the count is masked to 5 bits and that **a count greater than the
operand size leaves the result undefined**, so a 16-bit double shift by 24 is
formally undefined and a core is entitled to do anything.

But "undefined in the manual" is not "arbitrary in the silicon", and a real
chip quirk is worth preserving because software depends on it. The 486 has
one 32-bit shifter. It masks the count to 5 bits, shifts the 32-bit
`dest:src` concatenation, and keeps the half the instruction names, so above
a count of 15, bits of the *source register* land in the destination. The
core computes in 64 bits (a 32-bit `pair << count` would overflow) and
narrows to 32, the real shifter's width, so bits shifted off the top are
lost the way hardware loses them.

**The strongest evidence that this is the hardware's actual behavior is the
guest code itself.** Borland's helper is `SHLD dx,ax,cl / XOR bx,bx /
SHLD ax,bx,cl`, with a SHRD mirror for right shifts, and it is compiled in as
*the* implementation of `unsigned long << n` for a variable `n`. It is
correct across the whole 0-31 range only under this behavior, and CWSDPMI
calls it with `cl=24`. A helper written to be correct for counts 16-31 pins
the semantics as surely as a manual would; period compilers shipped it
because real 486s did this.

The **SHRD** path computes `pair >> count` with `pair = (src << 16) | dst`,
which is well defined for every count up to 31 and matches hardware. Only
SHLD's `16 - count` goes negative. That asymmetry is why the failure is so
selective, and why `hdd-boot-check` passes either way.

### 9.5 Why the symptoms differ between builds, and what is not involved

The browser shows a rendered title screen and then a CWSDPMI page fault at
`cr2=00400006`; natively the same wrong shift produces a runaway into zeros
with no output at all. Both share the one root cause, reached through
undefined behavior, which is free to differ between builds: the native build
is `-O2`, the wasm build `-O3 -flto` (§8.3), and a negative shift count is
exactly the kind of construct two optimizers can fold differently.
`cr2=0x400006` is itself consistent with the wreckage rather than with a
paging bug: DJGPP links its image from 0x1000 up and leaves the first page
unmapped so a NULL dereference traps, so a fault six bytes into that guard
page is a near-NULL pointer in the client, the downstream effect of a page
table built in the wrong place, not a translation failure.

Three things the evidence clears outright:

- **Page-fault delivery is correct.** The successful run takes 2992 #PFs and
  CWSDPMI services every one through its own vector-14 handler.
- **The TLB and INVLPG are correct.** Those 2992 repairs only work if a
  handler's page-table update is visible afterwards.
- **Safe Mode (§5.9, HIMEMX-only, no JEMM386) is not involved.** CWSDPMI
  asks DOS for memory, not the memory manager: `AH=48h` returns a real
  segment with CF clear, and the trace shows the whole `5800/5802/5801/5803`
  allocation-strategy dance completing normally. It needs no service Safe
  Mode withholds.

### 9.6 The proof

`make boom-check` (retired, §19.5) booted a Full-set image, ran BOOM, and
required the game to reach its renderer. The bar is not "a picture appeared", since the title screen is
a single static bitmap that a working WAD loader alone would produce, but
**24 consecutive differing 320x200x256 frames**, which only a running 3D
renderer produces:

```
[  1601 Mcyc] > IWAD found: ./doom.wad
[  1627 Mcyc] > R_Init: Init DOOM refresh daemon -
[  2593 Mcyc] > P_Init: Init Playloop state.
[  2595 Mcyc] > I_InitSound:
[  2823 Mcyc] > HU_Init: Setting up heads up display.
[  2825 Mcyc] > ST_Init: Init status bar.

OK: BOOM is rendering -- 24 consecutive differing 320x200x256 frames,
25 distinct frames total, at cycle 3241155494
faults during the run: v7=3 v14=2992
```

The captured frames are the rest of the proof: the title screen mid-**melt**
(Doom's column-by-column wipe, jagged by design, with FreeDoom's art and URL
legible through it), and gameplay: a 3D-rendered room, blue nukage floor,
three enemies, status bar. `v7=3` is #NM from CR0.TS across task switches,
which is the FPU behaving as §6.6 describes.

Regression coverage, in `tests/cpu80486_test.cpp`:

- `SixteenBitShldAboveFifteenShiftsTheThirtyTwoBitConcatenation`: the exact
  case CWSDPMI executes (page 0x2B, count 24), plus the whole Borland helper
  run as a sequence proving `DX:AX <<= 24`.
- `SixteenBitShrdAboveFifteenShiftsTheThirtyTwoBitConcatenation`: the SHRD
  mirror, `DX:AX >>= 24`.
- `SixteenBitShldAndShrdWithinTheDocumentedRange` and
  `SixteenBitDoubleShiftCarryIsTheLastBitShiftedOut`: the specified range
  and the carry rule, pinning down that widening the pair to 64 bits changes
  no documented case.

With the 64-bit shift, `pm-check` passes 24/24, `vbe-check` all-pass, and
`hdd-boot-check` reaches its idle `C:\>` at **cycle 1,409,073,889**, the
identical figure §8.3 records, so real mode is cycle-for-cycle unchanged.

### 9.7 The tooling, and what it costs

None of §9.1-9.3 is reachable with `on_fault` and `on_unimplemented` (§4.7,
§6.4). They report a fault or a bad opcode, and this failure produces
neither: the guest is executing valid `ADD [BX+SI],AL` instructions over
zeroed memory. What it needs is an instruction ring buffer, which means a
per-instruction hook in `Machine::run_cycles()`, §8's hot loop, where a
`std::function` would not be affordable. It is a plain function pointer plus
a ctx pointer, the same shape §8.3 gives `cpu80486::Bus`, and null by
default.

The cost is measured, since §8 is the reason to care: a full
`hdd_boot_check` boot runs in **15.07 s with the hook present and null
against 14.92 s with the call removed entirely** (native, `-O2`, two passes
each, idle machine), about 1%. A measurement taken while a Playwright suite
runs in parallel is wrong by tens of percent (21.5 s here), so A/B timings
run on an idle host.

Three diagnostics built on that hook did the work, all in
`boom_run_check.cpp` (deleted, §19.5) behind flags:

- `--catch-runaway` dumps the ring the moment the guest starts executing
  zeroed memory. Timing is the whole point: noticing the spin billions of
  cycles later finds a ring full of zeros, while catching the eighth such
  instruction still holds ~24000 real ones.
- `--watch LINEAR` dumps the ring when a linear dword changes, so the
  instruction that wrote it is the newest entry. This is what names the
  zeroing loop from the corrupted INT 21h vector.
- An IVT snapshot taken at launch and diffed at the failure turns "the
  vectors look wrong" into "these are page-table entries".

### 9.8 Two smaller observations

- **Text-mode corruption after BOOM does not reproduce.** Garbled colored
  blocks in a directory listing and in the `C:\GAMES\BOOM>` prompt do not
  appear with the correct shift: rendering a real `DIR` of `C:\GAMES\BOOM`
  through `ega_render.cpp` gives a clean screen, every digit of the
  file-size column, every glyph of the prompt, correct attributes. The most
  plausible account of the garbled screenshots is a session in which BOOM
  had already run with the wrong shift and page-tabled over the IVT and
  DOS's own data at physical 0, leaving DOS drawing from corrupted state.
  That explanation is consistent with the evidence, not demonstrated, and
  worth re-checking in the browser before treating it as closed.
- **`FF /7` is an invalid encoding.** Intel documents it as one, and
  `grp5()`'s `default` arm reports the opcode through `on_unimplemented`
  and returns, where a real 486 raises #UD. Every occurrence observed here
  was an artifact of a runaway executing garbage (all at `0000:xxxx` and
  `0002:xxxx`, inside the wreckage), so nothing real depends on it. A core
  that silently continues past an invalid opcode lets a runaway guest grind
  on where hardware would have stopped it. §39 raises #UD for it, with that
  verification pass.

### 9.9 What this says about the test strategy

The GoogleTest suite, `pm-check`, `vbe-check` and `hdd-boot-check` all pass
with a core instruction returning zero for an input real software actually
uses. Every one of those proofs is written against code this repo controls,
and none of it does a 16-bit double shift by more than 15.

That is the general shape: the cases a hand-written test suite omits are
precisely the ones nobody thought of, and undefined-in-the-manual corners are
where period software quietly depends on real silicon. Running genuine
third-party software is not a nice-to-have on top of the unit suite. It is
the only thing that samples the instruction set the way 1990s compilers
actually emitted it. `boom-check` was that kind of check alongside `hdd-boot-check`, and the shift
tests in §9.6 are in the unit suite.

### 9.10 Sources

- Intel 80486 Programmer's Reference Manual (1990/1992), SHLD/SHRD: the
  5-bit count mask, the fill-from-source definition, the carry rule, and the
  statement that a count greater than the operand size leaves the result
  undefined. Also `FF /7` as an invalid encoding (§9.8).
- **CWSDPMI r5 and its Borland-compiled runtime helpers, as shipped in
  `C:\GAMES\BOOM\CWSDPMI.EXE`**, cited as the primary evidence for the
  above-15 behavior: a 32-bit shift helper correct across 0-31 only under
  the 32-bit-concatenation semantics, called with `cl=24`. The disassembly
  in §9.3 is read straight out of this machine's own instruction trace.
- DJGPP's image layout, text linked from 0x1000 with the first page left
  unmapped as a NULL-pointer guard, which is what makes `cr2=0x400006`
  readable as a near-NULL client dereference (§9.5).

## 10. A PS/2 mouse on the 8042's second port

A 1993-94 "maxed out" 486 ships a PS/2 mouse hanging off the keyboard
controller's auxiliary port. The mouse is contained to `i8042.h`/`i8042.cpp`
and its own suite: the AUX side of the controller, IRQ12, and a complete
standard PS/2 mouse behind it.

### 10.1 The second port is a PS/2 feature, not an AT one

`i8042.cpp` models a *PC/AT* keyboard controller, and the original AT's own
8042 firmware has no mouse in it at all. OS/2 Museum's disassembly of the
5170 KBC ROM lists commands 80h-A8h and D2h-DEh as **ignored** outright,
which covers 0xA7/0xA8 (disable/enable AUX), 0xD2/0xD3 (write to either
output buffer) and 0xD4 (write to the mouse), and status-register bit 5 on
that part means "transmit timeout", not "this byte came from the mouse".

The port arrived with the PS/2 line in 1987 and was universal on 486 boards
by the time this machine is built: the round mouse DIN next to the keyboard
DIN is the same 8042 answering a superset of the same command set. So the
class keeps its name and its AT wiring, and gains the PS/2 superset on top,
the same "this is the later part that is backward-compatible with the
documented one" position `wd1003` already occupies for IDE.

### 10.2 Status bit 5 is the whole mechanism, and it is not optional

One output buffer, one data port, two devices behind it. Everything above
the controller separates the two streams by **AUXB**, status bit 5: set
means the byte in the buffer came from the mouse.

This is not a detail that degrades gracefully if you get it wrong. The
BIOS this machine ships reads mouse bytes through exactly one helper:

```c
get_mouse_data(data)
{
  while ((inb(PORT_PS2_STATUS) & 0x21) != 0x21) { }
  response = inb(PORT_PS2_DATA);
  ...
}
```

`rombios.c`. Its INT 74h handler opens with the same test and returns
immediately if it fails. A mouse byte delivered with OBF but without AUXB
is a byte this firmware spins on forever, so bit 5 is load-bearing for
every single transfer, command ACKs included.

### 10.3 What the firmware's own sequence requires

Three requirements come straight from `rombios.c`'s INT 15h AH=C2h
implementation rather than the protocol documents, and each is a test in
`i8042_test.cpp`:

- **Commands must work while the AUX clock is "disabled".** Every C2h
  subfunction begins with `inhibit_mouse_int_and_events()`, which clears
  command-byte bit 1 (IRQ12) and *sets* bit 5 (drive the mouse clock line
  low), and then sends mouse commands through 0xD4 and waits for their
  ACKs. Bit 5 stops the device *reporting*; it does not stop the controller
  raising the line to carry a host command. Only unsolicited movement
  packets are gated on it.
- **A write to port 0x64 cancels a pending 0xD4.** `set_kbd_command_byte()`
  writes 0xD4 to 0x64 and then immediately 0x60 to 0x64, abandoning that
  "write to mouse" without ever supplying its data byte. The 8042
  distinguishes command from data by the A2 address line, so the second
  write is simply a new command. A surviving stale 0xD4 would deliver the
  command byte to the *mouse*, and the machine would lose its keyboard and
  mouse interrupts at the exact moment a driver turned them on.
- **Every mouse byte raises IRQ12, ACKs included.** That is precisely why
  the BIOS disables IRQ12 around its own command traffic and restores it
  afterwards.

POST itself writes command byte 0x61: translation on, IRQ1 on, **AUX clock
disabled**. So a mouse is inert until a driver (INT 15h C2h, or the command
byte directly) enables it, which is the correct power-on posture and leaves
the boot unchanged.

### 10.4 A one-byte buffer needs a real queue behind it

A keyboard-only machine survives a single-byte output buffer where a new
byte overwrites whatever is unread, because BIOS drains before it asks
anything. A mouse does not survive it: the smallest unit this device sends
is a **3-byte packet**, a reset answers **FA AA 00**, and a player moving
the mouse while typing produces both streams at once. Overwriting would
silently eat one byte of every packet that collided with a keystroke.

Real hardware queues this in the *devices*, not the controller: the 8042
holds a device's clock line low for as long as OBF is set, and the device
keeps its bytes until the line is released. The FIFO stands in for both
devices' holding buffers, each byte tagged with its source (which
decides bit 5) and which interrupt line it drives. Answers to *controller*
commands deliberately do not go through it, the 8042 has no queue of its
own and writes its answer straight into the buffer over whatever was there,
and every caller of those commands polls for the answer.

Two consequences, both the real behavior: a scan code cannot destroy an
unread byte, and a scan code injected while the buffer is full waits instead
of arriving early. Tests that read the keyboard drain its power-on BAT byte
first, which is exactly what BIOS POST's own flush loop does before it tests
anything.

### 10.5 The mouse: a period 3-button PS/2 mouse, and no wheel

The device implements the full standard command set, FF, FE, F6, F5, F4,
F3, F2, F0, EE, EC, EB, EA, E9, E8, E7, E6, with its real defaults (100
samples/sec, 4 counts/mm, 1:1 scaling, reporting disabled, stream mode),
all four modes (reset, stream, remote, wrap), and the 3-byte movement
packet. Details worth naming, because each is easy to get plausibly wrong:

- **+Y is away from the user**, not down the screen. The reference's own
  worked table is the test: move up one is `08 00 01`, move down one is
  `28 00 FF`. A front end handing this device browser `movementY` must
  negate it.
- **Bit 3 of byte 1 is always set.** Some drivers ignore it; others treat a
  packet without it as an error and reinitialize the mouse.
- **The counters are 9-bit and saturate.** Range -255..+255; beyond that
  the overflow bit is set and the excess is genuinely lost, not carried.
- **2:1 scaling applies to stream reports only**, not to the packet
  returned by Read Data (0xEB). The table (0, 1, 1, 3, 6, 9, 2N) is the
  mouse's own, applied to the counters before they are reported.
- **The status packet's buttons are in the reverse order** of the movement
  packet's: left/middle/right in bits 2/1/0, against left/right/middle in
  bits 0/1/2.

Deliberately absent: the Microsoft IntelliMouse's 4-byte wheel packet. That
part shipped in 1996, two years after this machine's build date, and no DOS
software of the era asks for it. Its "knock" (set sample rate 200, 100, 80,
then read device ID) is answered the way a *standard* mouse answers it,
every rate accepted, device ID still 0x00, which is exactly how a driver
probing for a wheel learns there isn't one. Adding it later is a
self-contained change if a reason ever appears.

### 10.6 Tests

`i8042_test.cpp` covers the controller side (AUXB tagging, 0xA7/0xA8/0xA9, 0xD2/0xD3/0xD4, the
cancelled-0xD4 case, IRQ12 gating and its re-assertion per packet byte),
the device side (reset's three-byte answer, reporting gate, the four
reference movement packets and all three buttons byte-for-byte, counter
saturation and overflow, remote mode, scaling in stream but not in Read
Data, status request at defaults and after programming, set-defaults, wrap
mode's two exceptions, resend, unknown-command 0xFE, counters cleared by
commands), and two integration-shaped cases: keyboard and mouse bytes
interleaving without loss, and the BIOS's own INT 15h AH=C2h init sequence
run end to end.

The bar is real third-party software. A DOS game reaches the mouse through
INT 33h, which is a driver (FreeDOS ships CTMOUSE), not firmware, so "a game
sees the mouse at its setup screen" needs the chipset's IRQ12 line, a
host-side event path, and that driver actually reachable. The Drivers panel
(§25) hands the machine a CuteMouse diskette.

### 10.7 Sources

- **Adam Chapweske, "The PS/2 Mouse Interface" (2001)**, the command set
  and every response byte, the reset defaults and BAT/device-ID sequence,
  the four modes, the 3-byte packet layout, the 9-bit counters and their
  overflow rule, the 2:1 scaling table and its stream-only footnote, the
  status-request format, and the worked byte sequences for unit moves and
  button presses that §10.5's tests assert verbatim.
- **Bochs BIOS `rombios.c`** (the ROM this machine actually boots),
  `get_mouse_data`, `send_to_mouse_ctrl`, `set_kbd_command_byte`,
  `inhibit_mouse_int_and_events` / `enable_mouse_int_and_events`,
  `int15_function_mouse` (AH=C2h) and the INT 74h handler. Cited for what
  the firmware *requires* of the hardware, per §7.11's rule, not as a
  behavioral model.
- **Andries Brouwer, "Keyboard scancodes", ch. 11 (the AT keyboard
  controller)**, the status register and controller command byte bit by
  bit, and which commands are AT versus PS/2.
- **OS/2 Museum, "IBM PC/AT 8042 Keyboard Controller Commands"**, the
  5170 KBC ROM disassembly behind §10.1's claim that the original AT part
  ignores the whole AUX command range.
- Not pinned down by those documents: the AUXB flag driving IRQ12, and
  0xA9's answer going to the controller side rather than the AUX side.

## 11. A Sound Blaster 16 at 0x220, IRQ5, DMA 1 and 5

`soundblaster.h`/`soundblaster.cpp` add the card a 1993-94 "gamer's dream"
486 actually shipped with: a Creative Sound Blaster 16, CT1745 mixer, DSP
version 4.05, at base I/O 0x220, on IRQ5, with 8-bit DMA channel 1 for
legacy digitized playback and 16-bit DMA channel 5 for its own SB16-mode
stereo. That is exactly the card a period driver describes to itself as
`SET BLASTER=A220 I5 D1 H5 T6`, which is the environment this machine's DOS
software reads.

Everything about the register semantics comes from Creative's own *Sound
Blaster Series Hardware Programming Guide*, see §11.6. Where the guide is silent (a handful of
diagnostic commands Creative never documented but drivers use anyway), the
sources list says so explicitly.

### 11.1 The whole 20-port block, not just the DSP's four

A real SB16 decodes base+0h through base+13h, twenty ports, per SBPG
Appendix A Table A-15, so `owns()` claims 0x220-0x233: FM at base+0..3 and
base+8..9, the CT1745 mixer's address/data pair at base+4/5, DSP reset at
base+6, DSP read data at base+Ah, write command/data (and, on read,
write-buffer status) at base+Ch, read-buffer status at base+Eh, the SB16's
own 16-bit interrupt acknowledge at base+Fh, and the card's own proprietary
CD-ROM interface at base+10h..13h. That last group decodes as "card present,
no drive attached": this machine's CD-ROM is an ATAPI device on the secondary
IDE channel, so nothing answers behind the SB16's own drive interface.
Table 2-1 covers only the four SB1.5-era DSP ports and predates both the
mixer and base+Fh.

Two decode decisions are deliberate rather than incidental:

- **The 0x388/0x389 AdLib FM pair IS claimed**, and so are the FM ports
  inside the card's own block, all reaching one real YMF262 (`opl3.h`,
  §11.1). 0x388/0x389 is the original AdLib card's address pair and every
  Sound Blaster answers it; that is the path that actually matters, because
  an AdLib-era music driver, DOOM's included, writes FM registers there
  and never touches the card's own block at all. A machine that decoded
  base+0h..3h but not 0x388 would let software detect an OPL and then play
  nothing. 0x38Ah/0x38Bh, the
  bank-1 pair an AdLib Gold or PAS puts there, stay unclaimed: an SB16 puts
  the OPL3's second bank at base+2h/3h. Within the block: base+0h/1h
  is its bank-0 address/data pair, base+2h/3h bank 1, base+8h/9h the
  AdLib-compatible alias of bank 0, and all three status ports return the
  chip's one status register. The standard AdLib probe, reset both timers,
  read status, start timer 1, read status again and expect C0h, therefore
  succeeds, because a real OPL3 sits behind it. A fake OPL3 that passed
  detection and then produced silence would be *worse* than absent hardware,
  because software would believe its music was playing. Absent hardware is a
  condition period software already knows how to handle; a lying status
  register is not. FM detection and FM audio therefore come together.
- **base+Eh acknowledges the 8-bit interrupt on every read.** Creative is
  explicit that this is how it works ("to remain backward compatible, the
  interrupt acknowledgment of 8-bit DMA mode digitized sound I/O and SB-MIDI
  is done via the Read-Buffer Status port"), which means the same port a
  driver polls during the reset handshake is also the acknowledge. It is not
  a separate register that happens to share an address.

### 11.2 Why DSP 4.05 specifically, and why the version number is load-bearing

The DOS driver BOOM links, Allegro 3.x's `sb.c`, reads DSP command E1h,
assembles `(major << 8) | minor`, and branches on `>= 0x400` to choose its
SB16 path: 41h to set a true sampling rate in Hz, then B6h with mode byte
30h (16-bit signed stereo, auto-init, FIFO on) and a sample count, running
on the 16-bit DMA channel. Report 3.02 and the *same driver on the same
card* silently drops to the SB-Pro path instead, and 16-bit stereo output
never gets exercised at all. So the version byte pair is not cosmetic
identification, it selects which half of this device's command set real
software will ever reach. 4.05 is a genuine shipped SB16 revision.

### 11.3 Command coverage, and what is deliberately left ignored

Implemented: direct-mode 10h; the legacy 8-bit single-cycle 14h/24h and
auto-init 1Ch/2Ch paced by the 40h time constant with 48h block size; the
DSP 4.xx programmed Cxh (8-bit) and Bxh (16-bit) transfers with their
A/D-vs-D/A, auto-init and FIFO command bits and their stereo/signed mode
byte, rates set by 41h/42h; pause/continue/exit D0h/D4h/D5h/D6h/D9h/DAh;
speaker D1h/D3h/D8h; the 80h silence period; and the identification and
diagnostic commands E0h, E1h, E3h, E4h, E8h, F2h and F3h.

Three groups are accepted and ignored, each for a documented reason:

- **High-speed mode (90h/91h/98h/99h) does not exist on this card.**
  Creative's own availability matrix lists these for DSP 2.01+ and 3.xx
  only, not 4.xx, DSP 4.xx reaches full rate in normal mode and dropped
  them. Ignoring them is the real SB16's behavior, not a gap in this
  device, and Allegro only issues them after detecting a version below
  4.00 anyway. This is the one place where doing *less* is the accurate
  choice, which is why it has a test of its own.
- **A0h/A8h (mono/stereo input select) do not exist on DSP 4.xx either**,
  for the same documented reason: stereo is selected by the Bxh/Cxh mode
  byte instead.

- **ADPCM output (16h/17h/74h-77h, auto-init 1Fh/7Dh/7Fh) plays nothing.**
  SBPG Table 3-1 lists 8-bit mono ADPCM for DSP 4.xx, so a real SB16
  plays it. Creative never published the decoder's step and adjust tables,
  so the commands take their length bytes, keeping the command stream in
  sync, and start no transfer. A program that uses ADPCM gets silence for
  that sound. This is a labelled departure; a DSP ROM disassembly or a
  measured card would let it come back.
- **Recording delivers digital silence, paced correctly, with its
  interrupts firing on schedule.** Nothing is plugged into the line or mic
  inputs, so silence is what a real card digitizes, and note silence is
  not the same byte in both formats (80h unsigned 8-bit, 0000h signed
  16-bit), per Creative's own note that the two formats differ only in
  where minimum amplitude sits. Firing the interrupts is what keeps a
  recording program from waiting forever on a card that is working
  correctly.

### 11.4 Pacing: the same credit scheme as the floppy, for the same reason

Playback is paced by accumulated CPU-cycle credit against the programmed
sample rate, exactly like `fdc765.h`'s data-transfer pacing. Real elapsed
wall-clock time per sample is exact, CLAUDE.md forbids speeding this up,
and an audio device is the one place where getting it wrong is *inaudible
as a bug and obvious as a symptom*: everything simply plays at the wrong
pitch. The trade-off is the same one the floppy makes: the bytes for a
batch of due samples move in one burst rather than one DMA cycle per
sample, so the DMA address/count registers step in bursts. Software that
waits for the block interrupt (universal practice) cannot tell; software
polling the DMA count to find the play position mid-block would see it
move in steps rather than smoothly. `PlaybackIsPacedAtTheRealSampleRateAndNeverFaster`
is the standing guard on the timing itself.

The audio handed to the front end follows `pcspeaker.h`'s philosophy one
level up. This device does not synthesize anything: it records what the DAC
actually latched and the CPU cycle it latched at, normalized to signed
16-bit stereo (the card's own widest native format). That keeps the core
from committing to an output sample rate, and it makes a program that
changes rate mid-stream, or plays 8-bit mono right after 16-bit stereo,
need no special case at all, the timestamps already say what happened
when. The CT1745's Voice and Master attenuators are exposed as linear gains
*beside* the sample stream rather than folded into it, so `drain_samples()`
returns exactly the values the program wrote.

### 11.5 Two wiring details the chipset has to get right

Both are called out in `soundblaster.h`'s header because getting either
wrong fails quietly:

- **16-bit transfers run on DMA2's channel 5, whose registers count words,
  not bytes.** The 8237's address outputs drive A1-A16 on the 16-bit
  channels and the page register supplies A17-A23, so the physical address
  is `(page << 16) | (address << 1)`, the available byte count is
  `(count + 1) * 2`, and one `advance()` covers a byte *pair*. Page-register
  bit 0 is not connected on those channels. Treating channel 5 like the
  floppy's channel 2 would put every 16-bit buffer at half its real address.
- **Auto-init playback needs the DMA controller to reload address and count
  from its base registers at terminal count.** `Dma8237` models this
  directly (§12.2): a real 8237A-5 channel latches whatever address/count
  it's programmed with into a second, hidden pair of registers, and on
  autoinit reloads the *working* registers from that hidden pair at
  terminal count instead of continuing to increment/wrap (Intel 8237A-5
  data sheet, "Autoinitialize"). The floppy never sets the mode byte's
  autoinit bit, so the SB16 is the client that exercises it. The device keeps requesting bytes until the driver
  sends DAh (8-bit) or D9h (16-bit), which is the entire point of auto-init
  mode and is how a period driver plays continuous double-buffered sound.

### 11.6 Sources

- **Creative Technology Ltd, *Sound Blaster Series Hardware Programming
  Guide***, the primary source for essentially everything here, and cited
  by chapter/page in both the device header and the test file: Table 2-1's
  DSP I/O ports; chapter 2's reset handshake (write 1, wait 3us, write 0,
  poll read-buffer status bit 7, read 0AAh), interrupt acknowledgment via
  base+Eh/base+Fh, and the mixer 80h/81h/82h interrupt-setup, DMA-setup and
  interrupt-status bit layouts; chapter 3's time-constant formula
  `65536 - 256000000/(channels * rate)` with only the high byte programmed,
  and the single-cycle vs auto-initialize transfer procedures for both the
  legacy 8-bit commands and the DSP 4.xx ones; chapter 4's CT1745 register
  map, per-register power-on defaults, and the statement that the
  CT1345-compatibility volume registers "are actually mapped to the new
  volume control registers"; chapter 6's command reference, including the
  Bxh/Cxh command-byte bit fields (A/D vs D/A, auto-init vs single-cycle,
  FIFO) and mode-byte bit fields (stereo, signed), the availability matrix
  that rules high-speed mode out on 4.xx, and the note that on DSP 4.xx
  D1h/D3h move only the flag D8h reports; and Appendix A's confirmation
  that 220h is the factory default base address for every Sound Blaster.
- **Allegro 3.x `sb.c`** (the DOS Sound Blaster driver BOOM links through
  DJGPP), cited as evidence of what a period game's driver *actually
  issues*, rather than what the guide permits: the `(major << 8) | minor`
  version test against 0x400, `256 - 1000000/rate` as the time-constant
  byte, `0xB6` with mode `0x30` for the SB16 path, `0x14` for single-shot
  on pre-2.0 cards, `0x48` + `0x90` on 2.01-3.xx, D1h/D3h for the speaker,
  and acknowledging via base+Eh for 8-bit and base+Fh for 16-bit.
- **`SET BLASTER=A220 I5 D1 H5 T6`**, the period-standard environment
  string, corroborating 0x220/IRQ5/DMA1/DMA5 as the defaults this device
  powers up with (mixer 80h = 02h, mixer 81h = 22h).
- Unsourced: the E3h copyright string (`COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.`
  and that it ends in a null), E0h (bitwise complement), E4h/E8h (a
  diagnostic register that survives DSP reset), and F2h/F3h (8-bit and
  16-bit interrupt triggers). None is in Creative's guide, and no primary
  source or real-card capture was found. They stay in the code and are not
  a hardware citation.
- **DMX (Paul Radek), as used by DOOM**, background on why ~11 kHz 8-bit
  unsigned mono is the format the digitized-output path most needs to get
  right for this era, and why auto-init DMA (rather than a chain of
  single-cycle blocks) is the mode that matters.

## 11.1 The OPL3: FM music

`opl3.h`/`opl3.cpp` add the YMF262 the SB16 carries for music, 18 channels
over 36 operators, two- and four-operator FM, eight waveforms, the rhythm
section, and the two timers. Written from the Yamaha YMF262 register
documentation and the OPL decap analyses of the internal ROM tables.

**The output rate is not a choice.** The chip divides its 14.31818 MHz master
clock by 288, emitting one stereo frame every 288 clocks: 49715.9 Hz. That is
a property of the silicon, so `kSampleHz` is `constexpr` and the front end
resamples from the `cpu_cycle` stamps, the same contract the digitized path
already had, and the reason no speed override question arises here.

**Two sample streams, not one.** `SoundBlaster::drain_samples()` and
`Opl3::drain_samples()` stay separate all the way to the browser, where
`pumpSbAudio()` sums them. That mirrors the card: the OPL3 and the DSP have
their own DACs and are summed in the analog domain, each behind its own
CT1745 attenuator, Voice (mixer 32h/33h) for digitized audio, MIDI
(34h/35h) for FM, since the OPL3 *is* the card's internal MIDI source. Mixing
them in the core would force the event-driven DAC stream onto the FM chip's
continuous timebase for no gain.

One departure worth naming: those attenuators are pure attenuation, and both
Master and the per-source pair power on at 24 (-14 dB), so the chain sits
~28 dB down at defaults. A real card makes that up in the analog output amp
after the mixer, so the front end normalizes by the power-on product,
default mixer settings play at full scale and a program moving a slider still
attenuates relative to it. Without that, the attenuators alone would leave
every sound nearly inaudible.

**`tick()` steps the FM chip before the digitized path's early-out**, so music
keeps playing while no sample block is in flight, and `active_` counts a
running timer as well as an unreleased envelope. That second point is
load-bearing rather than incidental: a driver probes the timers with nothing
keyed on, so keying the early-out off envelope state alone deadlocks
detection, the chip never advances, the envelope never leaves its idle,
fully-released state, and the probe hangs forever.

### Known approximations

These are labelled in `opl3.cpp` at the point they matter, and none of them
affect whether software finds the chip or plays in time. The four-operator
pair's F-number source and the note-select split point are sourced; what is
open is in §31 and §35 (the modulation index is unsourced, and the rhythm
level comes from the YM2413 manual, not an OPL3 document). The envelope,
vibrato and waveform 7 sources and gaps are in §35.

- **The output stage's master gain has no hardware citation.** The YMF262
  datasheet (p.5) documents the output format, 16-bit offset binary on four
  buses at 49.7 kHz, which independently confirms `kSampleHz`, but neither
  Yamaha document says how up to 18 channels are summed internally or what
  headroom that leaves, and Creative's SB16 programming guide (p.138) refers
  OPL3 internals back to the vendor. The gain is 6.0, with `opl3_test.cpp`'s
  `FullPolyphonyAtModerateLevelsDoesNotClip` and
  `OneFullVolumeVoiceLeavesHeadroomForEighteen` holding the headroom. The
  exponent ROM is read in the direction §29 describes, so a waveform's
  amplitude is right before the gain applies. Both tests sum more than one
  channel.
- **The rhythm section's snare, hi-hat and top cymbal** use a reasoned
  noise/phase combination rather than a verified reproduction: the operator
  assignment is documented, the exact bit the silicon XORs is not. Bass drum
  and tom-tom are plain FM voices and are on solid ground.
- **The noise LFSR taps** are a generic maximal-length polynomial; no primary
  source for OPL3's actual tap positions was found.
- **C0h's power-on value.** One source claims C0h-C8h come up with their
  output-enable bits already set; it could not be corroborated, so every
  register clears to 0 as every other OPL reference states. Behaviour is
  identical either way, because the pan bits are ignored entirely while NEW
  is clear, which is what keeps an OPL2-era driver audible on both outputs.

## 12. Wiring the mouse and Sound Blaster into the chipset

§10 and §11 build the two devices in isolation, each against its own
independent test suite. This section covers the integration: both talking to
`Chipset`, `Machine`, and the browser front end without colliding, plus the
8237 autoinit behavior the Sound Blaster depends on.

### 12.1 Port decode, reset, and the tick order

`chipset.h` has a `SoundBlaster sb` member alongside the existing
`PcSpeaker speaker`, at its real factory-default address (0x220, IRQ5, DMA1/
DMA5, §11's `SET BLASTER=A220 I5 D1 H5 T6`). `io_in`/`io_out` have an
`sb.owns(port)` check beside every other device's, and `reset()` resets it
alongside the rest of the chipset on both power-on and the front-panel
Reset button.

The mouse needs no new member: `I8042` already owns the AUX port (§10), so
the only new chipset-level surface is a one-line forwarding method,
`Chipset::inject_mouse_event(dx, dy, buttons)`, that the host calls into
`kbc.inject_mouse_event(...)`, the same shape as the existing
`injectScancode` path for the keyboard side of the same controller.

`tick()` services both devices every CPU instruction, same as every other
chipset device, but the two need opposite edge-detection policies, and
getting that backwards for either one silently breaks it:

- **IRQ12 (mouse) is level-checked, not edge-detected.** `I8042::
  irq12_pending()` stays asserted across every tick of a multi-byte AUX
  packet until the guest's ISR reads the byte that clears it (§10.4); an
  edge-detect here would raise the interrupt once and then silently drop
  bytes 2 and 3 of every packet, since nothing would re-notice the line is
  still high.
- **IRQ5 (Sound Blaster) is edge-detected**, `sb_irq_prev_` tracking the
  0->1 transition exactly like the existing `fdc_irq_prev_`/`kbc_irq_prev_`/
  `hdd_irq_prev_`/`cdrom_irq_prev_` pattern (see `chipset.h`'s own comment
  on why re-raising a level-triggered ISA line on every tick, rather than
  only its rising edge, causes a real interrupt storm on the PIC). Unlike
  the mouse's AUX port, nothing about the DSP's interrupt-status protocol
  needs the line to stay asserted between ticks, it's cleared once, by
  the base+Eh/base+Fh read §11.1 describes, not re-derived from live state.

### 12.2 Making DMA autoinit real, in `Dma8237` rather than around it

§11.5 notes that `soundblaster.h` needs auto-init DMA (continuous,
re-triggering transfers with no per-block driver intervention), which
requires `Dma8237` to model the reload that makes auto-init actually
auto-*init*. The reload is genuine 8237A-5 hardware behavior with nothing
device-specific about it, so it lives in the DMA controller. A per-device
patch in the chipset that reloads the channel at block end would leave
`Dma8237::advance()` reporting a wrapped address to a caller that expects a
reloaded one, an observable gap between the model and a real chip, and any
other DMA client with the autoinit bit set would need the same fix
re-derived independently.

Per the Intel 8237A-5 data sheet's "Autoinitialize" section, a real channel
latches whatever address and count it's programmed with into a second,
hidden register pair alongside the normal (working) ones. `dma8237.h`'s
`Channel` struct has `base_address`/`base_count` for exactly this, and
`out()`'s address/count-write path mirrors every write into both pairs at
once, so the hidden registers always hold "the last full reload", never
anything a driver has to program separately. `advance()` checks the mode
byte's autoinit bit (0x10) at terminal count: if set, the working registers
reload from the hidden pair instead of continuing to increment/wrap; if
clear, the channel increments and wraps as a plain 8237 does. The floppy
controller, `Dma8237`'s only other client, never sets that bit. Two
`dma8237_test.cpp` cases
(`AutoinitializeReloadsAddressAndCountAtTerminalCount`,
`WithoutAutoinitializeAddressWrapsInsteadOfReloading`) pin both paths
directly. `chipset.cpp`'s `service_sb_dma()` calls `advance()` and trusts
the result, with no autoinit-specific logic of its own.

### 12.3 `service_sb_dma()`: one DMA client, two different channel widths

`service_sb_dma()` moves one block once the device's own real-time pacing
says a transfer is ready (`sb.transfer_ready()`), the same "one bulk copy
per completed real-time wait" shape the floppy's own DMA handoff already
uses, this is a batched copy standing in for what real hardware does one
byte at a time on every DMA request cycle, not a timing shortcut, since the
device-side pacing (not the copy) is what determines when bytes are allowed
to move at all. It branches on `sb.transfer_is_16bit()` and applies the two
corrections §11.5 documents: the 8-bit path reads/writes `dma1` byte-for-
byte at `(dma1.page(1) << 16) | dma1.address(1)`; the 16-bit path reads
`dma2`'s channel 1 (physical DMA channel 5) as *word*-addressed,
`(dma2.page(1) << 16) | (dma2.address(1) << 1)`, `(count + 1) * 2` available
bytes, one `dma2.advance(1)` per byte *pair*, exactly as the header's
formulas specify. Both paths finish with `sb.finish_transfer(moved)`,
handing control back to the device to decide (per its own auto-init/
single-cycle state) whether to re-arm for another block or raise its
interrupt and stop.

### 12.4 Exposing both to the browser

`wasm_machine.cpp` has `injectMouseEvent(dx, dy, buttons)` (a direct
forward to the chipset method) and `sbDrainSamples()`/`sbSampleRateHz()`
(mirroring the existing `speakerEdges()` shape: drain the device's own
timestamped sample log into typed arrays every call, so the log can't grow
unbounded even while muted). `app.js` gates SB16 output behind the same
"Enable sound" checkbox as the PC speaker, both are blocked by the same
browser autoplay policy until the user opts in, so one control for both
avoids asking twice, and adds a separate, also-off-by-default "Enable
mouse" checkbox that requests Pointer Lock on the canvas when checked,
translating `movementX`/`movementY` into PS/2-convention relative motion
(`dy` negated, since the AUX port's own axis convention is +Y-away-from-
the-user, the opposite of a browser's +Y-down-the-screen). Both audio
paths (speaker and SB16) use the ring-buffer-behind-an-
AudioWorklet pattern from §8's frame-loop design, a worklet's `process()`
callback runs on the real-time audio thread regardless of main-thread
jank, so a brief stall holds the last sample instead of clicking.

## 13. Holding the mouse and the Sound Blaster to BOOM

§10, §11 and §12 cover a PS/2 mouse, a Sound Blaster 16 and the chipset
wiring for both, each against its own suite. The bar for them is real
third-party software: **BOOM has to see the mouse, and BOOM's sound effects
have to come out of the DAC.** Four pieces outside the two devices decide
whether that happens, and none of them is in `i8042.cpp` or
`soundblaster.cpp`. Two sit in the layer that tells DOS what hardware
exists, one is the 8259A's priority resolver, and one is the direction of
the DMA byte mover. They are written in the order the guest reaches them,
because each is only reachable once the one before it is right.

### 13.1 The CPU identifies itself, so DOS loads the mouse driver

With BOOM at its attract screen, `Chipset::inject_mouse_event()` reaches the
firmware's INT 74h handler only if a driver has sent the mouse its `F4h`
Enable Data Reporting. The mouse sits inert until a driver wakes it (§10.3),
and `I8042::mouse_reporting_enabled()` reports whether that has happened.

The driver is `CTMOUSE`, which `FDAUTO.BAT` runs, but only on the 386+
branch. A live boot shows what happens when that branch is skipped:
`MEM /C /N` lists only `SYSTEM`, `HIMEMX` and `COMMAND`, the INT 33h vector
still points at DOS's own unhandled-interrupt stub (`00D9:1285`, the stub
§9.2's IVT dump records), and the `MEM /C /N` output appears **twice**,
because exactly one path through `FDAUTO.BAT` runs `MEM` twice: `:Only8086`,
which falls through to `:FINAL` and runs it again. The machine is being
treated as an 8086.

`FDAUTO.BAT` decides that with FreeDOS's own CPU-identification tool:

```
if not exist %dosdir%\bin\vinfo.com goto Only8086
vinfo /m
if errorlevel 3 goto Support386
if errorlevel 2 goto Support286
```

`vinfo /m` has to return 3 or more. The whole 386+ branch (`FDAPM`,
`CTMOUSE`, and the CD-ROM setup) depends on it.

`VINFO.COM` is UPX-packed, so the ladder is read out of this machine's own
instruction trace, catching its `INT 21h/AH=4Ch` exit and disassembling the
unpacked image in memory:

```
D3 E8              SHR AX,CL        ; CL=32: unmasked on an 8086, masked to 0 on a 186+
74/75 ...          -> AL=0 (8086)
54 5B 39 D8        PUSH SP/POP BX   ; 286+ pushes the pre-decrement value
                   -> AL=1 (186)
9C B8 00 70 50 9D  POPF 7000h       ; can FLAGS bits 12-14 be set in real mode?
                   -> AL=2 (286)
66 0D 00 00 04 00  OR EAX,40000h    ; AC (bit 18): the documented 386-vs-486 test
                   -> AL=3 (386)
66 B9 00 00 20 00  MOV ECX,200000h  ; ID (bit 21): does POPFD keep it?
                   -> AL=1           ; ...if not
66 B8 01 00 00 00  MOV EAX,1
0F A2              CPUID
C1 E8 08 / 83 E0 0F  SHR AX,8 / AND AX,0Fh   ; AL = family
```

A core passes the first four rungs as a 486 should, including AC, which §4
calls out as *the* pre-CPUID way to tell a 386 from a 486. The last rung
needs CPUID and an EFLAGS.ID bit that sticks. §4.5 gives AP-485's note that
"older versions of Intel486 SX, Intel486 DX and IntelDX2 processors do not
support the CPUID instruction", so an early IntelDX2 genuinely faults on
`0F A2`.

**The part this machine carries has it.** CPUID arrived on the
*SL-Enhanced* 486 line (AP-485 lists IntelDX4 and the SL-Enhanced IntelDX2 /
Intel486 SX / Intel486 DX as having it), and that is the part a 1993-94
"maxed out" build ships, not a first-year DX2. Choosing the earlier stepping
costs the machine its mouse driver, its APM support and its CD-ROM driver,
because the OS it ships with asks the CPU who it is and believes the
answer. So the core is the part the rest of the machine claims it is:

- `FLAG_ID` (bit 21) is real storage and `kPopfdMask`/`kIretdMask` let it
  round-trip, which is what makes AP-485's own detection sequence work.
- `0F A2` implements CPUID. Function 0 returns a maximum input value of 1
  (function 2's cache descriptors are Pentium-era) and `GenuineIntel` in
  EBX:EDX:ECX. Function 1 returns `0000_0433h`, type 0, family 4, model 3,
  which is AP-485's model number for the IntelDX2, and feature flags
  `0000_0001h`: the on-die FPU, and nothing else, because every other EDX
  bit names a Pentium-or-later feature.

`vinfo /m` answers 4, `FDAUTO.BAT` takes `:Support386` -> `:Support386Low`,
and the boot ends with

```
  SYSTEM      68,336   (67K)     HIMEMX       2,192    (2K)
  COMMAND      3,376    (3K)     FDAPM          928    (1K)
  CTMOUSE      3,104    (3K)     UDVD2        1,984    (2K)
  SHSUCDX      6,224    (6K)     Free       567,696  (554K)
```

That is the machine booting its own OS the way that OS intends on a 486,
CD-ROM driver included ("CD-ROM configured as D: drive").

### 13.2 CuteMouse asks the *BIOS* whether a mouse exists

With `CTMOUSE` running on a BIOS that reports no mouse, it prints:

```
CuteMouse v2.1 beta4 [FreeDOS]
Error: device not found
```

Tracing it to its exit and disassembling the unpacked image finds the gate
in three instructions:

```
0EA7  CD 11        INT 11h          ; BIOS equipment list
0EA9  A8 04        TEST AL,4        ; bit 2: pointing device installed
0EAB  74 F7        JZ  give-up      ; -> "device not found"
0EAD  B7 03 / B8 05 C2 / CD 15      ; only then: INT 15h AH=C2h AL=05, initialize
```

The driver never touches the 8042 until the firmware says a mouse is
installed. The BDA equipment word at 0x410 comes from CMOS byte 0x14, which
this BIOS copies into the equipment word. With `0x01` there (floppy
installed and nothing else), the word reads `0021h` and bit 2 is clear.

This is the same shape as §5.3's extended-memory bytes: a CMOS field that
real software gates a whole feature on. The machine has a mouse on the
8042's AUX port, so `Machine::configure_factory_cmos()` seeds byte 0x14 with
`0x05`, floppy plus pointing device, exactly what a real board's Setup/POST
records for a machine with a mouse in the round DIN. `CTMOUSE` then prints
**"Installed at PS/2 port"**, hooks INT 33h, and `mouse_reporting_enabled()`
goes true.

### 13.3 The 8259A's priority resolver keeps mouse packets in order

With a driver installed, injected motion produces INT 74h entries (three per
packet, one per byte, as §10.3 predicts) and Allegro's INT 33h callback
fires. The 8259A's in-service logic decides whether button presses arrive
as buttons or as movement.

The far call into `CTMOUSE` carries the packet on the stack, status, X, Y
from the lowest address up, per the PS/2 BIOS interface, and the driver
reads them at `[BP+1E]`, `[BP+1C]`, `[BP+1A]`. For an injected left-click
(`09 00 00`: bit 3 always set, left button down, no movement) a correct
delivery is **status 09h, X 0, Y 0**. A scrambled one reads status 0, X 0,
Y 9: the status byte lands in the Y slot, the button is delivered as
movement, and a click that should pop up BOOM's menu produces no INT 33h
traffic while motion still works, because garbage that looks like movement
is still movement.

The scrambling comes from re-entrancy. The firmware's INT 74h handler opens
with `STI` (it is a `sti / pusha / push ds ...` wrapper around
`int74_function`), and `I8042` re-asserts IRQ12 inside the very `in(0x60)`
that cleared it, because the next packet byte is already queued, the
behavior §12.1 models as a level check rather than an edge:

```
F000:5A76  MOV DX,60h
F000:04F7  IN  AL,DX        <- the handler reads packet byte 1 (09h)
0070:00BF  ...              <- IRQ12 again, *immediately*, before it stores it
```

A handler re-entered between reading a byte and storing it into the EBDA,
three deep, writes the three bytes into the packet buffer in the reverse of
the order they arrived.

**On real hardware this cannot happen, and the reason is the 8259A.** When
the chip acknowledges an interrupt it sets that line's In-Service bit, and
its priority resolver then refuses to assert INT for anything of *equal or
lower* priority until an EOI clears it. Intel's data sheet calls this fully
nested mode: "interrupts are ... allowed only if they are of higher
priority than the one currently being serviced". A handler cannot be
re-entered by its own device, which is why the firmware can afford to `STI`
on entry.

`pic8259.cpp` sets the ISR bit on `acknowledge()` and
`unmasked_pending()` applies the resolver, in one function:

```cpp
uint8_t in_service_mask() const {
    if (isr_ == 0) return 0xFF;
    return uint8_t((1u << __builtin_ctz(isr_)) - 1u);   // strictly higher priority only
}
uint8_t unmasked_pending() const { return uint8_t(irr_ & ~imr_ & in_service_mask()); }
```

Fixed priority means the highest-priority in-service line is the lowest set
ISR bit, and only requests strictly above it pass. Equal priority, the
line's own bit, is blocked, which is the whole point. The same click then
delivers `status 09h, X 0, Y 0`, and `CTMOUSE` calls Allegro's callback with
**AX=0002h** (left button pressed) and **BX=0001h** (left button down),
which is what BOOM waits for.

This is a core interrupt-controller behavior that every device on the
machine depends on, not a mouse behavior. The mouse is the first device here
whose line re-asserts *inside its own handler*, which is the one case where
a missing resolver is fatal rather than merely untidy. The floppy, the IDE
channels and the keyboard all raise one interrupt and wait to be drained.

### 13.4 The Sound Blaster's DMA direction

With the mouse working, phase 3's audio check judges a stream by its
content. A direction inversion in the DMA byte mover produces a stream that
runs perfectly and carries nothing: 222,824 samples at exactly the 22,727 Hz
the driver programmed, IRQ5 firing 434 times, and **every single sample
identical, at -32768**.

A constant is not audio, but it is also not silence, and the specific
constant is the evidence. Allegro programs this card with `B6h` + mode
`20h`: 16-bit **unsigned** stereo, auto-init, 512-frame blocks. Unsigned
silence is `8000h`; a DAC latching `0000h` gives what `expand16` centers to
exactly -32768. The card is playing a buffer full of zeros, while a 4KB
checksum of the guest's DMA buffer, polled across the run, changes
constantly because Allegro is mixing real audio into it the whole time.

`Chipset::service_sb_dma()` moves bytes in the direction the DSP command
names. `transfer_is_input()` is the DSP command's A/D bit: *input* means
recording, device -> memory. Playback is the other direction, memory -> card.
The floppy's handoff immediately above uses the opposite sense of the
same-looking flag: `fdc.transfer_is_write()` means write-to-*disk* and
therefore memory -> image. With the two branches swapped, every playback
block copies the card's own (zero-initialised) buffer *out over* Allegro's
mixed audio, and `finish_transfer()` turns that same buffer into samples.
The two halves hide each other: the DAC output is consistent with what the
card believes it fetched, and the guest's buffer is corrupted by the
emulator at exactly the rate the guest refills it.

With the direction right, the same run produces 263,217 non-silent samples
spanning the full ±32767 range across 2,634 distinct values; sampled over a
two-second window at the first sound, the waveform has an RMS of 2,793
(about 8.5% of full scale, consistent with BOOM's `sfx_volume 8`), roughly
2,100 zero-crossings per second, effect-shaped, nowhere near noise's
~11 kHz, and no clipping at all.

### 13.5 What `make boom-check` proves

`disks/boom_run_check.cpp` (deleted with `boom-check` in §19.5) checked
three things, and all three had to pass. The renderer bar is §9.6.

**The mouse.** Three facts, measured on screen:

- A **left click during the attract demo brings up BOOM's menu**, with no
  keyboard anywhere in the path: DOOM's own `G_Responder` treats any mouse
  button during demo playback like a keypress and calls
  `M_StartControlPanel`. The menu is a static overlay on a 3D view that
  repaints every frame, so the measurement is "how much of the view stopped
  changing", 8,824 pixels of 53,760 holding still across four frames
  before the click, 11,553 after, and it needs to know nothing about where
  BOOM draws its menu.
- **Motion turns the player's view, by the amount injected.** Walking that
  menu into a live game is deliberately keyboard, so the mouse is the only
  thing under test; a standing player's view is then perfectly still (**0**
  changed pixels over 4.5 seconds). Six `dx=+80` events change **49,336** of
  53,760 view pixels, and six `dx=-80` events bring it back to a frame that
  is **pixel-identical** to the one it started from (**0** differences). A
  view that turns and returns exactly is not a screen that happens to be
  moving: it is BOOM adding up the same movement counts twice, with opposite
  signs.
- **Button 1 fires.** `BOOM.CFG`'s `mouseb_fire 0` makes the left button the
  fire key; a press against that still view changes up to **44,243** pixels
  (muzzle flash and the firing frame of the weapon) and the captured frame
  shows the AMMO counter down from 50.

**The sound.** Judged over the whole run, with bars written so a
direction inversion like §13.4's fails them: a constant fails (at least 256
distinct sample values), a DC level fails, a stream that never rises above
the noise floor fails (peak amplitude at least 4096), a silent stream fails,
IRQ5 never firing fails, and playing *faster* than the programmed rate fails,
the same CLAUDE.md-mandated realism guard `PlaybackIsPacedAtTheRealSample
RateAndNeverFaster` puts on the device, measured here against real
third-party software's own rate. The run reports 1,183,155 samples over
52.06 s of emulated time = 22,727 Hz against a programmed 22,727 Hz, ratio
1.000.

The `--sb-trace` flag logs every DSP state change (rate, width,
channels, speaker gate) as it happens; the header comment documents it with
the rest.

### 13.6 What this costs, and the real-mode boot cycle

- `hdd_boot_check` reaches its idle `C:\>` at cycle **1,645,095,079**. The
  **1,409,073,889** that §8.3 and §9.6 record is the figure for a boot
  without the 386+ branch: the boot runs `FDAPM`, `CTMOUSE`, `UDVD2` and
  `SHSUCDX` (§13.1). Conventional memory free is 554K, against 566K without
  them. Real mode's own timing is unchanged; the identical-cycle check
  measures the earlier boot.
- `make boom-check` took about 100 s of wall clock, all of it phase 2
  playing BOOM. No clock was sped up to pay for it; this harness had no
  equivalent of the `fast=1` flag CLAUDE.md sanctions for the browser
  suites, and should not acquire one.

### 13.7 Tests

`make check`, `pm-check` 24/24, `vbe-check` all-pass, `hdd-boot-check` and
`boom-check` were green.

- `pic8259_test.cpp` has `ALineInServiceCannotInterruptItselfUntilEoi`
  (the mouse case, stated as the chip behavior it actually is),
  `HigherPriorityLinePreemptsOneInService` (the other half of fully nested
  mode, the timer must still tick through a slow handler),
  `AutoEoiSetsNoInServiceBitSoNothingIsBlocked`, and
  `SpecificEoiUnblocksOnlyTheNamedLine`.
  `AcknowledgeClearsIrrAndSetsIsrUntilEoi` pins the acknowledge side.
- `chipset_test.cpp` has
  `MousePacketBytesArriveOneInterruptAtATimeInOrder` (the whole §13.3 sequence
  end to end: three interrupts, correct AUXB tagging, correct byte order,
  and no delivery until EOI),
  `SoundBlasterPlaybackDmaReadsMemoryIntoTheCard` (which also asserts that
  playback leaves memory untouched, the half of §13.4 that protects
  the guest),
  `SoundBlasterRecordingDmaWritesTheCardsSamplesIntoMemory`, and
  `SoundBlasterBlockEndRaisesIrq5`.
- `cpu80486_test.cpp` has `CpuidFunctionZeroReportsGenuineIntelAndAMaximumInputOfOne`,
  `CpuidFunctionOneReportsAnIntelDx2SignatureWithOnlyTheFpuFeature` and
  `TheIdFlagRoundTripsSoSoftwareCanDetectCpuid` (AP-485's own detection
  sequence, which is the one FreeDOS runs). `PopfdDoesNotSetReservedOrVirtual
  ModeBits` excepts bit 21 and asserts it explicitly.
- `machine_test.cpp` asserts CMOS 0x14 bit 2, with CuteMouse's `INT 11h /
  TEST AL,4` as the reason.

### 13.8 What this says about where defects live

§9.9 makes the case that only real third-party software samples the
instruction set the way period compilers emitted it. This section makes the
same case one layer out: **the two devices are correct on their own, and all
four of this section's pieces are in what surrounds them.** Two devices,
unit tests between them for every register and response byte against
Creative's and Chapweske's own documents, and the mouse is still inert if a
CMOS bit is clear and the CPU will not say what it is, its packets are
scrambled without a priority resolver in the interrupt controller, and the
audio is silence if two `for` loops assign in the wrong direction. No
device-level suite finds any of those.

A unit test proves a component answers correctly when asked correctly. Only
real software proves anything about who does the asking: whether the OS ever
loads the driver, whether the driver believes the hardware is there,
whether the interrupt survives the trip, whether the bytes go the way they
are supposed to go.

### 13.9 Sources

- **Intel 8259A data sheet, "Fully Nested Mode" and "Priority Resolver"**,
  the in-service block behind §13.3: a request is granted only when it
  outranks everything currently in service, with equal priority blocked, and
  EOI as the only thing that lifts it.
- **Intel AP-485, "Intel Processor Identification and the CPUID
  Instruction"**, which Intel486 parts carry CPUID (IntelDX4 and the
  SL-Enhanced IntelDX2/486 SX/486 DX; the older parts fault), the EFLAGS.ID
  toggle as the documented way to detect it, the vendor-string layout, and
  the family/model table that makes family 4 model 3 an IntelDX2.
- **FreeDOS 1.3's own `VINFO.COM` and `FDAUTO.BAT`**, as shipped on this
  machine's disk image, cited as the primary evidence for §13.1: the
  CPU-generation ladder disassembled in §13.1 is read straight out of this
  machine's instruction trace, and `FDAUTO.BAT`'s `if errorlevel 3 goto
  Support386` is what makes the answer decide whether the machine gets a
  mouse driver at all.
- **CuteMouse 2.1 beta4 (`CTMOUSE.EXE`)**, likewise from the shipped image
 , the `INT 11h / TEST AL,4` gate in §13.2 and the INT 15h AH=C2h sequence
  behind it, read from this machine's own trace.
- **Bochs BIOS `rombios.c`** (the ROM this machine boots), `int74_function`
  and its `sti`-on-entry assembly wrapper, the EBDA packet buffer it
  assembles into, and the far call whose stack layout (status, X, Y, Z) is
  what §13.3 reads the scrambling off. Cited for what the firmware
  *requires* of the hardware, per §7.11's rule.
- **Creative, *Sound Blaster Series Hardware Programming Guide***, the
  `Bxh` command and mode-byte bit fields (§11.6's citation), which is what
  says mode `20h` is 16-bit *unsigned* stereo and therefore what makes
  `0000h` in the buffer full-scale negative rather than silence.
- **Allegro 3.x `sb.c` and its DOS mouse driver**, what BOOM's driver
  actually issues: `B6h`/mode `20h` at 22,727 Hz in 512-frame auto-init
  blocks on the 16-bit channel, and the INT 33h function 0Ch callback
  (registered with CX=007Fh, all events) through which button and motion
  events reach the game.
- **DOOM/BOOM `g_game.c` and `m_menu.c`**, `G_Responder`'s demo-abort
  branch (`ev->type == ev_mouse && ev->data1` pops up the menu), which is
  what phase 2's click test relies on, and `BOOM.CFG`'s `use_mouse 1` /
  `mouseb_fire 0` settings the shipped image carries.

## 14. BOOM throughput, and where the cycles go

BOOM is noticeably slow in the browser, on more than one host. §8 gives this
machine 76.5 M cycles/sec against a 66.0 MHz requirement, and §12/§13 add
per-instruction work to `Chipset::tick()` (the Sound Blaster's tick and DMA
service, the mouse's IRQ12 level check, IRQ5's edge check) and a priority
resolver to the 8259A that `has_interrupt()` runs twice per instruction. The
obvious hypothesis is that those additions ate the headroom. Measured, they
did not, and the cost sits somewhere else entirely.

### 14.1 §8's 76.5 M/s does not describe this workload

§8.1/§8.3 benchmarked `runCycles(20_000_000)` "at an idle FreeDOS `C:\>`
prompt". Two things make that number inapplicable to BOOM:

- It is **real mode with paging off**, where `seg_linear()` is one add and
  `translate()` returns its argument. BOOM is 32-bit protected mode under
  CWSDPMI with paging enabled, so every guest byte pays a segment limit
  check and a page translation.
- Since §13.1 the boot loads `FDAPM`, which halts the CPU when DOS is idle.
  The same benchmark at the same prompt reports **394 M cycles/sec**, which
  is not interpreter throughput at all: it is `Cpu::step()`'s
  `if (halted) { cycles += 4; return 4; }` measured 5 million times. The
  idle prompt is not a usable benchmark.

Measured where the complaint lives, the same
`runCycles(20_000_000)` timing loop, taken with BOOM in mode 13h at its
attract demo, real-speed page (`?test=1`, no `fast=1`):

```
36.1  36.3  36.9  37.0  38.9   M cycles/sec
```

Against 66.0 M/s required. The pump (§8.4) sheds the excess rather than
blocking, so BOOM runs at about 60% of real speed instead of freezing the
tab. `smoke.spec.ts`'s
33-99 M/s band covers this, for the same reason §8.7 gives: the band is
wide and the test measures a real-mode boot, not a game.

### 14.2 Where the cycles go, from two profilers

Native (`sample`, `-O2 -g`, `disks/boom_run_check` (deleted, §19.5) during phase 2, 25 s,
top of stack) and in the browser (Chrome's sampling profiler over a CDP
session, against the shipped `-O3 -flto` wasm rebuilt with
`--profiling-funcs` so the name section survives, 10 s under BOOM). The two
agree:

| wasm, self time                    | share |
| ---------------------------------- | ----- |
| `runCycles` (everything LTO inlined into it) | 43.9% |
| `Cpu::translate`                   | 16.5% |
| `Cpu::decode_modrm`                | 7.7%  |
| the `Bus` memory-read thunk        | 6.7%  |
| `Cpu::seg_linear`                  | 6.7%  |
| `Cpu::read32`                      | 5.9%  |
| `Cpu::write32` + the write thunk   | 3.2%  |

**About 39% of the whole run was the logical -> linear -> physical path**,
and the native profile (which does not inline across translation units, so
it can attribute what wasm folds into `runCycles`) puts the chipset at
roughly 19%: `Chipset::tick` 8.9%, `Pit8253::tick` 3.6%, `SoundBlaster::
advance` 2.6%, `Chipset::service_sb_dma` 1.8%, `PcSpeaker::update` 1.7%.

That 19% answers the opening hypothesis. Everything §12 and §13 add (the SB
tick, the DMA service call, the mouse and IRQ5 checks) is inside the Sound
Blaster's 2.6% plus the DMA service's 1.8%, and both are only non-zero
because BOOM is playing audio. The 8259A's `in_service_mask()` does not
appear in either profile at all; it is a compare, a `__builtin_ctz` and a
subtract, inlined into `Machine::run_cycles`, whose entire self time is 2.7%
including the run loop itself. **The mouse and Sound Blaster work costs on
the order of 5%, not 45%.**

The build flags are `web/Makefile`'s `EMFLAGS`: `-O3 -flto -fwasm-exceptions
-sSUPPORT_LONGJMP=wasm`, the §8.3 settings with no regression.

### 14.3 One segment check and one page walk *per byte*

`read16`/`read32`/`read64` composed out of `read8`, and `write16`/
`write32`/`write64` out of per-byte `seg_linear()` + `translate()` pairs,
run **four** full segment-limit checks and **four** page translations for a
32-bit read, and eight of each for a 64-bit FPU load. In real mode both are
nearly free, which is why §8 does not show it. Under BOOM they are the
single largest cost in the machine.

A real 486 does neither. Intel's 80486 PRM has the segmentation unit check
the whole access against the limit before any bus cycle is run, and the
paging unit consult the TLB once per page touched, not once per byte.

Three changes leave *what* the machine does alone:

- **`Cpu::access_phys()`** resolves a whole `size`-byte access with one
  `seg_linear()` and one `translate()` when the access is a contiguous run
  of offsets inside a single page. It refuses, and the byte-at-a-time
  path runs, when the run wraps real mode's 64KB boundary, overflows 32
  bits, or straddles a page boundary, which keeps a straddling access
  faulting where it should. `read16/32/64` and `write16/32/64` try it first.
  An access whose *first* byte is inside the segment limit and whose last
  is not raises #GP before reading byte 0, which is what the PRM says the
  segmentation unit does.
- **`seg_linear()` and `translate()` are split into an inline fast path and
  an out-of-line `_slow()`.** The fast paths cover the ordinary present,
  expand-up, correctly-typed segment and the TLB hit; `_slow()` is the
  complete original function, entered in exactly the cases the fast path
  declines, so a stale TLB entry still faults off its own cached rights
  rather than off a fresh walk. Same §8.3 playbook as the device ticks.
- **Three more per-instruction calls made cheap the same way.**
  `PcSpeaker::update()` (an AND gate and a compare that changes nothing on
  a silent machine) and `Pit8253::tick()`'s credit accumulation move into
  their headers, with the PIT's channel stepping left out of line behind an
  `if (whole == 0) return 0;`, at 66 MHz the PIT advances one count every
  ~55 CPU cycles, so almost every call ends there. `Chipset::tick()`
  guards `service_sb_dma()` behind the device's own inline
  `sb.transfer_ready()` flag, the same shape as `run_cycles()`'s
  `has_interrupt()` guard.

Measured, same benchmark as §14.1:

|                                  | before        | after          |
| -------------------------------- | ------------- | -------------- |
| wasm, BOOM at its attract demo   | 36.1-38.9 M/s | **38.9-41.4 M/s** |
| wasm, halted at the idle prompt  | 389-394 M/s   | **477-481 M/s**   |

In the re-profile `translate` and `seg_linear` do not appear as frames at
all, since their fast paths fold into `read8`/`read32`/`access_phys`, so the
share table is not comparable line for line with the earlier one, and the
throughput figures above are the measure of the change.

### 14.4 What this does not fix, and the honest number

**BOOM still does not reach 66 MHz on this host.** ~40 M/s against 66.0 is
about 60% of real speed, and §8.4's pump means the visible result is a game
running slow rather than a tab that stops answering. §14.3 is an
improvement of roughly 8% on a shortfall of 65%, which is not a fix for the
reported symptom.

What the profiling establishes is where the remaining work is, none of it
in the chipset:

- **The `Bus` memory thunks, 8-10%.** Every guest byte crosses
  `Chipset::mem_read`/`mem_write`, which re-checks the A20 gate, the VGA
  window, the RAM bound and (for writes) a bit-packed `vector<bool>` ROM
  map. Those four checks are per *byte*; they are properties of a 4KB
  physical page. A page-resolution cache in the CPU (physical page -> host
  pointer plus writability, filled by one chipset query) collapses them, and
  is the largest single win (§15.2).
- **Instruction fetch.** `fetch8`/`fetch16`/`fetch32` take the full
  logical -> physical path for every opcode and displacement byte, so a
  five-byte instruction pays five translations before it executes. Real
  silicon has a prefetch queue and a separate code TLB (§15.3).
- **`decode_modrm` at 5-8%**, and `step_inner`'s switch dispatch inside the
  `runCycles` blob.

None of these is a defect. They are the cost of a byte-at-a-time interpreter
meeting 32-bit paged protected-mode code. Recording them with numbers
attached means a pass at this starts from the memory path, not from the
chipset.

### 14.5 Tests

`make check`, `pm-check` 24/24, `vbe-check` all-pass. The case that pins
§14.3's one behavior change is `cpu80486_test.cpp`'s
`AnAccessStraddlingTheLimitFaultsBeforeMovingAnyByte`: a write whose first
bytes are inside the segment limit and whose last are not raises #GP with
memory untouched. `make boom-check` was green on all three phases, and its
figures were **identical to the ones §13.5 records**: 8,824 -> 11,553 still
pixels around the menu click, 49,336 view pixels turned and 0 on the way
back, 44,243 on the shot, 1,183,155 samples at 22,727 Hz against a
programmed 22,727 Hz. A pixel-for-pixel and sample-for-sample identical BOOM
run is the strongest available statement that none of this changes what the
machine does.

## 15. The bus stops being a per-byte question, and so does instruction fetch

§14.4 names three targets: the `Bus` thunks, instruction fetch, then
`decode_modrm`. Profiled on this build, the thunks and fetch are the same
cost, because almost every byte crossing a thunk is an instruction byte.

### 15.1 What the profiler showed

`sample` over `disks/boom_run_check`'s workload (`-O2 -g`, BOOM in mode 13h
at its attract demo, 25 s, ~19,300 samples, top of stack):

| self time                          | share |
| ---------------------------------- | ----- |
| `Cpu::read8`                       | 18.3% |
| `Chipset::tick`                    | 14.9% |
| `Cpu::step_inner`                  | 13.3% |
| `Cpu::decode_modrm`                | 9.2%  |
| the `Bus` memory-read thunk        | 7.9%  |
| `Cpu::access_phys`                 | 6.9%  |
| `SoundBlaster::advance`            | 3.4%  |
| `Cpu::read32`                      | 2.9%  |

Attributing `read8` to its callers is what settles the ranking: 1,689
samples from `step_inner`, 1,331 from `decode_modrm`, 269 from `two_byte`,
94 from `grp1_immed`, 50 from `grp2_shift`, **97% of it is `fetch8`**, and
`rm_read8` is 17 samples. The same holds a level down: of the read thunk's
samples, 416 come from `decode_modrm` and 375 from `step_inner`. A five-byte
instruction pays five segment checks, five page translations and five
indirect calls into the chipset *before it executes*, and the thunk cost
§14.4 measures is mostly paid on its behalf.

### 15.2 A physical page resolves once, not once per byte

`Chipset::mem_read`/`mem_write` re-derive four things per byte, the A20
gate, the VGA window, the RAM bound, and (for writes) a bit-packed
`vector<bool>` ROM map. All four are properties of a 4KB *page*.

`Chipset::page_host(page_base, write)` answers them once and returns a host
pointer into `mem`, or `nullptr` when the page is not plain memory: the VGA
window, unpopulated space above `mem`, or, for a write, a page holding
any ROM byte (a page-granular `rom_page_` mirror of `rom_`, so a partly
-ROM page is refused rather than scanned). `nullptr` is not a failure; it
sends that access back through the byte-at-a-time thunks, which is what
keeps the VGA window, ROM write-protection and open bus behaving as real
hardware does.

The CPU caches the answer in a 64-entry direct-mapped table per direction
and uses it from `read8`/`write8` and from every access `access_phys()`
already resolved. Caching a resolved translation is what the silicon does
too. It sits a step below the TLB, not as a shortcut past it.

**Invalidation is the whole correctness question**, and it has one real
trigger: A20. `Chipset` keeps a generation counter the CPU reads (one L1
load per access) and bumps it whenever an already-resolved page could now
resolve differently, the gate moving, a `load_rom`, a reset. The gate is
the 8042's output-port bit 1 and nothing else on this machine, so it is
checked after every write to the controller (where a guest moves it) and
once per `tick()` (so a host poking `kbc` directly is caught too). VGA bank
switching needs no trigger: the window is never handed over in the first
place.

The `Bus` carries the pair as *optional* members, bound by `Bus::For` only for
a host that offers them (`if constexpr` on a detection trait). The three
test fixtures that build a `Bus` over a bare byte array use the
byte-at-a-time path.

### 15.3 Instruction fetch gets a prefetch window

With the thunks out of the way, fetch would still pay a limit check and a
page walk per byte. `prefetch_fill()` resolves the run of EIPs sharing one code page once,
and `fetch8`/`fetch16`/`fetch32` read the rest out of a host pointer. It
declines, leaving an empty window, so every fetch takes the original path,
for a run that crosses a page, passes the segment limit or real mode's
64KB wrap, or sits on a page the bus will not hand over. A real 486 has a
prefetch queue and does the same thing for the same reason.

The window survives across instructions, because clearing it at the top of
every instruction is safe but costs one refill per instruction (`prefetch_fill`
at 12% of the run), a third of the available gain rather than all of it. It
is re-validated once per instruction against every piece of state its
EIP -> host-pointer mapping rests on: CS's cached descriptor (base, limit,
access, D/B, null), CR0, CR3, CPL, a counter every TLB flush and INVLPG
bumps, and the bus generation above. Comparing those *values*, rather than
clearing the window at each site that might write them, is what makes it safe
without an enumeration to get wrong, and it rests on the same architectural
contract the TLB already does: software that changes a page mapping issues
an INVLPG or reloads CR3. Nothing mid-instruction can move any of them,
because a CS load, a CR3 write or a mode change ends the instruction it
happens in. And because the window holds a pointer into live memory rather
than a copy, code that patches itself ahead of EIP still executes the
patched byte.

### 15.4 The numbers

Same benchmark as §14.1 (`Machine::total_cycles()` over a real-time window,
BOOM at its attract demo), native, built with the shipped module's `-O3
-flto` so the inlining matches; baseline is this same source with
`Bus::For`'s page binding disabled, so both sides are one build apart:

|                     | before        | after             |
| ------------------- | ------------- | ----------------- |
| BOOM, attract demo  | 58.9-69.2 M/s | **86.2-97.8 M/s** |
| mean                | 62.3 M/s      | **90.0 M/s**      |

**+44%.** These are native figures, not the browser's: §14.1's 36-41 M/s
were measured in wasm, and §16.2 measures the wasm build in a browser.

The re-profile shows where the interpreter spends itself: `step_inner`
28%, `Chipset::tick` 22% (inflated natively, where the device ticks do not
inline across translation units the way LTO makes them in the wasm build),
`decode_modrm` 13%, then `Cpu::step` 5%, `SoundBlaster::advance` 4.9%,
`access_phys` 4.8%. `read8` fell from 18.3% to 1.5%, `read32` to 1.2%,
`prefetch_fill` and the `Bus` thunks out of the listing entirely, and
`Chipset::page_host` never appears at all, the page cache essentially
never misses.

`decode_modrm`, §14.4's third target, was profiled rather than assumed: most
of its apparent cost was the fetching *inside* it (1,331 `read8` and 597
`read32` samples charged to it), which the fetch path removes. What remains is genuine
decode, the ModR/M and SIB address arithmetic, and it is third behind
the interpreter's own dispatch. Anything further is a restructuring of the
interpreter, not another cache.

### 15.5 Tests

`make check`, `pm-check` 24/24, `vbe-check` all-pass. Seven cases pin the
parts that have to keep working: `page_host()` refusing the VGA window, open bus and a ROM page for
writes while handing over plain RAM; the generation counter moving for a
gate change, a `load_rom`, and a gate change made outside `io_out`; and four
cases that run real guest code through a real `Machine`, a store to ROM
through an already-resolved page, a store and a load in the VGA window
neither reaching nor reading the RAM behind it, opening A20 mid-run and
watching the next store to the same offset land a megabyte higher, and code
that patches a byte ahead of EIP on its own page and executes the patch.

`make boom-check` was green on all three phases, with figures **identical to
the ones §13.5 and §14.5 record**, 8,824 -> 11,553 still pixels around the
menu click, 49,336 view pixels turned and 0 on the way back, 44,243 on the
shot, 1,183,155 samples at 22,727 Hz against a programmed 22,727 Hz. The
same pixel-for-pixel and sample-for-sample identical run §14 relies on, for
the same reason: it is the strongest available statement that none of this
changes what the machine does.

## 16. Real speed in a browser, and the last call left in the decoder

§15.4's figures are native, and BOOM's actual frame rate needs the wasm
build measured in a browser. This section is that measurement. **BOOM holds
real time in the browser**, which is the symptom §14 opens with.

### 16.1 The decoder and chipset-gate changes behind this section

Code comments cite "PC486_REVIEW.md §16" for the changes below. They are
listed with the figures each shipped with. **Those per-change shares are the
ones recorded in the comments, not re-measured here**; everything in §16.2
onward is measured.

| change | what it takes out of every instruction |
| ------ | -------------------------------------- |
| `kPrefix`, a 256-entry table, replaces the prefix loop's switch | an eleven-way branch on an answer that is "not a prefix" nearly every time (4.5% of a BOOM run) |
| the dense ALU block 0x00-0x3D decoded ahead of the main switch | a second dispatch for the opcodes a compiler emits most |
| `decode_modrm()` resolves a register operand and a plain base register inline | an out-of-line call measured at 14.8% of a BOOM run in wasm |
| `RM` shrunk to 8 bytes | a hidden return slot on every decoded operand |
| `always_inline` on `fetch8` and the `rm_read*`/`rm_write*` pairs | a call for a two-instruction helper, which `step_inner`'s size had pushed past the inliner's budget |
| `prefetch_revalidate()` folds its ten compares into one XOR/OR test | nine short-circuiting branches on the emulator's most frequently executed line (11.5%) |
| the GPR file as a `kGpr32`/`kGpr8` pointer-to-member table | an eight-way branch that never predicted, since the ModR/M field differs on nearly every instruction |
| `Pic8259::pending_`, memoized | re-resolving priority twice per instruction (5.2%) |
| `Chipset::tick()`'s `next_service_` gate, with `Pit8253::cycles_to_next_count()` | the whole device service pass, on all but one instruction in ~55 |

The gate is the one with a real argument behind it rather than an inlining
decision, and chipset.h states it: nothing on this board is clocked by the
CPU, the 8253's 1.193182 MHz is the fastest clock among them, and no device
state can become observable sooner than the next count of it. Any port
access re-opens the gate, so a guest polling a device is still serviced on
every poll, and A20 is still checked on every call.

(The same set includes one change that is not about throughput:
`RenderEgaNative16Screen` sizes mode 0x10 from the CRTC's own horizontal and
vertical display-end registers rather than a table, verified against what
this machine's real video BIOS programs for that mode.)

### 16.2 What a browser does

Same benchmark as §14.1, `Machine::totalCycles()` over synchronous
`runCycles(20,000,000)` windows, BOOM in mode 13h in a live game, headless
Chromium 1.63 driving the real page, the shipped `-O3 -flto` module. Host is
an Apple M4.

|                                   | wasm, in a browser |
| --------------------------------- | ------------------ |
| §14.1, before the §14 work        | 36.1-38.9 M/s      |
| §14.3, after it                   | 38.9-41.4 M/s      |
| this section, before §16.4        | 111.8-113.5 M/s (mean 112.5) |
| this section, after §16.4         | 115.1-116.6 M/s (mean **116.0**) |

Against 66.0 M/s required. The browser is **1.76x faster than the machine
it is emulating**, against 60% of real speed in §14, and almost all of that
is §15's page cache and prefetch window plus §16.1's list.

The figure that answers the complaint is the other one. On the
genuinely real-speed page (`?test=1`, no `fast=1`), measured over a 10-second
window with BOOM running, rendering and audio pumping exactly as a visitor
gets them:

| pump-driven rate, real-speed page | 67.7 M/s against a 66.0 MHz target |
| --------------------------------- | ---------------------------------- |

That is real time, with the ~2.6% over the target being the measurement
window and the pump's credit accounting rather than any speed-up: the pump
asks for `dt * 66,000,000 * 1` and `TEST_CPU_MULTIPLIER` is 1 on that page.
§8.4's shedding sheds nothing, because there is nothing to shed.

This is one host. The improvement factor is what carries to a slower one,
not the 116.

### 16.3 Where the time goes, in wasm rather than natively

§15.4's ranking is native and says so, flagging `Chipset::tick`'s 22% as
inflated by the native build's lack of cross-TU inlining. Profiled in wasm
with Chrome's sampling profiler over a CDP session, against the shipped module
rebuilt with `--profiling-funcs` so the name section survives, ~29,900
samples under BOOM:

| wasm, self time                     | share |
| ----------------------------------- | ----- |
| `runCycles` (everything LTO inlined into it) | 61.3% |
| `Cpu::access_phys`                  | 7.7%  |
| `Cpu::decode_modrm_slow`            | 7.2%  |
| `Cpu::read32`                       | 4.7%  |
| `Cpu::alu_apply32`                  | 4.1%  |
| `Cpu::write32`                      | 2.2%  |
| `Cpu::fetch32`                      | 2.1%  |
| `Cpu::read8`                        | 2.0%  |
| the `Bus` memory-write thunk        | 1.3%  |

The native ranking is misleading, in the direction it warns about:
**`Chipset::tick`, `Pit8253::tick`, `SoundBlaster::advance`,
`PcSpeaker::update` and `Chipset::page_host` do not appear in the wasm
profile at all.** §16.1's service gate is why. The chipset, which opened
§14 as the prime suspect and was 19% of the run then, is unmeasurable.

### 16.4 SIB forms in the inline decoder

`decode_modrm_slow` at 7.2% is not a slow path being taken rarely. §16.1's
inline decoder covers a register operand and a plain base register; what it
sends out of line is every form with a SIB byte, which is how 32-bit
compiled code reaches its locals and its arrays, so DJGPP-built BOOM is full
of them.

`decode_modrm()` resolves **every** 32-bit SIB form inline: base, index
with its scale, the `index == 4` encoding that means no index, the
`base == 5, mod == 0` encoding that means disp32 instead of a base, and the
ESP/EBP-base default to SS. Covering all of them is what lets it consume the
SIB byte without a bail-out path that would have to un-fetch it. The
published 486 effective-address penalty, base+index+displacement costs one
extra clock, every other form nothing, is charged here exactly as
`decode_modrm_slow` charges it, so no instruction's cost moves.

|                              | SIB out of line | SIB inline |
| ---------------------------- | --------------- | ---------- |
| wasm, BOOM in a live game    | mean 112.5 M/s | mean **116.0 M/s** |
| `decode_modrm_slow`, self time | 7.2% | 2.1% |
| shipped `pc486.wasm`         | 231,957 B | 251,162 B |

**+3.1%**, for 19KB of code. What is left in `decode_modrm_slow` is 16-bit
addressing and the bare disp32 form, neither of which 32-bit BOOM runs often.

Two things were measured and are *not* kept, which is the more useful half of
a profile:

- **`access_phys` inlined into the header**, the obvious read of its 7.7%.
  Measured 112.8 M/s against a 112.5 baseline, inside the ~0.4% spread
  between repeat runs of the same build, so: nothing. That 7.7% is the
  segment check and the page walk themselves, work that moves into
  `runCycles` when inlined rather than disappearing. Not kept.
- **Single-word loads and stores** for `read32`/`write32`'s host-pointer
  path, which compose their result a byte at a time. Checked before writing
  any code, by compiling the pattern on its own: clang already emits one
  unaligned `i32.load`/`i32.store`/`i64.load` for each. Nothing to win.

### 16.5 What is deliberately not done

The profile after §16.4 has no next candidate of the same kind. `runCycles`
is 66% and is the interpreter itself, `step`, `step_inner`, the dispatch,
the inline decoder, the chipset gate, all folded together by LTO. Everything
under it is genuine work rather than call overhead: `access_phys` is the
segment check and the page walk (measured, above), `alu_apply32` at 4.2% is
the arithmetic and its flags, `shld` at 1.0% is Doom's fixed-point math, and
`translate_slow` at 1.0% is real TLB misses.

The one structural item left is the `Bus` write thunk's 1.3%: BOOM writes
64,000 bytes of mode 13h frame per frame into the VGA window, which
`page_host()` refuses by design (§15.2) because the card answers there, not
RAM, latches, plane masks and chain-4 addressing all live in that write.
Handing a host pointer to it would mean reproducing those, for about 1.3%,
on a machine that already runs 1.76x faster than real time. Not worth the
risk, and it stays unattempted.

Anything beyond that is a restructuring of the interpreter, and this core
is deliberately an interpreter with real per-instruction cycle accounting,
so a basic-block cache or a JIT is not on the table at any speed.

### 16.6 Rebuilding the Full-set HDD image from this repo

Nothing in `pc486/` copies BOOM onto the disk: `build_freedos_hdd.cpp` runs
the real FreeDOS installer and stops, while `boom_run_check` expected
`C:\GAMES\BOOM` to hold `BOOM.EXE`, `CWSDPMI.EXE` and `DOOM.WAD` already.
Every other pinned asset here has a `fetch-*.sh` with a published checksum.
The pinned FreeDOS LiveCD already carries all of it:

- `packages/games/boom.zip` is BOOM 2.02 itself, `BOOM.EXE`,
  `CWSDPMI.EXE` (r5, the build §9.7 names), `GO32-V2.EXE`, `BOOM.CFG`, and
  the installer **puts it at `C:\GAMES\BOOM`**, unprompted.
- `packages/games/freedoom.zip` is Freedoom Phase 1, whose
  `GAMES/FREEDOOM/PHASE1/DOOM.WAD` the installer also lands, at
  `C:\GAMES\FREEDOOM\PHASE1\DOOM.WAD`.
- `FDAUTO.BAT` sets `BLASTER` (§11's `A220 I5 D1 H5 T6`), so BOOM's
  Allegro driver finds the card with nothing added.

So the whole missing step is copying one WAD the image carries into the
directory BOOM is installed in. **One caveat matters for reading §13.5,
§14.5 and §15.5:** their pixel counts belong to a 17,659,828-byte
`DOOM.WAD`, and no Freedoom release nor the pinned LiveCD contains a WAD of
that size. The one FreeDOS ships is 27,284,988 bytes (SHA-256 `c403ea7c...`,
identical to Freedoom 0.12.1's `freedoom1.wad`). Those figures are therefore
not reproducible from pinned media, and an image built from the pinned media
reports its own (§16.7).

Two build details come with it:

- **`build_freedos_hdd.cpp`'s `SendExtendedKey` leaves a real gap per byte.**
  An arrow key's `0xE0` prefix and its make code cannot be injected in the
  same instant. The 8042 has one output register, and a byte queued behind
  an unread one is not announced again until the guest's handler drains the
  first, so the `48` would sit undelivered and every scancode after it would
  queue behind it. The installer's partition prompt is the first one
  answered with an arrow key; it never advances, and the run fails after 30
  nag presses. Measured directly: with the power-on BAT byte drained first,
  `E0 48` raises IRQ1 once, the handler's read of 0x60 takes the `E0`, and
  nothing raises IRQ1 for the `48`. §16.1's service gate changes none of it.
  One real gap per byte is what a keyboard clocking one bit at a time does
  anyway, and the rule this machine's own front end follows for every
  multi-byte Set 1 sequence (`web/app.js`'s `injectScancodeSequence`,
  `helpers.ts`'s `tap`). The asymmetry underneath is §17.1:
  `Chipset::service()` edge-detects `kbc.irq1_pending()` against a previous
  sample, while the mouse's IRQ12 is consumed as the one-shot it is
  (`raise(4)` then `clear_irq12()`, §13); `clear_irq1()` exists with no
  caller. On real hardware the read drops IRQ1 and the next byte re-asserts
  it, a genuine edge, and that transition happens entirely inside one
  `in(0x60)` call here, where sampling the line cannot see it.
- **`web/Makefile` depends on its own headers.** This machine's hot paths
  (the decoder's fast path, the prefetch window, the page cache, every
  device's inline tick) are in the headers, and a `pc486.js: $(SRC)` rule
  that lists only the `.cpp` files can leave the previous module in place on
  a header-only change, which measures as having done nothing. It is the
  same family of trap `serve_nocache.py` exists for (§15).

### 16.7 Tests

`make check`, `pm-check` 24/24, `vbe-check` all-pass. Three cases pin what
§16.4's inline decoder answers for itself: a SIB with an EBP base defaulting to SS, a segment
override beating a SIB's ESP-base default, and, in the timing suite, where
the published effective-address penalty had only ever been checked on 16-bit
addressing, the same +1 clock charged on the 32-bit SIB forms, and not
charged on the three forms that do not earn it.

`make boom-check` was green on all three phases, with **every figure
identical with SIB forms inline or out of line**: 24 consecutive differing 320x200x256 frames; 4,884
-> 675 still pixels around the menu click; 49,662 view pixels turned and 0 on
the way back; 43,302 on the shot; 1,216,400 samples at 22,727 Hz against a
programmed 22,727 Hz, ratio 1.000, with 2,375 IRQ5 edges. A pixel-for-pixel
and sample-for-sample identical run is the same standard §14.5 and §15.5
used, for the same reason.

Those absolute numbers are not §13.5's, and §16.6 says why: this image's
`DOOM.WAD` is the one the pinned FreeDOS LiveCD ships, not the
17,659,828-byte WAD §13.5's figures were taken against. The proof is the
identity across the two decoder paths on one image.

## 17. IRQ1 is level-checked like IRQ12

§16.6 records a keyboard freeze with extended keys. It also shows in typing
an ordinary DOS command line, not just BOOM's extended-key sequences. The
cause is how `Chipset::service()` samples IRQ1.

### 17.1 The IRQ1/IRQ12 asymmetry §16.6 flags

`Chipset::service()` has to treat IRQ1 (keyboard) and IRQ12 (mouse) the same
way. IRQ12 is level-checked every service pass,
`if (kbc.irq12_pending()) { pic_slave.raise(4); kbc.clear_irq12(); }`, for a
reason: the controller's shared output register re-fills and re-asserts the
pending line inside the very same `in(0x60)` call that cleared it, whenever
a second byte was already queued, so no 1->0->1 transition is ever
observable at the service pass's own granularity. An IRQ1 that compares
against a `kbc_irq_prev_` sample from the previous pass and raises only on a
0->1 edge drops a second queued byte's interrupt outright, for the identical
reason.

A keyboard byte reaches `i8042.cpp`'s shared FIFO the same way a mouse byte
does (this file's own header: "no byte is ever lost, only delayed"), and
gets there just as easily two-at-a-time: the extended-key 0xE0 prefix pair
§16.6 hit, a multi-byte command response, or, what the browser was
actually reporting, two ordinary keystrokes landing close enough together
that the guest's own ISR hasn't drained the first when the second is
already queued behind it. Once that happens, the second byte sits in the
output register, genuinely present (`OBF` set, `irq1_pending()` true), but
edge-detection at service-pass granularity never tells the PIC, and a
purely interrupt-driven keyboard handler, which is what DOS and every
Set-1 keyboard ISR is, never reads it. Everything typed after that byte
queues up behind it forever, which is the freeze.

### 17.2 The test that pins it

`chipset_test.cpp`'s `SecondQueuedKeyboardByteStillReachesThePic` enqueues
two scan codes (a make and its break code) with no service pass between
them, services the first normally with an EOI, then checks the second. With
edge-detection, `pic_master.has_interrupt()` is false and `poll_interrupt()`
returns -1: the byte is sitting in the controller, unread, and the PIC was
never told. The test fails against the edge-detecting code, the same
discipline §14 and §15 hold their own findings to.

### 17.3 The service pass

`Chipset::service()` level-checks IRQ1 exactly like IRQ12, right next to
it:

```cpp
if (kbc.irq1_pending()) { pic_master.raise(1); kbc.clear_irq1(); }
if (kbc.irq12_pending()) { pic_slave.raise(4); kbc.clear_irq12(); }
```

`raise()` only sets an IRR bit (`pic8259.cpp`), so calling it every pass
while the line is already asserted is idempotent, the same property that
makes the IRQ12 pattern safe. There is no `kbc_irq_prev_`; `clear_irq1()`
(`i8042.h`) has one caller, matching `clear_irq12()`.

### 17.4 Tests

`make check`, `pm-check` 24/24, `vbe-check` all-pass. `make boom-check` (while
it existed) was green on all three phases with the same figures with or
without this change, since nothing about what byte reaches the guest or when
it is *queued* moves, only whether the PIC is told a byte that was already
there is waiting. BOOM was never the reproduction: it needs two keyboard
bytes with no service pass between them and a purely interrupt-driven read,
which is what §17.2's test forces and what a fast typist or an extended key
does to the real front end.

## 18. The CD-ROM drive is not on the critical path

A page load does not download a CD image before the machine powers on. The
CD-ROM bay starts empty. V86 mode (§6.1) gates a genuine FreeDOS reinstall,
but this machine ships with FreeDOS *already installed* on C: (§5), and
nothing about booting or running touches drive D: at all.

### 18.1 What the startup fetch is, and why it excludes the CD

`app.js`'s startup `Promise.all()` fetches `disks/freedos-hdd.img` (the
factory C: image, ~122MB gzip-compressed; `serve_nocache.py`/§15 cover that
path) and no CD. FreeDOS's own official install/live CD is ~420MB and, per
`web/Makefile`'s own comment, a densely-packed ISO9660 image that gzip
barely touches, so fetching it before the power switch enables would make
every visitor pay for it. CLAUDE.md's "Adding a new machine" convention of
shipped pre-loaded media fits the *fixed* disk. The CD-ROM is removable
media, and every other machine here (the floppy bay included) treats
removable media as something a visitor supplies or requests, not something
fetched on their behalf.

`hdd_boot_check` (§5.10) cold-boots this machine with **nothing but the
finished HDD image mounted**, no floppy, no CD, and reaches an idle `C:\>`
prompt. The CD-ROM path is real and tested (`atapi_cdrom.cpp`, §3) without
the CD being *in the drive by default*.

### 18.2 The empty bay

The CD-ROM bay starts empty, like the floppy bay. `app.js`'s firmware fetch
excludes `freedos-cd.iso`; `powerOn()` only calls `mountCdrom()` when
`pendingCdrom` is set. A "Load FreeDOS CD..." button next to the "Insert..."
file picker fetches the shipped `.iso` on demand, one `fetch()` call wired
through the same `pendingCdrom` path a user-supplied file uses, so swapping
discs, ejecting, and power-state independence (§7) all work identically once
a disc, any disc, is in the drive. §55 covers the removal of the
shipped CD and that button. On the build side `cdrom-image` stages the file;
only *when* a browser asks for it differs.

### 18.3 Tests

`web/tests/cdrom.spec.ts`: "boots with the drive empty, and never fetches the
FreeDOS CD unasked" asserts `cdromPresent()` is false *and* that no request
for `freedos-cd.iso` was made during boot, not just that the UI looks empty.
§55 removes the on-demand "Load FreeDOS CD..." case along with the button.
The eject/insert-arbitrary-ISO and
swap-while-powered-off cases start from the empty-by-default bay.

## 19. Virtual-8086 mode, and FreeDOS's own default boot menu

FreeDOS's installer configures its boot menu to load JEMMEX, a V86-mode
memory manager (§5.9). V86 mode is implemented in `cpu80486.h`/`.cpp`, so
`make hdd-boot-check` boots FreeDOS's real, unmodified installer output,
JEMMEX and all, to a genuine idle `C:\>`. §6.1 lists the one V86 form that
stays out (task-gate entry).

### 19.1 What V86 mode requires

EFLAGS.VM has storage (§6.1); entering the mode takes:

- **`cpl()` and segmentation stay correct under VM.** V86 is a submode of
  protected mode (VM implies PE=1), so every place that branched on bare
  `protected_mode()` to decide "real-mode-style addressing", `seg_linear()`,
  `load_seg()`, `wraps_at_64k()`, `far_transfer()`, `far_return()`, has
  a `v86_mode()`/`real_addressing()` check alongside it, while paging
  (`translate()`, gated on `CR0.PG` alone) stays untouched, V86 is exactly
  "8086-style segmentation, over paged memory." `load_seg_real()`'s CS load
  sets `cpl_ = 3` under VM instead of the unreal-mode `cpl_ = 0` (Intel 80386
  PRM: "CPL is always three in V86 mode").
- **Leaving V86 on an interrupt.** `protected_mode_interrupt()` has a
  `from_v86` branch: the gate must be a 32-bit, DPL-0, non-conforming gate
  (else #GP), VM is cleared in the *live* EFLAGS before the push (the pushed
  copy keeps VM=1 so IRETD can restore it), and the extended frame, GS FS
  DS ES, then SS ESP, then EFLAGS CS EIP, goes onto the TSS's ring-0 stack
  with all four data segments unconditionally nulled after (Intel 80386 PRM,
  "Entering and Leaving Virtual 8086 Mode").
- **Entering V86 on IRETD.** `far_return()` has a structurally separate
  early-exit branch, checked before the ordinary outward/inward-return code
  (which assumes `new_cs` is a real selector, an 8086 segment value is
  not): pop ESP, SS, ES, DS, FS, GS mirroring the exit frame, force each
  segment's limit to 0xFFFF/D-B to 0/DPL to 3 (since `load_seg_real()`
  deliberately *preserves* the cached limit and D-B bit for unreal mode's
  sake, exactly what a V86 task must not inherit from the monitor's flat
  32-bit segments), and set `cpl_ = 3`.
- **The four IOPL-sensitive V86 traps.** CLI/STI fault correctly once
  `cpl()` reads 3 in V86. PUSHF, POPF, IRET and INT n each carry an explicit
  `if (v86_mode() && iopl() != 3) raise_err(EXC_GP, 0)` (Intel 80386 PRM, "Additional Sensitive Instructions" /
  "Emulating 8086 Operating System Calls"), these four, and only these
  four, trap to the monitor so it can virtualize IF and intercept synthetic
  OS calls.
- **V86 ignores IOPL for I/O entirely.** `check_io_permission()`'s
  `cpl() <= iopl()` shortcut has `!v86_mode() &&` in front of it:
  "the protection mechanism does not consult IOPL when executing the I/O
  instructions... only the I/O permission bit map controls the right for
  V86 tasks" (Intel 80386 PRM, "Virtual I/O"). Without the guard, IOPL 3
  would let a V86 task's I/O bypass the TSS bitmap entirely, which is
  backwards.
- **Task-gate V86 entry is a declared gap, not silent corruption.**
  `task_switch()`'s CS load is a bespoke inline descriptor lookup, entirely
  separate from `load_seg()`/`load_seg_real()`, so none of the above reaches
  it. A VM=1 TSS would misread an 8086 segment value as a selector and
  derive CPL from its low bits. Since every period V86 monitor (JEMMEX
  included) enters V86 via IRETD inside one task, task-gate V86 entry is
  Windows 3.x Enhanced Mode's mechanism, out of scope here, `task_switch()`
  reports such a TSS as invalid (`#TS`) rather than corrupting state.
  This also makes the base-386/486 interrupt-redirection bitmap
  unreachable, for the same reason.

`Cpu80486V86Test` covers all of the above, including a
synthetic round-trip (`AMonitorEmulatesThreeTrappedInstructionsAndTheTaskRunsOn`):
a ring-0 harness IRETDs into V86 code that executes CLI, an `IN` denied by
the TSS bitmap, and a software `INT`, with the ring-0 handler emulating each
and IRETDing back, three traps, three emulations, the 8086 program runs to
completion.

### 19.2 POP r/m with an ESP base, and POPF/POPFD under VM

JEMMEX's boot runs paths that FreeDOS's real-mode boot and §6's
protected-mode tests never reach. Two instruction behaviors matter.

**`POP r/m` resolves its memory destination against the post-pop ESP.**
`case 0x8F` pops first (`pop32()`/`pop16()`), then calls `decode_modrm()`,
which reads live register values, including ESP, to compute a SIB-relative
address. Intel's documented quirk: "If the ESP register is used as a base
register for addressing a destination operand in memory, the POP instruction
increments the ESP register before data is written into the destination
operand" (Intel SDM, POP). So `POP [ESP+4]` resolves against the *post*-pop
ESP, not the value ESP held when the instruction started. `decode_modrm()`
only ever consumes bytes from EIP, so running it after the pop changes
nothing about instruction-stream parsing, only which ESP value the address
computation sees.

JEMMEX's own early V86-capability self-test uses a "CALL a two-instruction
thunk that pops-and-relays a stack slot via `POP [ESP+n]`" idiom, a stack
manipulation trick in period DOS-era assembly. Resolved against the wrong
ESP, the thunk's *first* iteration can still work (leftover data on the
stack coincidentally matches), but the second writes its relayed value 4
bytes short of where the following `RET` expects it, so that `RET` pops
stale data and jumps into what is actually a page-table page, executing its
raw PTE bytes as code until the first wild `[EAX]` dereference faults.
`Cpu80486Test.PopEspRelativeMemoryResolvesAgainstThePostIncrementEsp` pins
it directly: `POP DWORD [ESP+4]` with known markers at the pre- and
post-increment addresses, asserting the popped value lands at the *far* one
and the near one is untouched.

**`POPF`/`POPFD` preserve VM.** Both mask which EFLAGS bits a POPF/POPFD can
change (`keep`), and `keep` includes `FLAG_VM`, so `eflags = (popped & mask &
~keep) | (eflags & keep)` leaves VM alone. §19.1's IOPL-sensitive fault only
covers a POPF/POPFD executed at IOPL below 3; at IOPL 3, a V86 task's
perfectly ordinary POPF/POPFD (restoring flags it saved with PUSHF/PUSHFD
moments earlier) runs unfaulted, and with `keep = 0` it would silently drop
the task out of V86 mid-instruction-stream, with every cached CPL/segment
assumption built on VM staying 1 now wrong. Intel 80486 PRM, "POPF/POPFD":
"the VM and RF flags... are not affected by the POPF/POPFD instructions";
only IRETD from CPL 0, or a task switch, may change VM. The code is
`uint32_t keep = FLAG_VM;`.
`Cpu80486V86Test.PopfdAtIoplThreeCannotDropOutOfV86` is the 32-bit sibling
of the 16-bit-POPF IOPL/VM test, needed separately because 16-bit POPF
always preserves EFLAGS' upper 16 bits unconditionally (so it cannot
disturb bit 17 regardless of `keep`), while 32-bit POPFD builds its whole
result from `mask`/`keep` and so needs VM listed explicitly.

JEMMEX's boot depends on that second behavior. It loads its kernel, prints
its own banner, and then FreeCom itself starts loading, with a guest
`POPFD` executed somewhere in that startup path. With VM dropped, the next
segment load goes through the protected-mode `load_seg()` validation path
instead of `load_seg_real()`, tries to validate an 8086 segment value as a
GDT selector, and raises #GP, which cascades into an unrecoverable loop,
since JEMMEX's own exception path never re-sets VM either (nothing should
have to).

`make check` covers both behaviors and §19.1's V86 cases.

### 19.3 The result: FreeDOS's real default boots

With both behaviors in place and no boot-menu patch, `make hdd-image`
installs FreeDOS as in §5, and `make hdd-boot-check` reaches:

```
OK: reached an idle C:\> prompt at cycle 1530335551
=== screen ===

Modules using memory below 1 MB:

  Name           Total           Conventional       Upper Memory
  --------  ----------------   ----------------   ----------------
  SYSTEM      17,056   (17K)     10,752   (11K)      6,304    (6K)
  COMMAND      3,376    (3K)          0    (0K)      3,376    (3K)
  FDAPM          928    (1K)          0    (0K)        928    (1K)
  CTMOUSE      3,104    (3K)          0    (0K)      3,104    (3K)
  UDVD2        1,984    (2K)          0    (0K)      1,984    (2K)
  SHSUCDX      6,160    (6K)          0    (0K)      6,160    (6K)
  Free       751,856  (734K)    643,136  (628K)    108,720  (106K)
```

JEMMEX loaded and genuinely provisioned 106K of upper memory, into which
COMMAND.COM, FDAPM, CTMOUSE, UDVD2 and SHSUCDX all actually load (contrast
§5.10's Safe-Mode figure: HIMEMX only, no UMBs, everything in conventional
memory). This is the real, unmodified FreeDOS 1.3 default: the boot menu
matches what the installer itself wrote, with JEMMEX highlighted, so no
labelled departure is needed. §27 describes the plain config the shipped
image carries.

### 19.4 Open: BOOM under JEMMEX

BOOM (§9) does not reach the §9.6 bar through JEMMEX. Cold-booting through
JEMMEX and running BOOM off the same HDD image crashes:

```
General Protection Fault at eip=b4; flags=3206
eax=07ca010f ebx=0013000a ecx=00000000 edx=000000b7 esi=00005178 edi=00122200
ebp=0000091e esp=0000075c cs=a7 ds=af es=af fs=8f gs=0 ss=8f error=0000
```

That is a `#GP` inside CWSDPMI (BOOM's DOS-extender host), at cycle ~1.71B,
109.5M cycles into BOOM's own run. It is **specific to JEMMEX, not a
general CPU-core limit**: booting the same, already-built HDD image with
`FDCONFIG.SYS` patched to FreeDOS's Safe-Mode entry (HIMEMX only, no V86
monitor active) runs the identical `BOOM.EXE`/`CWSDPMI.EXE` binaries past
3.4 billion cycles, with full boot, render, mouse input and Sound Blaster
audio all working, more than double the cycle count where the JEMMEX boot
dies. Both of JEMMEX's own menu entries, the default ("more compatible"
EMS/UMB config) and the alternate ("no EMS, max RAM free" variant), fail at
essentially the same cycle count with the identical `cr2=10005178`/`eip=b4`
signature, which rules out "not enough free memory": the variable is
whether JEMMEX is loaded at all, not how much headroom it leaves.

Instruction-level, register-level tracing, cross-checked against `ndisasm`
and CWSDPMI's real upstream source, rules out every CPU-instruction-level
explanation checked: the task switch into CWSDPMI's TSS, the TSS base
address, the restored `ESI` value, the ModRM/SIB decode of every
instruction from entry to the fault, the page-fault restart immediately
preceding it, and the far-pointer table the crash path is reached through
are all correct against Intel's documented semantics and CWSDPMI's own
source. `tlb_flush()`/`tlb_invalidate()`/`prefetch_revalidate()` are also
correct: CR3 writes, CR0.PG/WP toggles, INVLPG, and task switches all bump
the TLB generation counter that `prefetch_revalidate()` checks, and V86
entry/exit touches none of CR3/CR0.PG/`tlb_gen_` at all (architecturally
correct, since a V86-to-protected-mode transition never changes address
spaces).

The instruction bytes at the fault site (`eip=b4`) are BOOM's own *static*
loaded code, not data, which only makes sense if that physical page's frame
was overwritten after BOOM's code was loaded into it. JEMMEX is the one
component in this picture that actively remaps physical page frames at
runtime while running as ordinary V86-trapped guest code (EMS page-frame
mapping, UMB carving), entirely in software, with no special chipset
support, the same as anything else that runs in V86. CWSDPMI runs its own,
independently-`CR3`'d allocator over the same finite pool of extended memory
at the same time. The leading hypothesis is a **physical-frame collision
between JEMMEX's EMS/UMB allocator and CWSDPMI's own allocator**, each side
believing it owns a given frame, which would produce exactly this symptom
regardless of how correctly any single instruction executes, because it
would live in the negotiation between two pieces of guest software over a
shared resource, not in the CPU core. (`FDCONFIG.SYS`'s JEMMEX lines carry
`NOVME NOINVLPG`, which points at a known rough edge in this area.) The
hypothesis is unconfirmed and the item is open. Tracing JEMM32's own EMS/UMB
allocation routines against CWSDPMI's `paging.c` (`valloc()`/`getpte()`),
both available from their real upstream sources, is the way to settle it.

### 19.5 The shipped image is FreeDOS's Base package set; `boom-check` is retired

The machine ships a barebones DOS environment meant as a starting point for
installing your own OS (MS-DOS 6.22 or otherwise) onto the blank-HDD option
`hddBlankBtn` provides (`web/app.js`/`web/index.html`, the same
mount-an-all-zero-image pattern as `ibmpc-at`'s own "Mount blank drive...").
It has no games, no applications, no dev tools, and no network stack.

FreeDOS 1.3's installer offers exactly this as a stock option, at the
`FDASK700` "packages do you want to install" screen §5.8 documents. Traced
with `PC486_TRACE=1`, the screen lists, top to bottom: "Plain DOS system",
"Plain DOS system, with sources", "Full installation including applications
and games", "Full installation with sources", and the installer's own
default highlight sits on "Full installation..." (a bare `@ENTER`, as in
§5.2, produces the 205-package Full image). `disks/build_freedos_hdd.cpp`
answers that one step with `@UP @UP @ENTER`, two Ups off "Full" onto "Plain
DOS system", and leaves every other stage300-800 answer alone.

FreeDOS's own Base package group is documented at 65 packages against
Full's 205 (the [FreeDOS 1.3 report](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/report.html));
games, applications, editors, development tools, networking, sound, and
GUI-desktop categories are Full-only. In the shipped image,
`C:\FREEDOS\PACKAGES` holds 67 package manifests (Base plus a couple of
installer-pulled dependencies such as `CWSDPMI.LST`, not present in the bare
"65" headline count), and `C:\GAMES` does not exist, where the Full image has
`C:\GAMES\BOOM` (§9).

The same `build_freedos_hdd` tool, against the real, unmodified installer,
completes in 85,747,404,698 cycles to the installer's own completion screen,
against §5.8's 317.5 billion for the Full set (the same
`total_cycles()`-at-write measurement in both cases). Fewer than a third of
the packages take well under a third of the cycles, since per-package
copying dominates the run and the fixed boot/partition/format/reboot
overhead §5.8 measures (~22.4 billion cycles) is the same.

`make hdd-boot-check` against the Base image reaches an idle `C:\>`
(cycle 1,448,204,026) through JEMMEX, with the identical UMB provisioning
§19.3 measures: COMMAND.COM, FDAPM, CTMOUSE, UDVD2, and SHSUCDX all load
high, 106K free upper memory. JEMMEX's presence is driven by
`fdins900.bat`'s CPU-class detection into `CONFIG.DEF` (§5.9), not by
package selection, so switching to Base does not avoid JEMMEX by default.
Safe Mode at the boot menu remains the only JEMMEX-free path, as in §5.10.

BOOM does not ship, so `make boom-check` (§9-§13's proof that this core runs
genuine third-party protected-mode software) is not a Makefile target:
there is no `C:\GAMES\BOOM` on the shipped image for it to run.
`disks/boom_run_check.cpp` is deleted, and git history has it for anyone who
builds a Full-package image and wants to re-run that proof.
`web/tests/zzbench.spec.ts`, a Playwright benchmark that also launched BOOM
and was self-labeled scratch code ("not part of the suite"), is deleted
rather than retargeted, since it was never shipped test coverage.

## 20. Installing a real copy of MS-DOS 6.22

FreeDOS ships as this machine's out-of-the-box OS, but a visitor can also
blank the HDD (`hddBlankBtn`) and install their own DOS onto it. Running a
real, commercially-shipped install end to end, Microsoft's genuine MS-DOS
6.22 "Plus Enhanced Tools" retail media (kept local and never committed;
this repo's media policy has real MS-DOS stay BYO, as `web/index.html`
documents), is what exercises two behaviors of the keyboard controller and
the A20 gate. The same method as §5.4 and §16.6: run the real software and
read what it does.

### 20.1 Keyboard-device responses raise IRQ1

Booting Disk 1 and running SETUP.EXE exercises the keyboard on its own
terms. `i8042.cpp`'s `push_kbd()` delivers every byte the *keyboard device
itself* sends back, scan codes and command responses (ACK, reset's BAT byte,
Read-ID's device ID) alike. Real hardware doesn't distinguish: "If no errors
occur, the response byte is placed in the input buffer, the IBF flag is set,
and IRQ1 is activated, signaling the keyboard driver" (Chapweske, "The
AT-PS/2 Keyboard Interface," "Writing to keyboard"), true of an ACK exactly
as much as a scan code. SETUP.EXE's own keyboard-presence probe sends `0xF2`
(Read ID) and waits on IRQ1, not on polling, to learn the response arrived.
The `0xF2` response is the genuine two-byte ID `0xAB, 0x83` (Chapweske,
command `0xF2`: "the keyboard responds by sending a two-byte device ID of
0xAB, 0x83").

A response that raises no interrupt piles up, unread, behind the 8042's
single-byte output register (§16.6's comment on that register), wedging it
full and silently dropping every keystroke typed afterward, SETUP.EXE
included. `irq` is therefore a required (not defaulted) parameter of
`push_kbd()`, forcing every call site to say explicitly whether it raises
IRQ1, and every keyboard-*device* response site says yes. Two tests
(`ReadIdRespondsWithAckThenTwoIdBytes`,
`KeyboardCommandAckRaisesIrq1WhenEnabled`) cover both halves. The account in
§20.2 assumes a keyboard that works throughout the whole install.

### 20.2 Port 0x92 ("Fast A20 Gate"), and DOS writing into the HMA

With the keyboard right, SETUP.EXE runs the real install to completion (disk
swaps, partition, format, file copy, all of it) and produces a genuine,
bootable `C:\`. The whole install was automated the same way
`disks/build_freedos_hdd.cpp` automates FreeDOS's (a scratch harness, not
committed, mirroring that file's screen-text-match / keystroke-inject /
floppy-swap technique against Disk 1-3). Cold-booting the resulting image
without port 0x92 hangs forever at `HIMEM is testing extended memory...`,
not slow but genuinely stuck: a 60-billion-cycle run (roughly 15 minutes at
66MHz on real hardware) makes no further progress.

The CPU spins in a tight loop between two addresses inside HIMEM.SYS's own
loaded segment, `RET` repeatedly popping a stale `0x0000` off the stack. The
segment's own first bytes should be its real device-driver header
(extracting the file straight off the finished image shows it starts
`FF FF FF FF 00 A0 4C 00 3F 0D 58 4D 53 58...`, a completely ordinary
"next=none, strategy@004C, interrupt@0D3F, name=XMSXXXX0" header), and
instead read as one repeated byte, `0xE6`, for tens of kilobytes. CS has
wandered into a swath of memory that has been overwritten wholesale.

Instruction-level tracing (`on_instruction`, the same hook §14.4/§16.6 use)
catches the write: a plain `STOSB` inside MS-DOS's own kernel, `AL=0xE6`,
executed in a loop with `ES=0000` and a 32-bit `EDI` climbing past
`0x00100000`. This is DOS deliberately writing into the HMA, just above the
1MB line, almost certainly as part of loading its own resident kernel "high"
(`DOS=HIGH`). The byte lands at physical `0x100472` and, watched live, the
*same* byte simultaneously appears at `0x000472`, the low-memory alias
exactly 1MB below it. That is the textbook A20-disabled wraparound
(`chipset.cpp`: `if (!kbc.a20_enabled()) addr &= 0xFFFFF;`), confirmed
directly: `kbc.a20_enabled()` reads false for the *entire* run, from cold
boot straight through the corruption and beyond. DOS believes A20 is on and
writes a long run of bytes starting just above 1MB; every one of them
aliases back down into conventional memory, sweeping upward through low
memory as the HMA write advances and eventually overwriting HIMEM.SYS's own
just-loaded code with the fill byte. The CPU then falls out of that wrecked
region into an unrelated, coincidentally-valid subroutine elsewhere in the
image, whose `RET` pops whatever zero happens to be sitting on the stack,
landing back at the start of the corrupted region: a self-sustaining, silent
infinite loop with no fault and no I/O, which is why it presents as a hang
rather than a crash.

The mechanism is **port 0x92, "Fast A20 Gate" / "System Control Port A"**.
Instrumented tracing shows the guest never once issues the
keyboard-controller A20 sequence (`0xD1` then a data byte) during this
entire boot; it relies exclusively on port 0x92. Without the port decoded,
the access hits the "unmapped port" default (`chipset.cpp`: reads return
`0xFF`, writes "vanish, open bus"). This is real, widely-documented period
hardware, not an obscure corner: essentially every 386+ chipset carries it
because toggling A20 through the keyboard controller's command protocol is
slow, and Microsoft's own HIMEM.SYS tries it first for exactly that reason
(OSDev Wiki, "A20 Line"; the AMD64 Architecture Programmer's Manual's
chipset chapter still guarantees port 0x92 bit 1 for backward compatibility
on modern hardware). FreeDOS's HIMEMX goes straight to the keyboard-controller
method, so it never exercises this path.

`i8042.h`/`i8042.cpp`: `owns_fast_a20()`/`fast_a20_in()`/`fast_a20_out()`
read and write the *same* `output_port_` bit 1 the keyboard-controller path
uses, not a second, independent latch, matching real chipsets wiring both to
one physical A20 line, with bit 0 triggering the documented fast-reset
behavior exactly like output-port bit 0 does. `chipset.cpp`'s
`io_in`/`io_out` route port `0x92` through these before falling to the
unmapped-port default, calling `note_a20()` on writes so the page-mapping
cache invalidates correctly, identically to the `0xD1` path. Three tests
(`FastA20PortEnablesA20`, `FastA20PortAndOutputPortShareTheSameA20State`,
`FastA20PortBitZeroTriggersReset`) cover the port directly.

End to end, cold-booting the MS-DOS 6.22 image reaches
`HIMEM is testing extended memory...done.` and a genuine, idle `C:\>`
(SMARTDRV loaded, prompt responsive) in 1.12 billion cycles. `make
hdd-boot-check` against the shipped FreeDOS image reaches its own idle
`C:\>` at the same cycle count as §19.5 (1,448,203,634 against 1,448,204,026,
within run-to-run noise), confirming FreeDOS's own boot path is unaffected.

## 21. Real Doom 1.2: unchained mode 13h, and incremental autosave

With MS-DOS 6.22 installed (§20) and playable, the next real-world test is a
real commercial DOS game: id Software's registered DOOM v1.2 (the user's own
retail floppies, kept local per this repo's BYO-media policy). The genuine
`INSTALL.EXE`/`SETUP.EXE` run end to end the same way §20 drives MS-DOS's,
and the title-screen demo runs after it. That exercises two things: how the
VGA renders an unchained mode 13h, and what a periodic HDD save costs the
audio.

### 21.1 `RenderVga256Screen` handles chain-4 off

The scratch-harness technique is §20's: real `INSTALL.EXE`, real
`SETUP.EXE` menu navigation (including its own Port/IRQ/DMA/digital-channel
submenus, all confirming this machine's fixed SB16 wiring of 220/IRQ5/DMA1),
then the real `DOOM.EXE`. A renderer that assumes chain-4 is always on
produces a tiled, banded field of color noise instead of the DOOM title
art, frame for frame the same in the native harness as in the browser.

DOOM's engine doesn't just set mode 13h and blit. It also disables Sequencer
Memory Mode's Chain-4 bit (SR04 bit 3) while leaving 256-color shift-out
selected, the well-known "unchained mode 13h" trick: with chain-4 off, four
adjacent displayed pixels live at the *same* per-plane offset, one byte per
plane, selected for writing through Map Mask (SR02), which is what lets a
column-based software renderer and up to four 64KB-aligned page-flip buffers
coexist in the card's 256KB of VRAM. Real 1993 DOS-VGA programming, not a
DOOM defect.

A flat walk of `vram[]` with `crtc_row_byte_stride()`/`start_byte_offset()`
is right only for chain-4: "Chain-4 makes the flat frame-buffer offset and
the planar VRAM index the same number". Once chain-4 is off those two
accessors stop meaning what a flat walk assumes. DOOM's own CRTC
reprogramming (clearing Underline Location's Doubleword bit, setting Mode
Control's Byte Mode bit) makes `crtc_row_byte_stride()` report 80 instead of
320 and `start_byte_offset()` report the page-flip buffers at 16384/32768
instead of their real 65536/131072-byte starts, both exactly 4x short,
because those two accessors' unit-of-measure scaling
(`crtc_address_unit_bytes()`) is only correct for the flat chain-4 case. The
real hardware's shift register round-robins all four planes for *display*
regardless of how the CPU addresses them for *writes*.

`Ega::chain4_enabled()` (a thin wrapper over the Sequencer Chain-4 bit
`mem_read`/`mem_write` consult internally) gives `RenderVga256Screen` a
chain4-aware branch: when chain-4 is off, it walks `plane_off`/`plane`
directly, `(plane_off << 2) + (x & 3)` with `plane_off` advancing by
`crtc_scanline_stride()` (the *unscaled* Offset-register figure, which is
exactly the real per-plane-group stride regardless of the byte/word/dword
bits DOOM's own reprogramming touches), the identical addressing
`mem_write()` uses for CPU-side unchained writes, applied to the display
side. The flat chain-4 path is the other branch and has its own
doubleword-address-unit test
(`Vga256ScanLineStrideUsesTheDoublewordAddressUnit`, which never touches
Sequencer Chain-4, so it stays on the flat path).

Two tests cover the unchained branch:
`Vga256UnchainedWalksPlaneOffDirectlyNotAFlatOffset` (writes one byte to each
of the four planes at the same `plane_off` through Map Mask, exactly like a
real unchained column blit, and checks all four resulting pixels decode
through the live DAC correctly) and
`Vga256UnchainedStartAddressSelectsA64KAlignedBuffer` (programs Start Address
to select the second of DOOM's four 64KB-aligned page-flip buffers and
confirms the renderer reads from the right one). `make vbe-check` confirms
the chain-4 path is unaffected. The native DOOM harness shows the actual
title screen, HUD, weapon, and monster sprites rendering correctly through
real gameplay.

### 21.2 Incremental autosave keeps the audio steady

DOOM's *audio* is a second place the host shows through. Capturing the Sound
Blaster's raw output natively across a full minute of real gameplay
(cycle-stamped samples drained every tick, rendered to a WAV via the same
sample-and-hold resampling `web/app.js`'s `pumpSbAudio()` uses, then scanned
for literal byte-identical repeated windows) finds zero genuine repeats in
the device's own output, so the DMA/DSP emulation (§11-§13) is not a source
of repeats. A periodic glitch in the browser has one cause: a full-disk copy
off the wasm heap, which is 180-350ms at the 504MB geometry (§8.6), firing on
a plain 5s `setInterval` whenever C: has been written. That is comfortably
longer than the Sound Blaster worklet's 8192-sample (~171ms) ring buffer, so
every firing underruns it into a held-last-sample glitch.

The save tracks *which* 4KB pages of C: a session wrote, not a whole-drive
boolean: `Wd1003::Drive` has a `dirty_page` bitmap (sized at `mount()`,
marked in `finish_read_or_write()`'s WRITE SECTORS path, cleared by
`clear_dirty()` alongside the dirty flag), and `dirty_ranges()` coalesces
contiguous dirty pages into byte ranges. `wasm_machine.cpp` exposes these as
`hddDirtyPatches()`, `[{offset, bytes}, ...]` for only the changed bytes.
`persistHddIfDirty()` `.set()`s each patch into its in-memory mirror
(`savedHdd`) instead of calling `hddImage()`'s full copy, and falls back to
the full copy only when there is no same-size mirror yet to patch onto (the
very first save after a fresh boot). A real DOS session's writes are a
handful of FAT/directory/file pages, not the whole disk, so the common-case
stall is whatever copying a few 4KB pages costs, well under the ring
buffer's headroom.

Two `Wd1003Test` cases cover the tracking:
`WriteSectorsAcceptsDataImmediatelyThenCommitsSynchronously` also checks the
single sector it writes produces exactly one 4KB dirty range at the right
offset, and `DirtyRangesTracksOnlyWhatWasActuallyWrittenAndClearsCleanly`
writes two sectors far enough apart to land in non-adjacent pages and
confirms they report as two separate ranges (not one merged across the
untouched gap), and that `clear_dirty()` resets both the flag and the
bitmap. The `hdd`/`soundblaster`/`speaker` Playwright specs, including "C:
persists across a page reload via IndexedDB", exercise the patch-then-save
path.

## 22. FM music, and the Sound Blaster paths it exercises

§11 covers the SB16's digitized path and the OPL3 supplies FM. Together they
bring four behaviors that real drivers depend on, each with a regression
test.

**The DSP block length counts DMA cycles, not audio frames.** The Bxh/Cxh
length is bytes on the 8-bit channel and words on the 16-bit one. Reading it
as frames makes every 8-bit *stereo* block twice too long, so a
double-buffering driver refills half a block behind the play position and
the stale half plays again: DOOM 1.2 would sound every effect twice. Mono is
arithmetically unchanged, which is why the legacy 14h/1Ch paths never show
it. SBPG gives the 16-bit length in words, which is only coherent if the
counter sits on the DMA side of the FIFO, and that same counter then serves
stereo.

**0x388/0x389 is decoded.** FM ports inside the card's own block are not
enough: an AdLib-era music driver writes FM registers to 0x388 and never
touches 220h at all, so a machine without it detects an OPL and then plays
nothing. That is the "a fake OPL3 that passed detection and then produced
silence would be worse than absent hardware" failure mode (§11), reached
by a different route: detection succeeds, and the writes fall into an
undecoded hole.

**Pitch follows the F-number formula.** The phase increment is
`fnum << block`, where one lap of the 20-bit accumulator is one cycle and
`wave_sample` indexes it as `phase >> 10`. A step of `fnum << (block + 2)`
runs two octaves sharp, exactly 4.00x across four octaves, against
F = fnum * kSampleHz / 2^(20-Block), the formula the classic AdLib note
table is built on (fnum 159h at block 4 is middle C). Two tests pin pitch.

**The catch-up burst keeps its credit.** `Opl3::advance` consumes the
credit for a catch-up burst before capping it. Capping the burst *first*
leaves the shortfall in `frame_credit_`, so a long tick gap makes the chip's
clock fall further behind on every such call: music that starts at the right
speed and slows down indefinitely. Dropping a slice of audio is recoverable;
running the chip slow is not. The test fails when the cap order is
reintroduced.

## 23. The Performance panel

Measuring the machine costs main-thread time. Readings taken with DevTools
attached and a `console.log` per second spend the very budget being
measured: Doom reads about 85% of real speed that way, and 100% with
DevTools closed.

That is the reason the panel reports to the *page* and never to the console,
and the reason it is split in two tiers:

- **Tier 1** is pure host-side timing, achieved clock, dropped cycles, the
  main thread's share of one core, the emulation/draw split, fps, memory,
  and the audio ring's health. It needs no instrumentation in the emulator
  and works against the shipped binary.
- **Tier 2** is the emulator's own counters, instructions, cycles per
  instruction, TLB misses, prefetch-window misses, MMIO accesses, device
  service passes, and a ranked opcode histogram. These sit on the hottest
  paths there are, so they are compiled out entirely unless the build sets
  `PC486_PERF` (`make PERF=1`), which produces a *second* binary,
  `pc486-perf.wasm`. `?perf` loads it; a page without it falls back to the
  shipped one and shows Tier 1 alone. The panel says which build it is on,
  because absent counters otherwise read as genuine zeros.

**Guest CPU usage needed a definition.** DOS has no scheduler and no idle
accounting, and bare DOS does not halt: COMMAND.COM waits for a key by
polling INT 16h flat out. Halt cycles alone therefore report ~100% busy at
an idle prompt, which is true and useless. Idle here means halted *or*
spinning in a recognisable DOS wait loop, INT 16h AH=01h/11h, and INT 2Fh
AX=1680h, crediting the gap *between* consecutive polls, and only when it
is short enough to be a tight loop (under 50,000 cycles) rather than a
program checking the keyboard once a frame while working. It is a labelled
inference about guest software, never a claim about the hardware, which is
why the panel spells out what idle means instead of just printing a number.

**Two measurements that rule out optimisations.** The six read-modify-writes
each ALU primitive does on `eflags` are already collapsed by clang, so
doing it by hand gains nothing. And doubling *all* flag computation cost
only 2.4%, which prices the ceiling on lazy flag evaluation: a 16-point
shortfall against the 18% that would be needed, so the core keeps eager
flags.

## 24. Mode 13h as linear memory

Every VGA memory access went through `mem_read`/`mem_write`'s per-byte
planar decode, because `page_host()` returned nullptr for the whole
aperture. Chain-4 already decodes to `vram[off]` exactly, the
`((off>>2)<<2) + (off&3) == off` identity is *why* mode 13h looks linear to
software, so when every planar stage is pass-through, the CPU's existing
page map can cache a pointer straight into VRAM.

`Ega::linear_page()` is deliberately conservative, refusing unless chain-4
is on, write mode is 0, Map Mask is 0Fh, the bit mask is FFh, the rotate
count is 0 and Enable Set/Reset is clear; reads additionally refuse read
mode 1. A page must also sit wholly inside the active window. Because the
page map caches the answer, `mapping_epoch()` moves whenever a register that
could change it is written, and the chipset turns that into a flush after
every port write, built from a signature over only those registers, so the
palette writes of a fade cost nothing.

Its benefit in DOOM is **unconfirmed**: with DevTools closed (§23) the
machine is already at 100% with `mmio` unchanged, which means the fast path
is not engaging for DOOM's writes. It stays because it is correct and
guarded, not because it was shown to pay.

## 25. Front-end: a driver disk, key mapper, and no CRT overlay

**The Drivers panel** closes §10.6's gap by handing the machine a CuteMouse
diskette. The files are extracted from the FreeDOS image this machine
already builds rather than downloaded: CuteMouse's own SourceForge URL
serves an HTML interstitial to anything without a browser session and the
FreeDOS ibiblio paths for it 404, so a pinned URL would be a build that
breaks elsewhere. The bytes inherit the FreeDOS zip's published SHA-256, and
`build-ctmouse-floppy.sh` re-checks CTMOUSE.EXE against the official 2.1b4
release hash so a future image carrying a different build fails loudly.

**Esc** joins the Function & extended keys panel (collapsible; open/closed
state is remembered): a user's own Esc releases pointer lock and leaves
fullscreen rather than reaching DOS, and the bezel's copy is easy to miss.
The **Key Mapper** panel sits below it (also collapsible, remembered): a
table of physical-key → guest-key rows with per-row enable/disable and
delete, plus presets for **WASD → Arrows** (A/D strafe via Alt+arrow, the
modern habit for games that still read the arrow cluster) and **Left Shift
→ Ctrl** (so macOS Mission Control / Spaces is not stolen by physical
Ctrl+Arrow mid-game, remapping physical Ctrl itself does not stop the OS
shortcut). Mappings persist in `localStorage` (`retro8080.pc486.keymap`).
Panel open/closed, Enable Sound, and Enable Mouse share a UI-state blob
(`retro8080.pc486.ui`), first visit still defaults sound/mouse off and
panels open. The bezel **KEYS** button enables/disables every mapping at
once (same as the panel's Disable All / Enable All). Translation stays at
the browser edge; the guest still receives genuine Set-1 scancodes.

**There is no CRT overlay.** A `mix-blend-mode: multiply` layer with a
repeating 1px scanline gradient, blended against the canvas on every
composited frame, is ~860x470 px windowed and invisible in the profile;
fullscreen it is several million, and blend modes routinely defeat GPU fast
paths. With it, fullscreen frame rate collapses while `draw` time *falls*
(fewer rAF callbacks doing the same work) and not one emulated cycle is
dropped: the cost is entirely in the browser's compositing, after the canvas
is handed over, where no JS timer can see it. Without it the clock minimum
is 65.7 MHz, against 60.2 with it. The panel shows this because it measures
the host's frame rate and the emulator's dropped cycles separately; either
number alone points the wrong way.

## 26. Front-panel Turbo: DX2 clock doubling, not a whole-board slowdown

A real 80486DX2-66 Turbo button disables the chip's clock doubling: the
internal CPU drops from 66 MHz to the 33 MHz bus rate, while the PIT's
1.193182 MHz crystal (and every other wall-clock device, floppy RPM,
IDE transfer rate, SB/OPL3 sample clocks) keeps running at real time.
Games that wait on IRQ0 still wake on the same wall-clock schedule; they
just get half as many instructions between ticks.

`Machine::set_turbo(false)` sets `cpu_hz_` to 33 MHz and propagates that
rate through `Chipset::set_cpu_hz` into FDC/HDD/CD-ROM/SB/OPL3 so their
seconds-to-cycles conversions stay wall-clock accurate. The PIT already
took `cpu_hz` per `tick()`, so it needed no new API. The browser's frame
pump credits `dt * cpuHz` (66e6 or 33e6) and never above the selected
rate, CLAUDE.md's three override rules: default is Turbo on (66 MHz),
the control is labelled, and it is opt-in.

The seven-segment "66"/"33" readout tracks Turbo (many period cases wired
the display to the same switch that gated clock doubling); only the amber
Turbo LED and the actual clock change with it.

## 27. One plain MS-DOS config replaces the stock FreeDOS boot menu

§19 boots FreeDOS's own installer-written `FDCONFIG.SYS`/`FDAUTO.BAT`
unmodified, five-way menu and all. That menu is not period-accurate to a
1993-94 MS-DOS gaming PC (no real box booted through a FreeDOS-style
numbered menu), and its `MENUDEFAULT` (JEMMEX with `NOEMS`) gives no EMS or
VCPI, which DOS/16M- and DOS/4GW-based extenders need.
`disks/apply-dos-config.sh` overwrites both files after install with a
single, unconditional config: `DOS=HIGH,UMB`, `HIMEMX.EXE` for XMS,
`JEMM386.EXE` (with EMS, not `NOEMS`) for EMS/VCPI, `UDVD2.SYS`/`SHSUCDX.COM`
for the ATAPI CD-ROM (needed to install from CD), and `CTMOUSE.EXE` for the
PS/2 mouse (SimCity 2000 requires it). The CD-ROM and mouse drivers load
high via `LH` so conventional memory stays free for a game's sound drivers.
`BLASTER` keeps the stock `P330`, which the card's own MPU-401 UART
interface answers (see `mpu401.h`), so a game offered a "General MIDI /
MPU-401" option completes its detection handshake instead of timing out.
This is the exact minimum to install and run period games (DOOM,
SimCity 2000), nothing more, and a labelled departure from stock FreeDOS's
own config. The script runs as a post-install step of `make hdd-image`,
right after `build_freedos_hdd` produces the image, so a from-scratch
rebuild gets the same config as the shipped one.

## 28. The factory HDD stash carries a version fingerprint

### 28.1 Why the stash needs an identity

`ensureFactoryHdd()` caches the pristine 504MB `freedos-hdd.img` two ways
so "Reset to factory" and factory-delta reconstruction (`savedHdd`/
`hddSaveKind`) never re-download it: a Cache API entry (`FACTORY_HDD_CACHE`,
the path an ordinary visitor takes) and a chunked IndexedDB stash
(`stashFactoryInIdb()`/`FACTORY_META_KEY`, exercised by the Playwright "no
re-fetch" spec). A stash that did not record *which build* of the image it
held would hand back the old bytes after a deploy replaced `freedos-hdd.img`,
"Reset to factory FreeDOS" would restore the stale image, and the only
recovery would be deleting the `pc486-hdd` IndexedDB database by hand.

The sharper hazard is silent corruption. A normal session's C: is stored as
a *factory-delta* record, offsets into whatever factory image was current
when it was written (see the "Deltas are what a normal FreeDOS session
writes" comment on `isFactoryDeltaRecord()`). Once the factory image
changes, those offsets are patches against a base that no longer exists.
Applying them to the new image via `ensureFactoryHdd()` plus
`applyFactoryPatches()` would write the old session's bytes into the wrong
locations of a different disk image.

### 28.2 The fingerprint check

`factoryFingerprint()` does one `HEAD` request to `factoryHddUrl()` (no
range or full fetch) and reduces the response to a small string: the
`ETag` if the server sends one, else `Last-Modified`, plus `Content-Length`.
`ensureFactoryHdd()` fetches this once per page load (memoized) before
trusting either stash, and compares it against the `fp` field on
`FACTORY_META_KEY`. A stash with no `fp` reads as *unknown*, not a match, so
it fails the comparison exactly like a genuine mismatch. On any mismatch,
`purgeFactoryStash()` deletes the chunked IndexedDB bytes, the legacy
single-blob key, the meta record, and the `FACTORY_HDD_CACHE` Cache API
entry, then execution falls through to a real network fetch and
`recordFactoryFingerprint()` writes a fresh (lightweight, `chunks: 0`) meta
record carrying the new `fp`. That is cheap even though the full chunked
byte-stash normally doesn't run in production (see `stashFactoryInIdb()`'s
own comment: a one-shot 504MB structured-clone put is a main-thread
longtask, so nothing calls it outside the Playwright spec that proves the
no-re-fetch case).

The correctness-critical half is in the boot sequence, not
`ensureFactoryHdd()` itself: when `isFactoryDeltaRecord(savedRecord)` is
true and `ensureFactoryHdd()` reports the stash it just purged was stale
(`factoryStashInvalidated`), the boot flow does not apply that record's
patches to the freshly-fetched image. It calls `clearSavedHdd()` and falls
back to the plain new factory image, with a status label that says why
(`"...the shipped disk image changed, so your saved changes were reset"`)
rather than silently reverting. A *full*-image record (blank drive, upload,
or any non-factory-delta save) is untouched either way: that boot path never
calls `ensureFactoryHdd()`, so it is never at risk.

A `HEAD` failure (offline, CORS, the server down) is treated as *no
information*, not as a mismatch: `ensureFactoryHdd()` falls back to whatever
is already cached and logs the failure the way the rest of this file logs
recoverable storage errors. A stale image that still boots beats a machine
that won't boot over a freshness check.

### 28.3 Tests

`tests/hdd.spec.ts` covers it. "A reload with saved C: does not re-fetch the
factory FreeDOS image" tracks every request whose URL contains
`freedos-hdd.img` and distinguishes `HEAD` (the freshness check, expected on
every boot) from `GET` (an actual re-download, asserted never to happen).
Two more cases: a factory-image fingerprint rewritten in IndexedDB
(simulating a stash from an older build, or one that no longer matches)
forces a real `GET` on the next boot *and* discards a saved factory-delta
record, reverting C: to the new image with the explanatory label,
exercising both halves of §28.2 in one flow; and a full-image save (a small
stand-in record substituted for what `hddBlankBtn`/`hddUploadInput` would
write, since a real one is the same 504MB single-put main-thread stall and
not something a test should wait on) survives the same fingerprint mismatch
with no network request at all, proving the "full records are
self-contained" half.

## 29. The OPL3 exponent ROM is read in reverse, and the master gain

§11.1 records the output master gain as an empirical number with no
hardware citation, 3.0, because three unison voices clipped 16% of their
samples at 8.0 and eighteen at realistic attenuation clipped 44%. The gain
below is 6.0, set against a waveform that is a true sine.

`wave_sample()` decodes the log-sin/exp table pair the OPLx decapsulation
gives: look up log-sin, add the attenuation, exponentiate. The exponent step
reads the ROM in reverse:

    int32_t frac = t & 0xFF;
    int32_t mantissa = int32_t(kWave.exp_tab[255 - frac]) + 1024;
    int32_t mag = mantissa >> (shift + 1);

`exp_tab[x] = round((2^(x/256) - 1) * 1024)` rises with its index, while `t`
is an *attenuation*, a negative exponent: `~t = -t-1`, so the inverted low
byte (`255 - frac`) indexes the table and the shift is `(t >> 8) + 1`, which
lands on kFullScale's unity-at-1024 convention (§35). Indexing forwards
instead makes the magnitude climb toward 2x within each octave of
attenuation and snap back at every 256 boundary, a sawtooth riding on the
sine.

Measured over all 1024 phase steps of waveform 0 at zero attenuation,
against an ideal sine:

| form | peak | worst local error | THD |
|---|---|---|---|
| forwards (the wrong way round) | 2042 | 3.98x at index 597 | **36.0%** (-8.9 dB) |
| `exp_tab[255 - frac] >> (shift + 1)` | 1021 | 1.00x | 0.0% (-73.6 dB) |

Read forwards, every operator of every voice would generate 36% total
harmonic distortion instead of a sine, with the peak at 2042 rather than
1021. That is what an 8.0 gain clips against, and cutting the gain to 3.0
suppresses the clipping while leaving the distortion untouched: FM music
that is harsh, not merely loud.

The same factor reaches the modulators. `phase_offset()` normalises a
modulator's output by `kFullScale` (1024), so an operator running 2x hot
drives **twice the intended modulation index** on every FM patch, and
`feedback_offset()` is inflated the same way; §11.1's standing "modulation
index is pi, unverified" approximation is judged against the true waveform.
The forwards error is also not a uniform 2x, it depends on `frac`, so the
waveform's *peak* does not fall where the sine's does (for the 18-unison
polyphony case the peak sat at log-sin 191, not 0).

The gain is 6.0. Measured on the exact patch
`FullPolyphonyAtModerateLevelsDoesNotClip` uses (18 voices, carrier TL=18,
one pitch, exact unison, the worst case for *coherent* summing), the raw
summed peak is 3852 with the correct waveform (9162 forwards), which would
suggest a gain as high as 8.0. That case is not what binds. Swept across
polyphony at the correct waveform, peak as a percentage of full scale:

| case | gain 4.0 | 5.0 | 6.0 | 7.0 | 8.0 |
|---|---|---|---|---|---|
| 1 voice, TL=0 | 12.5 | 15.6 | 18.7 | 21.8 | 24.9 |
| 6 voices, TL=0 | 57.1 | 71.4 | **85.7** | 100.0 clip | 114.3 clip |
| 9 voices, TL=0 | 57.1 | 71.4 | **85.7** | 100.0 clip | 114.3 clip |
| 9 voices, TL=16 | 15.1 | 18.8 | 22.6 | 26.4 | 30.2 |
| 18 voices, TL=16 | 30.7 | 38.4 | 46.1 | 53.7 | 61.4 |
| 18 voices, TL=18, unison | 47.0 | 58.8 | 70.5 | 82.3 | 94.0 |

A handful of *full-volume* voices clips long before eighteen attenuated
ones do. 6.0 is the highest gain that is clamp-free on every case above,
with 14% margin on the binding one, and it lands one zero-attenuation voice
on the ~18% of full scale §11.1 documents.

Level: on the unison case, RMS is 36.4% at gain 6.0 with the correct
waveform, against 42.0% at gain 3.0 forwards, about 1.3 dB down on the
meter. The 42.0% included the 36% distortion as signal energy, and a gain
of 3.0 with the correct waveform would give 18.2%, so 6.0 holds level while
the distortion is absent. **The correct waveform removes harshness; it does
not make FM music louder.** The perceived "quiet" is a separate question,
and the front end's `kMixerUnityGain` normalisation against whatever the
game programs into the CT1745's MIDI level (34h/35h) is where to look next
(§30).

Both clipping tests pass at 7.0. They bound the *summed* output, and a
per-operator waveform error of this size passes straight through them.
The per-waveform tests of §34 are what pin one operator against the shape
it is supposed to have.

## 30. The SB16's volume wheel

§29 removes the FM distortion but holds output level constant, which leaves
a real complaint: FM music is quiet. Measured on the correct waveform, the
reason no gain constant fixes it is that the OPL3's
own dynamic range between typical and maximal content is about 20 dB:

| content | peak @ gain 6.0 | RMS |
|---|---|---|
| 9 voices, mixed TL (typical music) | 34.7% | -19.5 dBFS |
| 18 voices, mixed TL | 57.7% | -16.5 dBFS |
| 9 voices, all TL=0 (worst case) | 90.6% | -10.8 dBFS |

Raising `kMasterGain` enough to put typical music at a healthy level clips
the loud case outright. That is the real chip's dynamic range, not a
modelling error, and the real card did not resolve it in the digital domain
either.

What a real card did: the YMF262 has no DAC. It emits two 16-bit serial
streams into a companion **YAC512** (the sdiy.info YMF262 page; secondary
sources describe the YAC512's format as a 10-bit mantissa with a 3-bit
exponent, which is not corroborated by a primary datasheet here, see
below). From the DAC it is analog the rest of the way: the CT1745's FM level
(34h/35h), then Master (30h/31h), then the card splits at the **OPSL/OPSR
jumpers**, either bypassing the onboard power amplifier for line-out, or
engaging it for speaker-out, where the output level is set by a **thumbwheel
potentiometer on the card's backplate**. The CT1740 and CT1750 (this
machine's CT1745-mixer, DSP 4.05 generation) both "retained the thumbwheel
volume control on the backplate seen on earlier cards" (DOS Days).

So absolute loudness on a real 486 was set by an analog knob downstream of
everything software could see, and `kMixerUnityGain` in `app.js`, which
models the output amplifier's fixed makeup gain, had that wheel
hardcoded at one position with no way to turn it.

The wheel is modelled as a range control in the audio row, applied as a
`GainNode` after the mix so it cannot clip the emulation, square-law for a
pot's audio taper, 50 = unity (exactly `kMixerUnityGain`'s modelled
amplifier gain) and 100 = +12 dB (`kWheelMaxGain` 4.0).
Default 70 (+5.8 dB). The position persists in `localStorage`, because a
physical wheel stays where it was left, unlike "Enable Sound", which must
never be restored, since a browser blocks audio until a gesture. The
motherboard speaker is a separate part on a real tower and does not pass
through the card, so `speakerNode` is deliberately *not* routed through the
gain node.

The restore path checks for a missing key before coercing it:
`Number(null)` is 0, which would pass a `0 <= n <= 100` range check and turn
the wheel fully down for every first-time visitor. Fresh profile gives 70,
stored 25 gives 25, stored junk gives 70.

**Not modelled, for want of a source.** `wave_sample()` allows an exponent
shift up to 15, while the YAC512's exponent is reportedly 3 bits. The
mantissa/exponent split is the format of the chip's *final output stage*
into the DAC, not of the per-operator path `wave_sample()` implements, and
the guard is dead code either way (the mantissa shifts to zero well before
16). The open question is whether `generate_frame` should quantise its
output to the YAC512's floating-point format rather than clamping linearly
to int16, which would reproduce the real chip's non-uniform quantisation
noise. It stays linear:
the sdiy.info page says "two 16-bit serial data streams" and says nothing
about a mantissa/exponent split, which conflicts with the 10+3 figure, and
no YAC512 datasheet was obtained. Modelling it would *add* a noise floor
around -60 dB, so it is not a change to make on conflicting secondary
sources.

`web/tests/volume.spec.ts` covers the default position and its square-law
taper, persistence across reload, and an out-of-range saved position being
ignored. Not covered yet: that the PC speaker path bypasses the wheel.

## 31. Sound Blaster 16, OPL3 and the audio path: what is still open

The card and its FM chip match hardware on every point there is a source
for, and FM music plays clean on a real session including while a game is
busy. What follows is what does not match, and what is still worth chasing.

Three things to know before reading it:

- **Measure audio at real speed.** The `fast=1` test multiplier runs the
  guest faster than wall time on purpose and decimates the FM stream, which
  is a different regime entirely and says nothing about the shipped page.
  `web/tests/fmquality.spec.ts` boots with `realtime: true` for that reason.
- **The Performance panel is the first instrument**, not the console: it
  carries the ring chart, the pump's cadence (`post`) and whether the machine
  has actually produced the audio yet (`pace`). DevTools attached costs the
  very main-thread budget being measured.
- **`?audiotrace` is the second**, for anything the panel's one-second
  windows are too coarse to catch, `window.__audio.start()`, `.summary()`,
  `.save()`, one row per post.

### Open on the OPL3

- **Timer mask semantics are unverified.** A masked timer still sets its own
  status flag and the mask gates only bit 7 (the IRQ bit). No primary source
  was found either way. Nothing on
  this machine can observe the difference, because the OPL3's interrupt pin
  is not bussed to the PIC, a real SB16 does not wire it either, so this
  is a documentation gap more than a behavioural one.
- **The output stage's master gain, the rhythm section's noise/phase bits,
  the noise LFSR taps, and C0h's power-on value** all remain as §11.1
  describes them. The master gain is the one that matters most, because it
  alone sets the FM-against-digitized balance the front end mixes at.
- **Synthesis test coverage** (§34): KSL/KSR scaling, all four four-operator
  algorithms individually, and waveforms 1-6 have direct assertions
  alongside the modulation index, four-operator F-number sourcing,
  note-select, rhythm section output level, waveform 7 and vibrato LFO
  cases.
- **Envelope, vibrato and waveform 7** (§35): envelope times match the
  YM3812 manual's Table 3-6, but the per-sample stepping pattern, the
  vibrato LFO's shape and quantisation, and waveform 7's slope have no
  primary source.

### The front end's audio path

Audio placement happens on the **audio thread**, keyed to its own sample
clock. The main thread hands the worklet the card's samples exactly as the
emulator stamped them, one guest CPU cycle per sample, plus the CT1745
attenuators to apply, and nothing else. It computes no buffer length, no
mapping and no mix.

The reason is the main thread's timing. It measures elapsed time in ~1 ms windows (`kMinAudioPumpDt`, ~900 posts a
second on a real session) while `runCycles()` blocks it for up to 12 ms at a
stretch: when the guest is busy no post happens during a chunk and several
fire immediately afterwards, so a cycles-per-second figure derived there
swings wildly, and every sample position divided by it swings with it.
Measured on a pure 309.5 Hz OPL3 tone captured as it reaches the device,
placement on the main thread gives 286 Hz and ~18% period jitter with the
main thread busy: scratchy, warbling FM music whenever a game does real
work. The audio clock has no such problem: it advances one sample per
sample. Same tone, same load, placed on the audio thread: **310 Hz and
0.26% jitter**. `web/tests/fmquality.spec.ts` holds the assertion.

Three details earn their keep in `kSbWorkletSrc`:

- **The play position never runs past the newest sample the card has
  produced.** The step rate is only an estimate of guest cycles per real
  second, so pinning there makes the estimate a hint rather than something
  that has to be right.
- **The rate estimate is an aggregate**, cycles divided by elapsed time over
  a window, not an average of per-post ratios, that average sits above the
  true rate whenever the ratios are spread out, which under load they are,
  and the bias was larger than the correction the controller may apply.
- **The correction is asymmetric**: +1% to shed a surplus, -4% when the
  machine has fallen behind and the audio does not exist yet. A 70-cent bend
  that recovers beats a dropout, and either way it is one smooth drift rather
  than a per-post lurch.

Two more details, both measured on a real session: the pump carries the
fractional sample instead of rounding it away (rounding loses ~450
samples/s at ~900 posts/s, which drains the cushion until it starves
continuously: `ring 8 ms of 50`, `starved 2.7 ms/s`), and the ring holds a
real cushion (45 ms with the carry).

The cushion is 80 ms of lead (120 ms before the surplus is dropped), which
is also the longest main-thread stall that passes unheard. At 40 ms a real
session still showed about three dropouts a minute with the clock at 100% and
the pump regular, stalls longer than the cushion, GC-pause and
disk-persist scale. Raising it costs the same again in output latency, which
FM music does not notice.

**Where this stands on a real session** (SimCity 2000, the workload that
started this): the music is clean, including while the game is busy enough to
put up its wait cursor. The panel
reads `ring 81 ms`, `starved 0.0 ms/s`, `pace guest 1.000x`, clock 100%, with
the pump regular (`post ~900/s, p50 1.0 ms, max 8.3 ms`). What remains is the
occasional dropout, a main-thread stall longer than 80 ms, visible as a
collapse in the ring chart with a red mark under it.

**The next step is to catch one rather than buy it off with more latency.**
`?audiotrace` during a dropout gives the stall's true length in `postMs.max`,
which points at its source. Two candidates worth checking first:
`persistHddIfDirty()`'s full-copy fallback (§8.6 and §21.2: 180-350 ms when
it runs, which dirty-page patches avoid) and a GC pause on the ~1 GB
heap. Both are fixable where they happen, which is better than paying for
them in output latency forever.

The Performance panel's ring chart and its `ring N ms of M target` line both
read the target from the audio thread's own stats rather than a literal, so
they follow the cushion if it is retuned instead of quietly going stale.

**What is left.** If the emulator drops below real time the audio for that
interval does not exist, and no arrangement here invents it, the clock line
on the Performance panel is what to watch. Under the fast-test multiplier the
guest outruns wall time on purpose, so the audio thread sits pinned to
whatever has been produced and the lead figure reads zero; the tests that
measure the cushion run at real speed for that reason.

**Four other arrangements were measured and rejected:** mapping by the nominal clock while sizing
the buffer from wall time (49% jitter, the two timebases must agree);
sizing and mapping both from `cpuHz * TEST_CPU_MULTIPLIER` (starves under the
multiplier, where the host cannot reach 20x); a smoothed
guest-cycles-per-second on the main thread (starves *and* trims there); and
letting the buffer follow the delivered audio with the ring carrying the
slack (best of the four in the harness at 0.70% jitter, and worse on the real
workload: music alternately slowing and speeding up). A synthetic main-thread load does not reproduce what a real DOS
game's redraw does to this path, which is why `?audiotrace` exists.

### Open on the SB16

- **Burst DMA timing is the cheap middle option** (§33): bursts are capped
  at about a millisecond of frames instead of draining whatever
  `frame_credit_` has accumulated (up to the full 256KB buffer, where a block
  that big or a long main-thread stall would move and become audible in one
  step). Doing it exactly means one DMA cycle per sample, 176 400
  servicings a second at 44.1 kHz 16-bit stereo, against the per-instruction
  budget §8 and §14-16 were spent buying back, so a program racing its own
  buffer can still be heard up to that ~1ms cap late, and the DMA count still
  steps once a burst rather than sliding continuously.
- **Two behaviours are not in Creative's documentation.** The 1993-1996
  *Sound Blaster Series Hardware Programming Guide* has its command
  chapter. That chapter runs D9h/DAh, then E1h ("Get DSP version
  number"), then Appendix A. E0h, E2h, E3h, E4h, E8h, F2h and F3h are
  not in it. Chapter 3 and Appendix B describe ADPCM as a reference byte
  followed by 4-bit, 3-bit or 2-bit codes, and they do not publish the
  step or adjust values. ADPCM output is therefore not played (§11.3). E2h
  (DMA identification) takes one parameter byte and makes no DMA write; the
  one-byte parameter count is unsourced. A measurement of a real card, or a
  disassembly of the DSP ROM, would let either come back.
- **The tone controls' corner frequencies are an estimate.** Mixer 44h-47h's
  +-14 dB range in 2 dB steps is documented; the CT1745's actual shelf
  corners are not, so the front end uses a 100 Hz low shelf and a 5 kHz high
  shelf, labelled as uncited at the point of use. A measurement from a real
  card would replace them.
- **SB-MIDI (30h/31h/34h-38h) is still absent**, the card's other MIDI
  path, which shares the DSP's own ports and its 8-bit interrupt status bit.
  The MPU-401 UART interface covers what period software actually selects, so
  this is low value. MPU-401 *intelligent* mode is not a gap: a real SB16
  implements only UART mode.
- **The mixer's CD input and its 3Ch output switches are live** (§32): CD-DA
  playback exists, routed through mixer 36h/37h and 3Ch rather than straight
  to the front end. The line and mic inputs remain switches with nothing
  behind them, this machine has no line-in source or microphone jack.
- **8237 leftovers.** The status register's terminal-count and request bits
  are modelled (§33); the verify/single/block/demand mode distinctions are
  still not modelled. No period Sound Blaster or floppy driver needs them.
- **The card's own CD-ROM interface at base+10h..13h decodes but has no drive
  behind it** (§11.1), so a Panasonic/Matsushita-interface CD driver finds
  nothing there.
- **Two deliberate non-fixes.** base+4h returns the mixer index on read,
  which Appendix A calls write-only but real cards do answer; and the
  write-buffer status at base+Ch always reports ready, where real hardware is
  briefly busy after each byte. Both stay as they are on purpose.

## 32. CD-DA: the drive plays audio into the mixer's CD input

The mixer's CD input has a signal behind it: `atapi_cdrom` plays CD-DA (Red
Book audio), sourced to
**SFF-8020i** ("ATAPI Packet Interface for CD-ROMs", revision 2.6), the
primary ATAPI/MMC specification this drive's register interface already
follows elsewhere in the file.

**Mixed-mode discs.** `mount()`'s flat-ISO path is one all-data track, and
`mount_cue()` takes a CUE+BIN pair alongside it:
`FILE`/`TRACK`/`INDEX` only, the subset a real single-BIN game rip actually
uses (pregap `INDEX 00` lines are ignored; only `INDEX 01`, the track's real
start, is read). Data tracks stay 2048-byte cooked sectors feeding the
existing flat `image_` `cmd_read10()` already reads; audio tracks are raw
2352-byte Red Book frames (588 stereo 16-bit sample pairs each) decoded into
a separate `audio_pcm_` buffer. The last track's length is the remaining
bytes in that track's own sector size as the parse walks forward, falling
back to "whatever bytes are left" only for the one track with no following
`INDEX 01` to measure against. (Dividing `bin_len` by the last track's
sector size would miscount whenever an earlier track used a different sector
size, such as a MODE1/2048 track ahead of an AUDIO one, i.e. every
mixed-mode disc this exists for.)

**New commands**, all gated behind the drive actually having media the way
every other data command already is: `PLAY AUDIO(10)`/`PLAY AUDIO MSF` (45h/
47h), `PAUSE/RESUME` (4Bh), `STOP PLAY/SCAN` (4Eh), `READ SUB-CHANNEL` (42h,
format 1, current audio status and position; formats 2/3 answer MCVal/TCVal
clear rather than claiming a UPC/ISRC this drive doesn't have), and
`MODE SELECT(10)` (55h) for mode page 0Eh (CD-ROM Audio Control Parameters),
the one legitimate use of data-out on a read-only drive, since a real CD
driver uses it to route and mute the four audio ports before calling PLAY
AUDIO. That takes an actual data-OUT phase (`Phase::kDataOut`) for
`write_data_byte()`'s host-to-device byte. `TOC` and `MODE SENSE(10)`'s page
2Ah report real per-track CONTROL bits and genuine audio-capability flags.
WRITE and every other data-out CDB stay absent (§6.1); this is not general
write support, just the one audio page.

**Mode page 2Ah length.** `cmd_mode_sense10()` sets the page's length byte to
`0x12` (`p[1]`): SFF-8020i Table 68 gives the page as 18 more bytes after the
2-byte header, 20 total, and the test's expected response size is 28 bytes.

**The playback clock** follows this file's own established pattern
(`SoundBlaster::advance()`'s CPU-cycle-credit pacing, same back-dated
per-sample timestamp discipline): `advance_audio()` accrues real CPU cycles
against 44.1kHz and decodes one more frame per `cycles_per_frame` elapsed,
stopping on its own at the end of the requested range or at a track-type
boundary. The cycle baseline it measures elapsed time from (`audio_prev_cycles_`)
updates every tick, and only credit accrual is gated on the pause flag, so
paused time is tracked but produces nothing, which is the contract PAUSE
has. (A baseline that went stale during a pause would make a resume after a
long pause see one huge `delta` and produce an entire track's worth of
frames in one burst.)

**SB16 side**: `cd_gain_left()`/`cd_gain_right()` read mixer 36h/37h (CD
volume) through the existing `five_bit_gain()` table, gated by 3Ch's CD.L/
CD.R output-switch bits, same shape as `fm_gain_left/right()`, with a
real signal behind it. The front end's worklet (`kSbWorkletSrc`) has a
third ring alongside the FM and digitized-audio ones: the "sums in the
analog domain, no new `AudioWorkletNode`" design §31 describes extends
unchanged, since nothing about placement or mixing is specific to how many
analog inputs are being summed. The CD-ROM bay's file picker accepts a CUE
file and its companion BIN as a two-file selection (`<input multiple>`),
and takes the single-ISO path when exactly one non-CUE file is chosen.

**Tests**: native cases in `atapi_cdrom_test.cpp` (PLAY AUDIO at the
real sample rate, pause/resume, stop, the illegal-mode-for-track and
end-of-user-area rejections, READ SUB-CHANNEL position reporting, TOC control
bits on a synthetic mixed-mode disc, MODE SELECT page 0Eh round-tripping and
its rejection of any other page) plus the SB16 gain accessor, and
`web/tests/cdda.spec.ts` that mounts a synthetic CUE+BIN through the real
secondary-ATA-channel register protocol (`SendPacket`'s browser-side
equivalent, driven through `portIn`/`portOut` the way a DOS CD driver would)
and confirms `cdromDrainSamples()` reproduces the mounted track's exact PCM,
left/right distinguishable, stopping on its own past the requested range.
`web/tests/cdrom.spec.ts` has the UI-level CUE+BIN mount case.

**What is still open**: MODE SELECT's one real-world omission is that only
page 0Eh is accepted, every other page (including the two in MODE SENSE's
page-3Fh "all pages" answer) still reports CHECK CONDITION, which matches
`require_media()`'s existing all-or-nothing style elsewhere in this file but
is narrower than a real multi-page-capable drive. Burst-accurate DMA timing
for the digitized path, the 8237 status register, and the OPL3 items are
tracked in §31 and §33.

## 33. SB16 burst-DMA capping, and the 8237 status register

Two of §31's smaller items.

**Burst DMA timing.** `SoundBlaster::advance()` caps each burst at
`max(1, rate_hz_ / 1000)` frames, about a millisecond's worth; whatever
credit the cap leaves behind is not consumed and carries to the next
`advance()` call. Without the cap, a big auto-init block, or several ticks'
worth piling up behind a main-thread stall, would drain `frame_credit_` in
one step (bounded only by the 256KB internal buffer), potentially tens of
milliseconds late and audible. The `RunUntilTransfer`/`ServeBurst` test loop
pattern tolerates a block arriving as several bursts (most tests loop until
a target byte count is reached).
`LongBlocksArriveAsMultipleSubMillisecondBurstsNotOneBigOne` asserts the cap
bites (a 256-byte block at ~11kHz needs on the order of twenty-odd bursts,
each at or under the cap) and that the full block arrives intact across
them. This is the "cheap middle option" §31 names, not the exact model (one
DMA cycle per sample, 176 400 servicings a second at 44.1kHz 16-bit stereo).

**The 8237 status register** (`in(8)`, reg offset 8) reports status, not the
command register's last-written value. Per the Intel 8237A-5 data sheet's
"Status Register": bits 0-3 are per-channel terminal-count, latched when a
channel reaches TC and cleared by reading the register; bits 4-7 are
per-channel request, live and unlatched. Both are modelled:

- `advance()` sets a per-channel `tc_latch_` bit on terminal count (in
  addition to its existing return value, which callers already use for
  autoinit/wrap decisions); `in(8)` reports and clears all four TC bits
  together, matching the one real status register the chip has.
- `Dma8237::set_dreq(channel, bool)` is called every tick from
  `chipset.cpp` with whatever each channel's device currently reports
  wanting (`Fdc765::transfer_ready()` for DMA1 channel 2,
  `SoundBlaster::transfer_ready()` for whichever channel/controller the
  card's mixer jumpers currently select), independent of that channel's
  mask bit, since a real device asserts DREQ whether or not the controller
  is listening. `in(8)`'s request bits OR this together with the existing
  software Request Register (`out(9)`), which uses the SBPG/8237A-5 data
  sheet's encoding: bits 0-1 select the channel and bit 2 sets or clears that
  one channel's bit, the same shape as the single mask register (reg 10) and
  mode register (reg 11). Exercised by
  `StatusRegisterRequestBitAlsoFollowsTheSoftwareRequestRegister`.

**Where this is actually observable**: everything else in this emulator
resolves a DMA transfer synchronously, within the same tick that sets DREQ
(`chipset.cpp`'s `service()` checks `transfer_ready()` and moves the bytes
in the same call), so the request bits read back 0 in the ordinary case,
there is no real time window where a byte is in flight. The one case they
are genuinely visible is a channel left masked while its device wants
service: DREQ stays asserted with nothing to clear it, exactly the state a
driver's "is the device still asking?" poll would want to observe before
unmasking. Covered by
`StatusRegisterRequestBitStaysSetWhileMaskedSinceItIsNeverServiced`.

**Not modelled**: the mode register's verify/single/block/demand
distinctions, §31 already noted no period Sound Blaster or floppy driver
here needs them, and that remains true; this section only closes the
status-register half of that bullet.

**Tests**: a `soundblaster_test.cpp` case for the burst cap, and
`dma8237_test.cpp` cases for the status register (reads zero after reset,
the TC latch sets-and-clears, TC is per-channel, the request bit follows
live DREQ unlatched, the request bit also follows the software Request
Register, and the masked-channel visibility case). Nothing here is
front-end-visible.

## 34. OPL3 test coverage: KSL, KSR, the four four-operator algorithms, waveforms 1-6

These tests cover §31's synthesis-test-coverage item. None of them assert
anything about the envelope's per-sample shape (§35), only routing, scaling,
and waveform identity, so they hold against §35's Table 3-6 envelope.

**Four-operator algorithms.** The connection formulas are taken from
moddingwiki's OPL chip reference (cross-checked against the same formulas
in period AdLib programming documentation), which gives, for each of the four
(cp,cs) cases:
FM-FM (0,0) = Op1\*Op2\*Op3\*Op4, output Op4 alone; AM-FM (1,0) =
Op1+(Op2\*Op3\*Op4), output Op1+Op4; FM-AM (0,1) = (Op1\*Op2)+(Op3\*Op4),
output Op2+Op4; AM-AM (1,1) = Op1+(Op2\*Op3)+Op4, output Op1+Op3+Op4 (Op2
feeds Op3 but is never itself summed). `opl3.cpp`'s four branches match
this exactly. The tests for the three combinations the existing case does
not reach isolate one operator at a time (fast attack, full volume, EGT hold)
while silencing the other three (AR=0, so their envelope never leaves
maximum attenuation regardless of what phase they're fed) and check whether
the isolated operator's signal reaches the output bus, which is a much
smaller and faster check than analysing a modulated waveform's shape.

**Two details the tests depend on:**

- `Operator::env_level`'s default member initializer (`opl3.h`) is
  `511 * 65536` (kEnvMax's own value, inlined since `kEnvScale` is private
  to the .cpp). Envelope attenuation carries `kEnvScale` (65536) fixed-point
  precision, so a bare 511 would represent *near-zero* attenuation (full
  volume), not silence. `Opl3::reset()` always sets it explicitly, so the
  default only matters to a test that constructs a bare `Opl3`, never
  resets it, and relies on an **unkeyed** operator (AR=0, so the attack path
  never runs) staying silent.
- Writing a four-operator pair's connection bit straight to C0h
  (`w(0, 0xC0, cp_cnt)`) also zeroes bits 5-4, the pan bits. That is
  harmless with NEW clear (pan is forced on), but NEW is set for four-op
  mode, so the whole primary channel goes silent regardless of algorithm.
  The four-op tests OR in 0x30.

**KSL** (`ksl_env_units()`) matches Table 3-5 of the YM3812 application
manual (§35): at BLOCK=7 and the table's largest
F-number-top-4-bits entry (21 dB at KSL=1), KSL=2 measures half that (1.5
dB/octave) and KSL=3 double (6 dB/octave), both within the dB arithmetic's
own rounding. **KSR** (`effective_rate()`) likewise: at a fixed, high
key-scale number, KSR=1 decays an order of magnitude faster than KSR=0 at
the same DR register, matching `RATE=(rate)*4+Rof`'s documented Rof
selection (the full key-scale number vs. it shifted right by 2). Both
measured by isolating the carrier (silencing the modulator via AR=0, so
`compute_sample()` sees it as an unmodulated oscillator) and comparing peak
amplitude in dB rather than trying to read the envelope's internal units
directly.

**Waveforms 1-6**, verified against `wave_sample()`'s own documented shape
for each (half sine, full-wave-rectified sine, repeated rising quarter,
double-frequency sine in the first half only, its rectified twin, and a
no-shaping full-amplitude square) by sampling specific frames of a fixed
fnum=64/block=0 cycle (phase advances by exactly 64 units/frame, so frame
indices map onto quadrant boundaries exactly) and checking the
sign/silence/repetition pattern each waveform's code path implies, rather
than reconstructing the expected waveform independently.

**Tests**: two four-op-algorithm cases (FM-AM is covered by the existing
case), one KSL case, one KSR case, and one case per waveform 1-6, all in
`opl3_test.cpp`. Nothing here is front-end-visible, and `opl3.h`'s default
is on no path a real caller takes.

## 35. OPL3 envelope generator, vibrato and waveform 7: sources

**Sources.**

- Yamaha YM3812 Application Manual (1994-06), §3-1-4 to §3-1-7, §3-3,
  Fig. 3-1/3-2, Table 3-6 "Attack and Decay Times for Various Rates" (p.16).
  https://archive.org/details/YamahaYM3812ApplicationManual
- Yamaha YMF262 catalog datasheet (1994-11), register descriptions for
  KSR, EGT, SL, VIB, DVB and WS. https://bitsavers.org/components/yamaha/YMF262_199411.pdf
- Gambrell & Niemitalo, "OPLx decapsulated" (2008). It covers the log-sin
  and exp ROMs only. It says nothing about the envelope generator or the
  vibrato, and suggests waveform 7 "is probably simply the exponential
  table".
- Yamaha YMF278B (OPL4) application manual, FM section: tremolo figure
  (p.43, a 3.7 Hz triangle from 0 dB to the DAM depth), vibrato figure
  (p.44, a triangle swinging both ways about the note), waveform figure
  (p.53). map.grauw.nl/resources/sound/yamaha_opl4.pdf
- Yamaha YM2413 (OPLL) application manual, III-4 and Fig. III-3: the
  percussion sounds are output twice because they "seem lower" than the
  melody. map.grauw.nl/resources/sound/yamaha_ym2413.pdf
- US 4,249,447 (Tomisawa, Nippon Gakki, 1981), averaging device 23: the
  feedback loop takes the mean of two adjacent samples to stop hunting.
  US 4,554,857 (Nishimoto, Nippon Gakki, 1985), adder 62: "the average of
  the last output signal and the last but one output signal".

**What the sources give.** Attenuation is a 96 dB range in 0.1875 dB steps
(0-511). Attack is exponential, decay/sustain/release are linear in dB;
attack hands over to decay at 0 dB, decay to sustain at SL. RATE = R*4 + Rof,
and R = 0 never moves. SL steps by 3 dB, and all ones is 93 dB (496 units),
not 45. EGT=0 lets the release rate take over at SL with KON still held.
Release times equal the decay column. Vibrato is 6.1 Hz, 7 cents, or 14 with
DVB.

**Table 3-6 as the model.** Times are at fM = 3.6 MHz, 72 clocks a sample,
so 50 frames per ms. The YMF262 frame is the same 72 clocks of its
14.31818 MHz / 4 internal clock, so the table compares in frames. Decay
times halve per RM and scale exactly as 4/(4+RL) (RATE 4: 39280.64,
31416.32, 26173.44, 22446.08 ms). RATE 60-63 all list 2.40 ms decay and
0.00 ms attack. `opl3.cpp` turns that into a per-operator speed
`(4+RL) << RM` (60-63 pinned to the RATE 60 speed) and a rate remainder
carried between frames (`Operator::eg_acc`):

- Decay, sustain and release add one unit per step. The divisor 30688 is
  RATE 4's 1964032 frames over 512 steps.
- Attack takes `level -= (level + 9) >> 3` per step. The shape is fit to the
  table's attack 10-90% / 0-100% ratio (0.525; a continuous exponential
  needs an offset near 1.25 dB to hit it); 34 steps from silence, divisor
  33250 from RATE 4's 2826.24 ms. Across Table 3-6 this lands within about
  1% at the slow rates and within one frame at RATE 52-59.
- RATE 60-63 attack snaps to 0 dB on the first frame.

A retrigger starts the attack from the current level. No source says either
way; it is the reading with the fewest assumptions.

**Gaps.** No primary source describes how the real chip schedules envelope
steps (a shared counter, its period, which frames a slow rate moves on), so
the per-frame pattern here is a remainder accumulator, not the silicon's.
Times match; sample-level step placement may not. The divisors and the
attack constants (9, >>3) are fits to the table, not die values. The
decay 10-90% column (8212.48 ms at RATE 4) implies 20.1 dB of span where
linear-dB gives 19.1 dB, about 5% off; untested. Also searched for the
step schedule: the YMF262 datasheet, the YM3812, YMF278B, Y8950, YM3526 and
YM2413 manuals, the YMF289B and YMF715 datasheets, and Yamaha envelope
patents (US 4,928,569, JP S59-101698 and the Google Patents Nippon Gakki
envelope set). None describes the OPL counter.

Vibrato and tremolo are triangles (YMF278B manual pp.43-44): vibrato swings
both ways about the note, "7 cents" read as the peak, and tremolo runs from
0 dB to the DAM depth. Whether the real LFO moves in steps, and its step
size, is unsourced; it is smooth here. The YMF278B manual gives the vibrato
as 6.0 Hz and the Y8950 manual as 6.4 Hz; the YMF262 datasheet's 6.1 Hz is
kept for this chip. Phase reset on key-on is kept, unsourced.

**Waveform 7's slope is unsourced.** The datasheet figure is schematic (it
draws the ramp reaching zero mid-cycle and is near linear, which no
exponential does), and the decap note only says the waveform is probably the
exp table. The YMF278B manual's figure (p.53) is curved instead; read off the
page, it falls about 30-40 dB over the half cycle, but its two halves of the
curve disagree, so it is a drawing, not data. `wave_sample()` keeps 8 log
units per phase step (96 dB over a half cycle) as a single unsourced value.

**Other OPL3 citations, re-checked.** The decap write-up covers the ROMs
only, so nothing else in `opl3.cpp` cites it except the tables and their
use.

- *Exp decode.* The write-up says the table is read at the input's 8 LSBs,
  plus 1024 as the significand, with the MSBs as the exponent. An
  attenuation is a negative exponent, and `~t = -t-1`, so the decode
  indexes `255 - frac` and shifts by `(t >> 8) + 1`. That is derived from
  the write-up, not a claim about how the die stores the ROM.
- *KSL.* `kKslOct7Eighths` is Table 3-5's 3 dB/oct OCT=7 row (YM3812
  manual p.14) in 1/8 dB. Every lower row in the table is that row minus
  3 dB per octave, floored at 0, and Table 3-4 gives 1.5 and 6 dB/oct as
  half and double. `KeyScaleLevelMatchesTable35BelowTheTopOctave` checks
  four cells.
- *Four-operator F-number.* The YMF262 datasheet's "FM signal source
  organization" (pp.6-7) lists only A0h-A2h/B0h-B2h for a four-operator
  channel, so the primary's F-number, block and KON drive all four slots.
- *NTS.* Both the YM3812 manual (3-1-3, p.11, with its split table) and the
  YMF262 datasheet (NTS) say NTS=0 splits on the F-number's second bit
  (bit 8) and NTS=1 on the MSB (bit 9). The code follows those two Yamaha
  documents; a YMF715x document that could not be retrieved is not cited.
- *Feedback.* FB's 0, pi/16 ... 4pi is the YMF262 datasheet's FB table, and
  F = A sin(wt + bF) is the manual's formula (3). The fed-back value is the
  mean of the last two outputs, per Yamaha's feedback FM patents (US
  4,249,447 averaging device 23; US 4,554,857 adder 62).
- *Rhythm level.* Rhythm voices sum at 2x. The YM2413 manual (III-4) says
  its percussion is output twice to match the melody. No OPL2/OPL3 document
  states a rhythm level; the YM3812 manual's rhythm section (5-4) covers
  only the noise generator's use of channel 8 and 9 frequencies.
- *Unsourced:* the 8pi modulation index of a full-scale modulator into the
  next operator. Not in the YMF262 datasheet, the YM3812, YMF278B, Y8950
  or YM2413 manuals, US 4,249,447 or US 4,554,857 (which says only that the
  modulating wave is added to the phase).

**Tests** (`opl3_test.cpp`, `Opl3EnvelopeTimeTest`): attack 0-100% and
10-90% at RATE 4, 19, 38, 49, 52; release 0-96 dB at RATE 4, 22, 41, 56,
62; decay stopping and holding at SL=14 (224 units) with EGT=1; SL=15 at
496; EGT=0 releasing with KON held; rate 0 frozen. `envelope_level()` is
the test accessor. Vibrato's test measures ±7 / ±14 cents, the 6.1 Hz
half period from zero crossings, and the triangle shape (half the period
beyond half the peak; a sine LFO fails it). Each was seen to fail against a planted
regression (RL ignored, SL=15 as 45 dB, DVB ignored).

## 36. Scratchy-music diagnostics: the ring range, and restoring Enable Sound

A live SimCity 2000 session can turn FM music scratchy when laying a road,
clearing up on zoom. The Performance panel (§31's "front end's audio path")
shows no stall and no starvation at the moment of capture (clock 100%, host
well under one core, `starved 0.0 ms/s`), but the ring sits *above* its 80 ms
target, not below it. `?audiotrace` taken moments later with DevTools open
shows `guestPerWall` at 0.85: a capture with DevTools open is itself 15%
slowed by DevTools' own overhead, so this diagnostic is as vulnerable to the
main-thread-cost problem it exists to find as the thing it measures.

**The ring's range.** The panel shows ring depth, post cadence percentiles
and guest/FM pacing ratios live, with no DevTools needed (see the comment at
`startPerfPanel()`). A single current reading cannot show a brief surplus
building and the correction bleeding it off again, which is what a
transient CPU-load spike (laying a road, triggering pathfinding/tile-redraw
work) produces. So the audio block in `startPerfPanel()` carries
`range <min>-<max> ms (60s)`, computed from the same 60-entry `history`
buffer `drawAudioChart()` draws from: a number alongside the chart that is
already plotting it. A capture showed `range 0-111 ms (60s)` and a starved
mark at the base of the climb: the ring drains to empty under a load spike,
then overcorrects upward, which confirms the mechanism without needing
`?audiotrace`. A case in `web/tests/performance.spec.ts` asserts the range
appears and brackets the current reading (`min <= ring <= max`).

**Enable Sound starts audio on the first click.** Enable Sound is restored
from `localStorage` on load (`speakerCheckbox.checked = uiState.sound`),
which does not fire the checkbox's own `change` handler (setting `.checked`
from script dispatches nothing). Without another path, the checkbox would
show checked while no `AudioContext` exists until the box is unchecked and
rechecked. `screenEl`'s click handler, the first gesture every visitor
makes to focus the machine and start typing, calls `ensureAudioStarted()`
when sound is supposed to be on, matching the comment above it ("a restored
checkmark may need one click before audio actually starts"). The function is
idempotent (resumes if suspended, no-ops if already running), so it costs
nothing on every other click. A case in `web/tests/speaker.spec.ts` persists
the preference, reloads, confirms audio is *not* running yet, clicks the
screen, and confirms it is, without touching the checkbox. Both tests fail
against a sabotaged build, as §35's do.

## 37. C: in a worker, uncompressed, and converting old 504MB saves

The browser-side C: avoids three costs, given the drive layout in §3.1.

**The main thread never touches IndexedDB.** `web/hdd-worker.js` owns
the `pc486-hdd` database and runs every load, save, and conversion as one
serial job queue, so a load always sees every save queued before it.
app.js talks to it with `hddWorker.call(op, args, transfer)`. The 5 s
autosave sends only `machine.hddDirtyPatches()`, with their buffers
transferred, and the worker does a read-modify-write of each touched chunk
in one transaction.

**No gzip, and no JS mirror.** C: is stored as raw 64KB chunks (`c2:<n>`)
plus a `c2-meta` record. A chunk that's all zero has no record, so empty
space costs nothing; that's sparse storage, not compression. There is no
`savedHdd` 504MB `Uint8Array` mirror to patch. Power-on asks the worker for
the image, mounts it, and drops it. `Wd1003::mount()` has a move overload so `mountHdd` hands the converted vector straight to the
drive instead of copying it again, which matters because the wasm heap
never shrinks. On a first visit the factory image is mounted and then
transferred to the worker as C: (`modified: false`, with the factory
fingerprint). That is the whole factory-base stash: an untouched
C: from an older freedos-hdd.img is dropped on load (`forget`) so the
visitor gets the current one, and a modified one is kept.

**Old 504MB saves convert in the browser.** When the worker finds only an
old-format save (the gzip Blob under `c-drive`, or the older raw, chunked,
or factory-delta shapes), `web/hdd-convert.js` copies its FAT16 tree file
by file into the exact layout FreeDOS gave the factory image (§3.1).
Directory entries are copied verbatim, so long names, attributes,
timestamps, and the volume label survive; only start clusters change.
Files are placed breadth-first, files before subdirectories, so root boot
files keep the first clusters. The MBR and boot-sector code are kept and
the partition entry and BPB are rewritten, which is all DOS boot code needs
since it reads geometry from the BPB. The new chunks and the deletion of
every legacy key commit in one transaction. Progress shows in the screen's
load overlay. An upload of a 504MB file goes through the same path. If a
save won't fit or isn't a single FAT16 partition, nothing is written; the
old record stays, the status line says so, and a "Download old 504MB image"
button hands it back.

Tests: `web/tests/hdd.spec.ts` seeds old-format saves built by `make
legacy-hdd-fixture` (`web/tests/make-legacy-hdd.sh`, which recreates the
504MB layout with mtools from the shipped files plus a marker file) as the
gzip Blob, 512KB chunks, and a factory delta, then checks the marker after
conversion. It also covers the too-full path, a 504MB upload, every
converter refusal reason, chunk-boundary patches, all-zero chunk removal,
and the whole-image fallback. The raw-bytes shape isn't seeded: Chromium
refuses a single IndexedDB value over ~133MB, so only another browser could
have stored one. `wd1003_test.cpp` covers the move overload.

Checked against the real thing: converting the 504MB factory image produces the identical tree (1514 files, 13,215,922 bytes, `diff -r` clean
via `mcopy -s`), `fsck_msdos` reports nothing, the partition table and BPB
match FreeDOS's own byte for byte, and `hdd_boot_check` boots it to `C:\>`
with the kernel reporting a 244MB C:.


## 38. The suite on a slow CI runner

Three specs account for the host's speed instead of measuring it.

**FM quality is judged in guest time.** `fmquality.spec.ts` hears its 309.5
Hz tone at 294-296 Hz on a slow CI runner (a 3x CPU throttle reproduces the
exact number): the guest still keeps real pace, but the main thread posts
audio late, so the SB16 worklet does what it is built to do, slowing
playback up to 4% or holding, rather than dropping out. That is a
deliberate trade, so the test asks the real question. The worklet's capture
records the guest cycle each output sample played (`__test.sbCaptureCyc`),
and every period of the tone is checked in guest time: 213,247 cycles, under
5% of periods off. A period that spans a hold or a forward re-anchor is a
dropout rather than a placement error, so it is left out; with fewer than 50
clean periods the host is too starved to judge and the test skips (CI has
skipped whole periods, past anything a 4x throttle produces). The as-heard
pitch and jitter checks run only when the capture shows the host kept real
speed with no starved hold. Verified across 1x-4x throttles, and against two
sabotaged worklets: a per-post placement error fails at both 1x and 3x.

**Panel order counts visible panels.** `drivers.spec.ts` counts only the
visible panels, since the hidden Performance panel sits between the front
panel and the hard disk panel.

**Smoke pacing starts after start-up.** The real-speed check waits a second
before opening its 2-second window, because a slow runner is still tiering
up the wasm and writing the first C: to IndexedDB when the machine goes
live.

## 39. A parity audit: reserved opcodes, AAA/AAS, DSKCHG, and what stays out

An audit of this machine against a real 1993-94 DX2-66 build, and of its
test coverage. It covers four behaviors below, and records three gaps that
stay open.

### 39.1 Reserved encodings raise #UD

Every encoding the core does not recognise calls `on_unimplemented` and then
raises #UD. Intel SDM Vol. 3, "Undefined Opcodes": "Attempting to execute
a reserved opcode always results in an invalid-opcode (#UD) exception."
Software depends on that. A handler around a later instruction (RDTSC,
CMPXCHG8B, FCMOV, FISTTP) is how a program tells a 486 from what came after
it. Carrying on would run the next bytes as if the probe had worked.

`Cpu::raise_ud()` fires the hook and then raises #UD. It covers the
one-byte map, the unassigned 0F space (including UD2, `0F 31`, `0F C7`, and
`0F A6`/`A7`, the A-step CMPXCHG slot this part doesn't decode), `FF /7`,
`FE /2-/7`, `0F BA /0-/3`, and register operands where only memory makes
sense: LEA, LES/LDS/LSS/LFS/LGS, BOUND, and far CALL/JMP indirect.

Two encodings the SDM names as reserved but never faulting stay that way:
`F1` (ICEBP, a #DB trap, §46.3) and `D6`, SALC (`AL = CF ? FFh : 00h`, flags
untouched). SALC is undocumented but present from the 8086 on. There's no
published 486 timing for it, so it's charged as SBB AL,AL.

On the x87 side, the reserved forms fault before the FPU pointers or the
deferred-error check move. That covers FCMOVcc, FCOMI/FCOMIP/FUCOMI/FUCOMIP,
FISTTP, and the empty register and memory slots (`D9 /1`, `DD /1`, `DD /5`
and `DF /1` are not FNSTCW, FNSTSW or FISTP).

The audit also covers encodings a real 486 does execute:

- **FUCOM / FUCOMP ST(i)** (`DD E0+i`, `DD E8+i`). These are documented 387
  instructions.
- **The undocumented register aliases**: `DC D0`/`D8` (FCOM/FCOMP), `DE D0`
  (FCOMP), `DD C8` and `DF C8` (FXCH), `DF D0`/`D8` (FSTP), `DF C0`
  (FFREEP), and `D9 D8+i`, the FSTP that skips the stack-underflow check.
  The DC and DE compare aliases are compares, not the neighbouring
  divide-reverse arithmetic. Source: the "List of undocumented x86 instructions"
  and sandpile.org's FPU map.

With these in place, `hdd-boot-check` reaches the idle `C:\>` at cycle
1,036,029,044 (§19.5 gives the figure for the Base image). `pm-check` and `vbe-check` pass.

### 39.2 AAA and AAS adjust AX, not AL

From the 80286 on, AAA adds 106h to AX and AAS subtracts 6 from AX, so a
carry or borrow out of AL reaches AH (Intel SDM AAA/AAS; the 8086-vs-286
difference is noted in Intel's 8086 opcode notes at pcjs.org). The 8086
form gives 0105h for `AX = 00FFh` after AAA, where a 486 gives 0205h, and
the core does the 486 one.

### 39.3 Ejecting a floppy asserts DSKCHG

A 3.5" drive asserts DSKCHG when the disk comes out and resets it only on a
step pulse with a disk inserted (TEAC FD-235HF spec, "Disk Change").
`Fdc765::unmount()` drops the image and sets the line, and a step clears it
only with a disk in. An empty bay keeps reporting "changed", which is how
DOS sees a missing disk.

### 39.4 Coverage

`make coverage` includes `opl3_test` in `UNIT_TESTS` and `mpu401.*` in the
report (leaving them out reads the OPL3 at 68% lines when it is 99%).
Tests cover all sixteen conditions through SETcc and short Jcc, 16-bit
shifts and rotates (`shiftrot16` had never run), 8-bit rotates, DAS,
AAA/AAS, word I/O through `Chipset::io_out16`, all eight DMA page ports,
FDC unmount, and every #UD and alias above. `volume.spec.ts` covers the
SB16 volume wheel. At the time of the audit native totals were 93.0% lines
and 79.3% branches (89.2% and 72.8% before it).

### 39.5 Not implemented

These are real departures from period hardware, recorded here.

- **Cache and bus timing** (done in §47). Every memory and I/O access is charged at the
  486's published L1-hit cost. There are no L1 misses, no 33 MHz bus cycles
  for an external access, and no ISA or VGA wait states. Code that hammers
  memory or video RAM runs faster than a real board would. Fixing it means
  an L1 model (8KB, 4-way, write-through), an L2 decision for a 1993 board,
  and a per-region wait-state table (ISA I/O, VGA aperture, ROM). All of it
  sits on the hottest path, which §14-§16 spent a long time making cheap.
  It needs its own design pass, with period board numbers to cite.
- **Game port (0x201).** A real SB16 carries one, and flight sims of the
  era expect a joystick. It wants a front-end input story (Gamepad API or
  keys) as much as the 558-timer emulation.
- **Serial and parallel ports.** No COM1/COM2 (8250/16550) and no LPT1,
  though every period board had them. That rules out serial mice,
  null-modem play and printing.

## 40. A second audit: the clock, key repeat, VGA colour, and three CPU gaps

A second pass over the machine, aimed at the devices and CPU edges §39
doesn't cover. Six behaviors are below. The rest of what it found is listed
in §40.7.

### 40.1 The RTC keeps time

`CmosRtc` runs from its own 32.768 kHz
time base in guest time, so Turbo doesn't change its rate. Once a second it
runs the MC146818A update cycle: UIP rises 244 us ahead, the registers roll
over 1984 us later, then the update-ended and alarm flags are checked. The
periodic flag follows register A's rate select (1024 Hz by default). Any
enabled flag drives IRQ8, slave line 0, until register C is read. BCD and
binary, 12- and 24-hour, SET, the divider-reset half-second, the
every-fourth-year leap rule and the alarm's don't-care bytes all follow the
data sheet. Registers C and D are read-only, as on the chip.

The part is 128 bytes, the DS12887 class a 1993 board carries. At 64
bytes, Bochs BIOS reads of 0x5B-0x5D would alias onto the disk geometry at
0x1B-0x1D.

A real board's battery has kept the clock running, so each power-on loads
the visitor's local time (`Machine.setRtc`, from `app.js`). Native
harnesses that don't set it start at 1994-01-01 00:00:00.

### 40.2 The keyboard repeats

The page drops the browser's repeat events, so the keyboard behind the 8042
repeats its most recently pressed key on its own clock: 500 ms delay and
10.9 cps at power-on, changed by `F3` (so `MODE CON RATE=` and INT 16h
AH=03h work), restored by `F6` and `FF`. A grey key repeats with its `E0`
prefix. Pause never repeats. Releasing the key, or pressing another, ends
it, including when the controller has the keyboard disabled at that moment.
Sources: IBM PS/2 Technical Reference, Set Typematic Rate/Delay; Chapweske,
"The AT-PS/2 Keyboard Interface"; IBM PC AT Technical Reference
(6139362, Sep 1985) p.4-7 for the period `(8+A) * 2^B * 0.00417 s` and the
`(1 + bits 6-5) * 250 ms` delay, both +-20%.

Three rules keep multi-byte scan codes from sticking keys in Doom. The page
sends every sequence through one queue, so two scan codes never interleave
(a real keyboard sends one whole before the next). Without that, releasing
W and D (Up and Right on the WASD preset) together with 20 ms gaps could
reach the keyboard as `E0 E0 C8 CD`, Right's break would lose its `E0`,
fail to match the repeating `E0 4D`, and Right would repeat forever. A
break of the repeating key's code ends the repeat whatever its prefix. And
a repeat waits while the host is partway through a sequence.

### 40.3 Text and 16-colour modes go through the DAC

Text, CGA-compatible and 16-colour pixels do not use the EGA's fixed
64-colour scheme here (that is right for `ibmpc-at`'s EGA): on a VGA the palette registers address the DAC (IBM VGA Technical
Reference, "Attribute Controller"). Color Select supplies DAC bits 6-7, and
bits 4-5 too when AR10 bit 7 is set. Color Plane Enable masks the planes in
16-colour graphics. Palette fades and custom palettes outside mode 13h
show. A real FreeDOS boot renders as it would on the EGA scheme, because
the VGA BIOS loads the EGA-compatible table into DAC 0-63 on every text or
16-colour mode set.

### 40.4 Single-step

An instruction that begins with TF set is followed by #DB, with the next instruction's address saved and DR6.BS set. A POPF that
sets TF runs untrapped; one that clears it still traps. INT n, INT3 and
INTO clear TF and enter their handler untrapped. DEBUG's `T` command works.
REP string instructions still run every iteration inside one step, so they
trap after the last iteration rather than after each one.

### 40.5 The STI and MOV SS interrupt shadow

STI (from IF=0), MOV SS and POP SS hold off maskable interrupts and the single-step trap
for one instruction, so `MOV SS / MOV SP` can't take an interrupt onto a
half-loaded stack (Intel 80486 PRM, MOV and STI).

### 40.6 Shutdown resets the CPU

A fault while delivering a double fault is a shutdown, and an AT-class
board decodes the shutdown cycle and pulses RESET (halting instead would let
the next IRQ wake the CPU into the broken IDT that caused it). `Machine`
does the same CPU-only reset the 8042's pulse gives, so memory and CMOS
survive. Software that leaves
protected mode this way, or crashes, reboots instead of wandering.

### 40.7 What the audit left to later sections

Tracked in [`PC486_PARITY.md`](PC486_PARITY.md). VGA text layout, scroll
effects and the 4:3 aspect are §42 and §43. PIT modes and the PIC poll
command are §41. Real-mode limits, FXCH, LOCK, the 15-byte limit and the
reset register state are §44. Still open: IDE commands beyond IDENTIFY,
INITIALIZE PARAMETERS, READ and WRITE (VERIFY, SEEK, READ/WRITE MULTIPLE,
SET FEATURES, diagnostics and power commands abort); keyboard `EE`
answering ACK instead of `EE`, and `F0 00` returning no scan-code set; and
FORMAT TRACK leaving the old sector data in place.

## 41. The PIT gets all six modes, and the PIC answers a poll

Items P1-P4 from [`PC486_PARITY.md`](PC486_PARITY.md).

### 41.1 Every counter mode

A symmetric square wave toggling every N/2 clocks gets the edge rate right
for the BIOS tick and nothing else. Mode 0 is a one-shot, and a program that
sets it up and polls for OUT going high must not see it fall again N/2
clocks later. `Pit8253` follows the
Intel 8254 data sheet (231164), "Mode Definitions", mode by mode:

- **Mode 0.** OUT drops on the control word and on a count write, rises at
  terminal count and stays up. The first byte of a two-byte count stops
  the counter. Gate low pauses it.
- **Mode 1.** Waits for a gate rising edge, then OUT is low for N clocks.
  Retriggerable.
- **Mode 2.** OUT is low for one clock in every N. A new count takes effect
  at the next reload.
- **Mode 3.** Counts down by two. An odd count loads N-1, and OUT holds
  high one extra clock, so it's high (N+1)/2 and low (N-1)/2. A new count
  waits for the end of the half-cycle.
- **Modes 4 and 5.** One clock low at terminal count, once. Mode 4 starts
  on the count write, mode 5 on a gate edge.
- Modes 6 and 7 alias 2 and 3.

A count reaches the counting element on the next CLK after it's written,
so mode 0's OUT rises N+1 clocks after the write, as the data sheet's
timing diagrams show. The counter wraps to FFFFh (9999 in BCD) after
terminal count in the one-shot modes, and keeps counting.

Gates 0 and 1 are tied high on an AT, so modes 1 and 5 never fire on
those channels. That's the real board. Only channel 2's gate (port 61h
bit 0) moves. Gate low in modes 2 and 3 still forces OUT high, which the
speaker's direct-toggle playback depends on.

A control word that raises channel 0's OUT (mode 0 to mode 2, say) is a
rising edge, and IRQ0 sees it.

### 41.2 Read-back, status, BCD

The 8254 read-back command (control word 11xxxxxx) latches count, status or both for any set of channels. Status is OUT,
NULL COUNT and the six programmed control bits. A latched status reads
before a latched count. A second latch before the first is read is
ignored, for both kinds. BCD counting works, with 0 meaning 10000.

The read and write byte flip-flops are separate, as on the chip, so
read LSB, write LSB, read MSB, write MSB is legal.

### 41.3 PIC poll

OCW3 with P=1 makes the next read of either port an interrupt
acknowledge. It returns bit 7 set and the level in bits 2-0, sets ISR and
clears IRR, or returns 0 when nothing unmasked is pending. Source: Intel
8259A data sheet, "The Poll Command".

### 41.4 Checks

`hdd-boot-check` reaches the idle `C:\>` at cycle 1,036,029,604, 560
cycles after §39's figure because of the load clock. `pm-check` and
`vbe-check` pass. `pit8253_test` has one or more cases per mode plus
read-back, BCD and the flip-flops. `pic8259_test` has three poll cases.

## 42. VGA text layout, panning, split screen, and a 4:3 monitor

Items V1-V7 from [`PC486_PARITY.md`](PC486_PARITY.md). Register meanings
are from the IBM VGA Technical Reference (CRT Controller, Sequencer,
Attribute Controller).

### 42.1 One scan-line model for every planar mode

The text, 16-colour and 256-colour renderers share `LocateRaster`, which
does what the CRTC's address counter does on each raster line (so the start
address selects the page for EGA page flipping, and split screens and
panning apply to every planar mode):

- Above Line Compare (CRTC 18h, plus R07 bit 4 and R09 bit 6), the row
  starts from the start address plus byte panning (CRTC 08h bits 5-6),
  and Preset Row Scan (CRTC 08h bits 0-4) starts the first row part-way
  down.
- Past Line Compare, the address restarts at 0 and the row scan at 0.
  That's the split screen.
- Scan doubling and Max Scan Line divide raster lines into rows.

Line Compare counts raster lines, so mode 13h's 200 rows are 400 lines
to it. The renderers draw one output line per logical row, so a split
that lands on the second raster line of a doubled row shows from the
next row. `Ega::reset()` loads Line Compare at its 10-bit maximum, so a
card nobody has programmed shows no split.

### 42.2 Pel panning

AR13 shifts each line left before display. In 8-dot text and 16-colour
graphics it's 0-7 pixels. In 9-dot text 8 means none and 0-7 mean 1-8,
which is why the BIOS loads 08h for text modes. In 256-colour mode it
counts half pixels. AR10 bit 5 turns panning off below the split, which
is how a game scrolls the playfield under a fixed status bar. VBE modes
pan with their own X/Y offset registers and ignore all of this.

### 42.3 Text the way a VGA draws it

- **Rows** come from Vertical Display End over the row height, so 43-
  and 50-line modes show every row. 43 lines on a 350-line raster leaves
  six lines of a 44th row, drawn as the monitor shows it.
- **Cells are 9 dots** unless SR01 bit 0 selects 8. With AR10 bit 2 set,
  column 9 repeats column 8 for C0h-DFh so box-drawing lines join, and
  everything else gets background there. FreeDOS's prompt is 720x400, as on
  a real VGA.
- **Attribute bit 7** blinks the foreground when AR10 bit 3 is set, and
  selects a bright background when it's clear.
- **Blink timing** is the card's own. `Ega` counts vertical frames, the
  cursor toggles every 8 and blinking characters every 16. The page's
  1.9 Hz timer is not involved, so `renderFrame()` takes no argument.
- **SR03** picks font map A for attribute bit 3 set and map B for clear,
  at the eight plane-2 offsets the VGA uses. That's 512-character mode.
- **Color Plane Enable** (AR12) masks text colours too, which is how
  512-character software keeps bit 3 from brightening.

### 42.4 A 4:3 screen

A VGA monitor fills its 4:3 tube in every mode, where sizing the screen to
each mode's pixel ratio would draw 320x200 at 16:10 and 720x400 nearly 2:1.
`#screen` is `aspect-ratio: 4/3`, and `app.js`
scales the frame by a whole factor on each axis before the browser's
final smooth scale, so pixels stay even.

The taller monitor makes the modern theme's mini-tower taller too. Both
blank 5.25" covers fit at every width where the tower sits beside
the screen.

### 42.5 Checks

`hdd-boot-check` reaches `C:\>` at cycle 1,036,029,604, and
`render_screen` shows the FreeDOS boot at 720x400 with 9-dot cells.
`vbe-check` and `pm-check` pass. `ega_render_test` has one or more cases per
item, and the text cases program the SR01 and cursor they assume. `screenscale.spec.ts` checks the 4:3 box and the
per-axis scale.

### 42.6 Still open

- Blink in 16-colour graphics (AR10 bit 3 there blinks pixel bit 3).
- The CGA-compatible 4-colour path ignores the start address and panning.
- Cursor skew (CRTC 0Bh bits 5-6) and the underline row (CRTC 14h).

## 43. Graphics blink, CGA scrolling, cursor skew and underline

Items V8-V10 from [`PC486_PARITY.md`](PC486_PARITY.md), the three left
open in §42.6.

### 43.1 Blink in graphics modes

With AR10 bit 3 set in a graphics mode, the pixel leaves Color Plane
Enable and reaches the blink logic, and a pixel with bit 3 set drops it
in the off phase. The IBM VGA/XGA Technical Reference (May 1992) shows
this order in its Attribute Controller block diagram (Figure 2-3, p.2-10):
Color Plane Enable Logic, then APA Blink Logic, then the Internal
Palette. AR10's Enable Blink field (p.2-93) says only that the bit
"enables blinking". The manual never says which pixel bit blinks or how.

Unsourced: that bit 3 is the blinking bit; that the on phase forces bit 3
to 1 for pixels that lack it; the blink rate (the character rate is used).
With plane 3 disabled the masked bit 3 is 0, so nothing blinks (nothing
sourced says otherwise). Richard Wilton's, Abrash's and Ferraro's books were not available to
check.

### 43.2 CGA-compatible mode scrolls

The 4-colour path goes through `LocateRaster` like the other planar modes, so the start address, byte
and pel panning and Line Compare apply. CRTC 17h bit 0 still swaps row
scan bit 0 into address bit 13, which is what makes CGA's two 8KB banks.
A CRTC nobody has programmed keeps mode 4's layout.

### 43.3 Cursor skew and underline

Cursor Skew (CRTC 0Bh bits 5-6) draws the cursor 0-3 character clocks
right of its address. An attribute of foreground 1 on background 0
(`attr & 77h == 01h`) draws a line on the Underline
Location row (CRTC 14h). It fills dots 1-8, so in 9-dot modes the line
is dashed between cells and solid through C0h-DFh with line graphics on,
as IBM's "Programming Considerations" describes. Colour modes put the
row past the cell, so normal text shows none.

### 43.4 Blink rates, checked

FreeVGA toggles the cursor every 16 frames, the same as blinking
characters. IBM's manual gives the VGA cursor
VSYNC/16 and characters VSYNC/32, both 50% duty: a toggle every 8 frames
and every 16, which is §42.3's rule. The same manual has
line graphics the way §42.3 does (bit 2 set repeats column 8), where
FreeVGA's register page has it reversed.

`ega_render_test` has cases for each item. `vbe-check` passes, and the
screen-scale, smoke, boot and keyboard specs pass.

## 44. CPU edges: reset state, FXCH, LOCK, length, real-mode limits, REP under TF

Items C1-C6 from [`PC486_PARITY.md`](PC486_PARITY.md).

### 44.1 RESET loads the component ID and a cache-off CR0

EDX comes out of RESET holding 0435h, and CR0 holds 60000010h, CD
and NW set (Intel486 Microprocessor Data Book, register state after
RESET). The stepping matters. 0435h is the SL-Enhanced IntelDX2-66
(aB0/aC0, S-spec SX807 or SX911), the DX2 that has CPUID; 0433h is the
older B1 step, which doesn't. CPUID reports the SL-Enhanced part, so it
returns 0435h too (0433h is a signature that part never reported). Source for the steppings: the Intel 486
S-spec table at ardent-tool.com.

`MOV CR0` with NW set and CD clear raises #GP(0), the other invalid
combination next to PG without PE (Intel SDM, MOV CR). There's still no
cache model (T1), so CD and NW change nothing else.

### 44.2 FXCH

FXCH swaps the tags with the registers and checks for an empty operand.
An empty operand is a stack underflow:
IE and SF set, C1 clear. Masked, the empty side reads as the real
indefinite and the exchange goes ahead with both tags valid. Unmasked,
nothing moves (Intel 80486 PRM, FXCH).

### 44.3 LOCK raises #UD where a 486 does

LOCK is legal only on a memory-destination ADD, ADC, AND, BTC, BTR, BTS,
CMPXCHG, DEC, INC, NEG, NOT, OR, SBB, SUB, XOR, XADD or XCHG (Intel
80486 PRM, LOCK). Everything else, including any register destination,
CMP, BT and plain MOV, raises #UD. The decoder peeks the opcode and
ModR/M without consuming them. A LOCK #UD doesn't fire the
unimplemented-opcode hook, since the opcode is implemented. The timing test uses
the memory form of LOCK ADD, since `LOCK ADD AX,BX` faults on a 486.

### 44.4 The 15-byte limit

An instruction longer than 15 bytes raises #GP(0) (Intel 80486 PRM,
"Instruction Format"). Without prefixes no 486 instruction passes 12
bytes, so the check arms only once an instruction has four or more
prefixes. It caps the prefetch window at byte 15, and the slow fetch
path faults on anything past it. The common path pays nothing.

### 44.5 Real mode checks the cached limit

Real mode and V86 check each access against the cached limit, and nothing
else; the descriptor cache of §6.2 is what keeps unreal mode working
without exempting real mode from limit checks.
A word at FFFFh raises #GP (#SS through SS), a PUSH with SP=1 raises
#SS, and a V86 task faults the same way, as on a 486 (Intel 80486 PRM,
"Real-Address Mode Exceptions"). A 4GB limit left behind by a
protected-mode excursion passes, so HimemX's unreal-mode copies work.
`seg_off()` does not wrap a multi-byte operand at 64KB (that is 8086
behaviour).

The two addr32 tests run in a real unreal-mode setup (`enter_unreal()`),
and the V86 wrap test expects #GP(0). `hdd-boot-check` reaches `C:\>` at
cycle 1,036,029,604.

### 44.6 REP under single-step

A REP string instruction under TF runs one iteration per step and
traps after each, with the saved IP on its first prefix byte until the
count runs out (Intel 80486 PRM, "Single-Step Trap"). That's what lets
DEBUG's `T` walk a `REP MOVSB`. INS and OUTS do the same.

### 44.7 Checks

`hdd-boot-check`, `pm-check` and `vbe-check` pass. The gaps this pass left
are C7-C10 in the parity doc, handled in §45.

## 45. FPU tags, 32-bit I/O, interruptible REP, and the code-fetch limit

Items C7-C10 from [`PC486_PARITY.md`](PC486_PARITY.md), plus 32-bit IN and
OUT, and the I/O permission check on INS/OUTS.

### 45.1 The tag word classifies each register

The tag register tracks only empty (11) or valid (00). FSTENV and FSAVE
store a tag word built from the registers: 00 valid, 01 zero,
10 special (NaN, infinity, denormal, unnormal), 11 empty (Intel 80486
PRM, "Tag Word"). FLDENV and
FRSTOR keep only which registers are empty, and the rest is worked out
again from the contents on the next store. A 486 tags an infinity and the
FXCH indefinite special, and the FPU tests expect that.

### 45.2 32-bit port I/O

`IN EAX` and `OUT EAX` honour the 0x66 prefix, INSD and OUTSD move
dwords, and INS/OUTS honour 0x67. Every I/O device on this board is 16-bit, so the 486's dynamic bus
sizing (BS16#) runs a 32-bit I/O as two 16-bit cycles, the second at
port + 2 (Intel486 Microprocessor Data Book, "Dynamic Bus Sizing"). The
core does exactly that, and INS/OUTS use ESI, EDI and ECX under 0x67.

The BIOS and UDVD2 probe for PCI with `OUT 0CF8h,80000000h` /
`IN EAX,0CFCh`. An empty ISA bus returns FFFFFFFFh in all of EAX, so the
probe reads "no PCI" the way it would on a real VLB/ISA board (a stale top
half in EAX would not). `hdd-boot-check` reaches `C:\>` at 1,025,005,555,
about 11 million cycles sooner than a core that leaves EAX's top half stale.

INS and OUTS take the same IOPL and TSS bitmap check as IN and OUT, on every
transfer (Intel 80486 PRM, INS), so a CPL 3 task cannot reach any port
through them.

### 45.3 A REP can be interrupted

A 486 takes a pending interrupt between REP iterations and resumes the REP
after IRET, so a long `REP MOVSD` does not hold off every IRQ until it
finishes. Devices here only advance between CPU steps, so the CPU yields
part-way:
`Cpu::rep_yield_cycles`, which `Machine` sets to one PIT count (55
cycles at 66 MHz, 27 at 33), is the budget. A REP that reaches it puts
EIP back on its first prefix and returns. The machine services its
devices and takes any interrupt, and the next step carries on with the
count.

A continuation is charged only its own iterations, with no prefix clocks
and no setup, so an uninterrupted REP still costs exactly its published
formula. If an interrupt is taken between chunks, the instruction
restarts after IRET and pays its setup again. The 486 timing tables
don't publish a cost for a resumed REP, so that's an inference, labelled
as one.

### 45.4 An instruction can't straddle the CS limit

Data accesses fault past the limit (§44.5), and so does an instruction
fetched a byte at a time across FFFFh in real mode. The slow fetch path
checks each byte against CS's limit from where the instruction began. A one-byte
instruction at FFFFh still leaves IP at 0000h, because IP itself is 16
bits.

### 45.5 Checks

`pm-check` and `vbe-check` pass, along with the Playwright suite. The
Performance panel's audio-ring check has flaked under load and passes on
its own, as does the real-speed smoke test.

## 46. Alignment check, debug registers, ICEBP, and the reset vector

Items C11-C14 from [`PC486_PARITY.md`](PC486_PARITY.md).

### 46.1 #AC

A misaligned data access at CPL 3 with CR0.AM and EFLAGS.AC both set raises
#AC(0), vector 17 (Intel 80486
PRM, "Alignment Check"). V86 runs at CPL 3, so a DOS program under
EMM386 or Windows sees it too. The check sits after the segment limit
check and before paging.

Each word, dword and qword access is checked against its own size,
which gives the PRM's table for free: an 80-bit real reads 8 bytes then
2, so it needs 8-byte alignment, and a 16:32 pointer needs 4. Two kinds
of operand are checked as a whole instead, because their pieces don't
line up with the table. FSAVE, FRSTOR, FSTENV and FLDENV need only 2 or 4
(by operand size), even though FSAVE's 10-byte register slots fall on
odd multiples of 2. SGDT and SIDT need 4 for the whole 6-byte image,
though its base sits at offset 2.

### 46.2 Hardware breakpoints

DR0-DR3 and DR7 fire:

- **Instruction breakpoints** (RW=00) fault before the instruction at
  that linear address runs, with DR6.Bn set. RF holds them off for one
  instruction. Every fault pushes EFLAGS with RF set, so the IRET that
  restarts the instruction doesn't re-take the breakpoint, and RF clears
  when an instruction completes (Intel 80386 PRM, "Debug Exceptions"). A
  REP picking up after an internal yield (§45.3) is the same
  instruction, so it isn't re-checked.
- **Data breakpoints** (RW=01 writes, RW=11 reads or writes) trap after
  the instruction, with LEN 1, 2 or 4 and the address aligned to LEN.
  Any overlap with the access matches. A 486 always reports them on the
  instruction that caused them; the 386 needed LE or GE for that. A REP
  stops after the iteration that hit, with IP still on the instruction,
  the same as single-step.
- **GD** (DR7 bit 13) turns the next MOV to or from a debug register into
  a #DB fault with DR6.BD, and clears itself so the handler can get in.
- **The TSS T bit** traps once a task switch into that TSS completes,
  with DR6.BT, before the new task's first instruction.
- **Reset and reserved bits.** DR6 reads FFFF0FF0h and DR7 400h after
  RESET, and writes keep those bits. DR4 and DR5 alias DR6 and DR7, as
  they do on any part without CR4.DE.

Single-step, data hits and the T bit arrive as one #DB with every DR6
bit that applies. DR6 is never cleared by the processor; the handler
does that (Intel 80386 PRM, "Debug Registers"). Later Intel manuals
allow a status bit for a breakpoint that isn't enabled; this core
reports only enabled ones.

### 46.3 ICEBP

`F1` is a #DB trap through vector 1 with no gate-DPL check, unlike `INT 1`,
and no DR6 bit (sandpile.org). §39 keeps it from faulting as a reserved
opcode. It is charged INT3's 26 cycles, since nothing publishes a figure
for it.

### 46.4 The first fetch is at FFFFFFF0h

A 486 leaves RESET with CS =
F000h but CS's hidden base at FFFF0000h, so its first fetch is at
FFFFFFF0h, and the base stays there until the first far jump (Intel486
Microprocessor Data Book, "RESET"). The core does that, and the
chipset maps the top 2MB of the 4GB space back onto the BIOS ROM at
E0000-FFFFF, mirrored every 128KB. With A20 masked, the real CPU fetches
from FFEFFFF0h, which still falls in that window. This chipset masks a
closed A20 down to 20 bits, so that case lands on FFFF0h directly.

The BIOS's first instruction is a far JMP into F000h, so nothing after
it changes. A CPU reset with A20 open also goes through the
top-of-memory alias.

### 46.5 Checks

Native cases cover six debug-register behaviors, ICEBP, the reset fetch,
the T bit, six #AC cases and the chipset alias. `hdd-boot-check` reaches
`C:\>` at cycle 1,025,005,555, and `pm-check` and `vbe-check` pass. The
smoke test's main-thread blocking check has failed once under the full
suite's load and passes on its own.

### 46.6 What C1-C14 cost in speed

The checks add work to every instruction. A FreeDOS boot in the wasm build
runs host-bound at about 98 MHz, against about 116 MHz without C1-C14.
Profiling puts the extra time in the inlined step and decode path, not in
any one feature. Three cheap structures keep the native slowdown at 8-10%
instead of 12-18%: the prefix-count and LOCK checks run only after a
prefix, the breakpoint and RF work sits behind one branch in a cold
helper, and the #AC and watchpoint hooks sit behind one flag that is set
only while CR0.AM is on or a data breakpoint is armed. The rest is open as
X6 in [`PC486_PARITY.md`](PC486_PARITY.md).

## 47. Cache and bus timing

Item T1 from [`PC486_PARITY.md`](PC486_PARITY.md). Charging every access the
486's published L1-hit figure would mean nothing ever misses a cache or
waits on a bus. `cache486.h` times memory and I/O the way a 1993-94 DX2-66
board did. It holds no data: the chipset still answers
every read, and the model only decides how many core clocks it cost.

### 47.1 The board

T1 needs a board and a video card behind the machine. The board is a
VL-Bus board with a 256KB L2, the common spec for a DX2-66 gaming PC of the
time, built around an SiS 85C471-class
single-chip controller (SiS 85C471 data sheet, Preliminary V6.0, August
1994). The video card sits on the VL-Bus.

### 47.2 What it models

- **L1** (Embedded Intel486 Processor Hardware Reference Manual,
  27302501): 8KB, 4-way, 16-byte lines, pseudo-LRU, write-through with no
  allocate on a write miss. A read miss fills the line with a 4-dword
  burst. With CR0.CD set the L1 still answers hits but never fills.
- **Write buffers** (same manual, "Write Buffers"): four entries, one
  write per clock. A write only waits when all four are still pending. A
  read miss can go ahead of buffered writes once, and only if they were
  all L1 hits; otherwise the buffer drains first. Port reads never go
  ahead, and an OUT waits for the buffer and its own cycle.
- **L2** (471 data sheet): 256KB direct-mapped write-back, 16-byte lines,
  2-2-2-2 burst reads and 2T writes, the 471's setting for 20ns SRAM at
  33 MHz. A write miss goes to DRAM and leaves the L2 alone, and a dirty
  line is written back before it's replaced. The 8-bit tag covers 64MB,
  so all 32MB of RAM is cacheable.
- **DRAM**: the 471's "Faster" setting for 33 MHz, 4-3-3-3 page-hit
  bursts and 3T writes. Leaving the open row adds its RAS precharge and
  RAS-to-CAS delay, 3T + 2T. The row is 4KB, the column count of 1Mx4
  DRAMs across a 32-bit bank.
- **ISA**: 8.33 MHz (bus clock / 4), 1 wait state for 16-bit and 4 for
  8-bit cycles, half a clock of command delay on I/O and two clocks of
  recovery between commands (471 "AT Bus State Machine"). Only the IDE
  data ports are 16-bit; a 16-bit access to an 8-bit device is two
  cycles. The published IN and OUT counts already hold one zero-wait bus
  cycle, so those 2 clocks come off.
- **VL-Bus video**: memory at A0000-BFFFF is never cached. A cycle takes
  2 bus clocks, the VL-Bus minimum, plus 1 wait state on a write and 3 on
  a read; the VGA's ports cost the same. Those wait states are an
  estimate for a period VLB card, not a cited figure. Writes go through
  the write buffer, so a `REP STOSD` to video memory streams at bus speed.
- **ROM**: the BIOS and video BIOS run shadowed in DRAM and cacheable, as
  a period BIOS sets up, so they cost what RAM costs.
- **INVD and WBINVD** empty the L1 and, through the flush special cycle,
  the L2.

All of this is in bus clocks, two core clocks each on the DX2.

### 47.3 A stand-in for the BIOS's cache setup

The 486 leaves RESET with CR0.CD and NW set. A period AMI or Award BIOS
clears them during POST; the Bochs BIOS stand-in never does, so CR0 stays
60000010h at the DOS prompt. Modelled faithfully, the machine would run
with its L1 off, about three times slower than a real DX2-66. So the board clears CD and NW whenever it releases the CPU from
reset, standing in for the BIOS's cache setup. That's a labelled
departure: POST here runs cached from its first instruction, where a
real BIOS turns the cache on a few milliseconds in. The bare `Cpu` still
comes out of RESET with them set, and its tests still say so.

### 47.4 Where it hooks in

The CPU holds a pointer to the board's model, null on a bare CPU, so
`cpu80486_test` and `cpu80486_timing_test` still check the published
table and nothing else. `Machine` owns the model. Data accesses are
timed where they're translated, in `read8`, `write8` and `access_phys`.
Code is timed by its 16-byte line, once when the prefetch window fills
and once per instruction for the line it starts in. Port I/O is timed in
the bus helpers. Each instruction's stalls are added to its cost when it
completes.

Not modelled: the page-level PCD and PWT bits, DMA and bus-master traffic
on the bus, accesses that straddle a page (they take the byte-at-a-time
fallback), and descriptor-table and TSS reads. §48 covers all of these.

### 47.5 Checks

`cache486_test` has cases for the model, worked out from the figures above,
and two `Machine` cases that run real guest code (a
cold line fill costs exactly two 18-bus-clock DRAM bursts, an OUT to an
8-bit port its ISA cycle) and check the board turns the L1 on after
every kind of reset.

`hdd-boot-check` reaches `C:\>` at cycle 1,055,515,521, 3% later than
without the model; a DOS boot spends most of its time waiting rather than
missing the cache. Natively that boot costs about 3% more host time.
`pm-check` and `vbe-check` pass, and so does the Playwright suite.

## 48. The rest of the timing rules, and a real de-turbo

Item T2 from [`PC486_PARITY.md`](PC486_PARITY.md), plus the per-instruction
rules Intel lists next to its clock table. §47 timed the caches and buses
and adds the core's own pipeline penalties, the DMA controller and the page
bits, with a cited video card.

### 48.1 The video card

The VL-Bus card is a Cirrus CL-GD5428, the usual VLB card of the
time (CL-GD542X Technical Reference Manual, SR16 "Performance Tuning").
A memory write takes 3 bus clocks from ADS# to RDY#: SR16 needs the delay
to exceed 3 MCLK periods plus 2ns, and at the default 50.11 MHz MCLK
(SR1F) on a 33 MHz bus that's 3. Port I/O takes SR16's default of 2. The
manual doesn't print a read figure. A read waits for the 7-MCLK RAS
cycle, about 140ns, so it's 6 bus clocks with the address phase. That
one is derived, not cited.

### 48.2 The pipeline rules

The Embedded Intel486 Developer's Manual (27302101) lists the assumptions
behind its clock counts in 12.3.1. With the board's model attached, the
core charges the ones the table leaves out:

- **Rule 2, misaligned operands**: 3 clocks for a word at an odd address,
  a dword off a 4-byte boundary or a qword off an 8-byte boundary (the
  manual's own definition, under EFLAGS.AC). Charged once per operand,
  not again for the halves of a page-split read.
- **Rule 3, line fills**: a read miss stalls until its first dword
  arrives, not the whole burst. The rest of the line fills behind it, and
  a later read or write to that line waits for the burst to finish. The
  next miss waits for the bus.
- **Rule 4, AGI**: a base register written by the previous instruction
  costs a clock. The core compares each instruction's registers with
  the ones the instruction before started with, so a write of the same
  value doesn't count. PUSH and POP use ESP without an effective address,
  so the rule's exemption for them falls out.
- **Rule 8**: a displacement and an immediate in one instruction cost a
  clock (ALU, MOV, IMUL, shift, TEST, BT and SHLD/SHRD forms).
- **Rule 10, TLB misses**: a page walk costs 13, 21 or 28 bus clocks as
  neither, one or both entries need an A/D bit written back. The figures
  assume the entries aren't in the data cache, which the walk doesn't
  check here.

Rule 5's index-register clock is charged for base, index and displacement
together (Quantasm's 486 table legend). Rule 6 is covered by the code-line
timing: a jump target that runs into the next line pays for that line on the next
instruction.

### 48.3 Page bits, DMA, and the paths §47 skipped

- **PCD**: a page whose directory or table entry has PCD set never fills
  the L1. The 471 has no PCD or PWT inputs, so the L2 doesn't see either
  bit, and PWT changes nothing on a write-through L1.
- **DMA**: each 8237 transfer holds the bus for 6 DMA clocks at half the
  ISA clock, 48 bus clocks: 5 for the transfer (800KB/s at 4 MHz,
  National Instruments AN-011, "ISA (PC AT) Extensions to DMA
  Implementation") and the S0 clock a single-mode transfer spends
  requesting HOLD again. A transfer into memory invalidates the L1 line,
  since the 471 drives EADS# in DMA cycles, and the 471 writes its own L2
  copy on a hit (471 data sheet, "Cache Update Policy"). The floppy's
  sector still lands as one block copy once its real-time wait is up, so
  its 512 holds land together instead of one every 16us. This machine
  has no bus masters: IDE and the CD-ROM are PIO.
- **Page-split writes** are timed as one bus write per run of
  contiguous bytes, and **descriptor-table and TSS** reads and writes go
  through the same model as any other access.

### 48.4 Turbo off

On a SiS 85C471 board, Turbo off doesn't touch the CPU clock. The
chipset asserts HOLD for 4us of every 12us (register 58h bit 4, reset
default 4us; 8us is the other choice), so code running from the L1 keeps
going and anything that needs the bus waits. (Other boards of the period
halve the clock to 33 MHz, but not this one.) `Machine::set_turbo` sets the
hold and leaves the clock at 66 MHz. The front panel's readout shows 33, as
a tower's jumpers would set it. The Turbo button has no tooltip; the panel
has no explainers, like a real tower.

### 48.5 What's left

- A snoop that contends with the core's own cache access costs 1 bus
  clock (rule 9). It isn't charged.
- AGI misses a register rewritten with the value it already had.
- An instruction that runs into the next code line and then jumps away
  never pays for that second line.
- The 471's refresh is taken as hidden refresh (register 58h bit 5 set by
  BIOS setup), so it never holds the CPU.

### 48.6 Checks

Native cases cover the cache model's first-dword stalls, a fill
still in progress, the de-turbo hold and DMA hold and snoop; a `Machine`
case for AGI, misalignment and rule 8 in real guest code; paging cases for
the three walk costs and PCD; and a chipset case where a floppy transfer
invalidates the L1 line and holds the bus for 512 x 48 bus clocks.

`hdd-boot-check` reaches `C:\>` at cycle 1,059,517,619, 0.4% after §47's
figure. Natively that boot runs at 157 guest MHz on the host, against 158
for §47. `mem_timing` is forced inline, since it stops being inlined once
it has the PCD check, which costs about 5% on a memory-heavy loop.

## 49. The drawn keyboard replaces the key panels

The drawn keyboard stands in for the Function & extended keys panel, the
Ctrl+Alt+Del button and the Key Mapper table (§25). Under the monitor and its
sound/mouse bar, is a drawing of the IBM Enhanced Keyboard, the 101-key US
layout a 1993 486 shipped with. It's shared with ibmpc-at
(`shared/pc-keyboard.js`, IBM_PCAT_REVIEW.md §57). The bar's
Keyboard box is off by default on a desktop, on by default on a touch
screen with no mouse, and remembered once toggled
(`retro8080.pc486.keyboardOpen`).

- **Scale.** Keys are placed on a 1u pitch: 2u Backspace, 2.25u Enter and
  left Shift, 2.75u right Shift, 7u space bar with the blank spaces either
  side of it, 2u-tall keypad + and Enter, a 2u keypad 0, and the half-unit
  gaps between the function groups and the three blocks.
- **Same input path.** A click sends the key's Set 1 make on press and break
  on release through `sendKey()`, so it goes through the same scancode
  queue and 8042 as the real keyboard. Holding a drawn key repeats on the
  8042's own typematic clock.
- **Modifiers latch.** A click can't hold two keys, so Shift, Ctrl and Alt
  latch (make sent at once) until the next key's release, or until clicked
  again. Ctrl, Alt, Delete reboots with the real scancodes, with no
  `machine.reset()`: `keyboard.spec.ts`'s warm-reboot test passes on the
  clicked combo alone.
- **Lock lights.** `i8042` keeps the 0xED argument (`leds()`, cleared
  by 0xFF), and the Num Lock, Caps Lock and Scroll Lock lamps above the
  keypad follow it, so they light when the BIOS says so, not when the page
  guesses.
- **Mapping lives on the board.** "Map a key", then press one of your own
  keys (or click it), then click what it should send. Latch Shift, Ctrl or
  Alt first for a combination; Done commits a modifier on its own. A mapped
  key wears a badge naming its target; clicking it twice in map mode clears
  it. The WASD and Left Shift → Ctrl presets, Disable All, Clear All and the
  bezel KEYS button are on the page. Mappings apply to the visitor's own keys and to
  clicks on the drawing alike. A clicked key whose mapping is all
  modifiers (Left Shift → Ctrl) latches like a modifier.

Legend style follows the Model M: shifted character above unshifted, symbol
Backspace and Tab, a broken bar on the backslash key, darker caps for
modifiers, function and navigation keys. `pckeyboard.spec.ts` covers the
show/hide state, typing, latching, the Caps Lock lamp, map mode, and
mapped clicks.

## 50. No preset hard disks

The machine ships no third-party DOS games. C: is the factory FreeDOS image,
the Disk library holds only free software (FreeDOS packages, DOS Navigator,
Wing, Bolitaire), and there is no `?disk=` preset C:. Visitors bring their
own floppy, CD-ROM and hard disk images.

## 51. 8237 terminal count masks a single-cycle channel

The 8237A-5 data sheet ("Mask Register") sets a channel's mask bit when it
reaches terminal count without autoinitialize. `Dma8237::advance` masks the
channel at TC, so `Chipset::service_sb_dma` cannot feed a DSP block longer
than the DMA count past the end of the buffer.

Tyrian 2.1 with FM music and Sound Blaster effects both on depends on it.
Its timer handler restarts a single-cycle block every tick, with DMA count
0x179 and DSP length 0x17F. It then reads channel 1's address register as
the play position and computes `0x17F - (addr & 0x1FF)`. A 6-byte overrun
makes that negative, Turbo Pascal raises runtime error 201 inside the IRQ0
handler, IRQ0 is never EOI'd, and the game sits on a black palette after
the Epic logo. With the mask bit set at TC, the address stops at the end of
the block, as on real hardware.
`Dma8237Test.TerminalCountMasksTheChannelUnlessAutoinitialize` covers it.

The OCW2 decode in `Pic8259::out_impl` treats every non-EOI command
(0x40 no-op, 0xC0+n set priority, the rotate forms) as an EOI, since it
doesn't check the EOI bit (bit 5). This is an open simplification, and
nothing seen here depends on it.

## 52. Port 61h refresh toggle follows PIT channel 1

Port 61h bit 4 toggles with each DRAM refresh request, which PIT channel 1
raises every ~15 µs (IBM PC/AT Technical Reference, port 61h and System
Timers). Toggling it once per `Chipset::service()` pass would flip it on
nearly every read of a loop polling `IN AL,61h`, since any port read reopens
the service gate, and refresh-calibrated delay loops would run far short.

`Pit8253::tick` reports channel 1's rising edges as ibmpc-at's does
(IBM_PCAT_REVIEW.md §46.3), and the chipset toggles bit 4 on each one.
`BIOS-bochs-legacy` never programs channel 1, so `Machine::start_refresh()`
writes IBM's POST bytes (`OUT 43h,54h`, `OUT 41h,12h`) at power-on and on
reset, the same stand-in ibmpc-at uses. The 471's refresh stays hidden
(§48.5), so no bus clocks are stolen.

`ChipsetTest.RefreshToggleFollowsChannelOneNotPortReads` polls port 61h
every 40 cycles at 66 MHz across 100 refresh periods and expects 100 flips.
A per-pass toggle fails it, as it does the "no refresh until channel 1 is
programmed" check.
`ChipsetTest.RefreshToggleCountsEveryRequestInOneServicePass` lands two
requests in one service pass and expects bit 4 unchanged, then three and
expects it flipped.

## 53. A20 is open at power-on, and port 92h has its own latch

A20 is open at power-on. The 8042 writes CFh to its output port (P2) at reset and again in
the AAh self-test, so A20GATE comes up high and the gate is open until POST
closes it (IBM AT KBC ROM disassembly, pcjs.org `8042_INTERN.TXT`).
486-era KBC firmware clones
that interface. A reset value of 00h would also hold bit 0 (CPU reset) low
and make command D0h read back 00h.

Port 92h is not the KBC's A20 bit. On a real board it is a
separate register whose ALT_A20 bit is ORed with A20GATE: writing it high
opens A20 "regardless of the state of A20GATE", and A20 is masked only when
both are low. It resets to 0, reads back its own bits, and bit 0 pulses
ALT_RST only on a write of 1 after it was written back to 0 (Intel 82378
SIO data sheet, 290571-001, Port 92 Register and ALT_A20; PIIX4 290562-001
gives the same truth table). An ALT_RST resets the CPU, not the register.

`i8042.cpp` resets the output port to CFh, AAh restores it, and
`port92_` is its own latch. What a guest sees after POST does not depend on it, since the BIOS sets
the gate itself: the native FreeDOS boot reaches `C:\>`.

Tests: `OutputPortComesOutOfResetAtCfhWithA20Open`,
`SelfTestPutsTheOutputPortBackToCfh`, `FastA20PortIsOredWithTheKbcGate`,
`FastResetNeedsBitZeroWrittenBackToZeroFirst`, `ResetClearsPort92`,
`ChipsetTest.A20IsOpenAtPowerOn`,
`ChipsetTest.ResetReopensAClosedGateAndRetargetsResolvedPages`, and
`MachineTest.AFastResetThroughPort92KeepsTheA20Gate`. Tests that need a
closed gate close it with D1h, DDh, as POST does.

## 54. Only the gzipped hard disks ship

GitHub documents a 1 GB limit for a Pages site, and a raw C: image is
mostly zeros (a fresh FAT16 drive gzips to 1-7 MB). Pages gzips on the
wire anyway, so only the storage counts against the limit, and only the
gzipped image ships.

`app.js` fetches `disks/freedos-hdd.img.gz` and decompresses it with `DecompressionStream("gzip")`. The
`.gz` comes back as plain `application/gzip`, with no `Content-Encoding`,
on Pages and on both dev servers, so nothing decompresses it twice. The
dev servers map a raw request to its `.gz` sibling only when the raw file
exists, so a request for an image that wasn't staged 404s as it does on
Pages.
`make stage` drops every `*-hdd.img`. The raw images stay in `web/disks/`
locally, because the `.gz` and the legacy-disk test fixtures are built from
them. The staged site is about 520 MB with a CD image, of which the 420 MB
FreeDOS CD is most, and it doesn't compress (xz -9 gets it to 372 MB; §55
removes it).

The factory fingerprint is the `.gz`'s ETag and length, so a visitor's
untouched saved C: is replaced once by the same FreeDOS. A changed C: is
kept, as for any new factory image (§28).

Tests: `hdd.spec.ts` "the factory C: is fetched as freedos-hdd.img.gz and
decompressed in the page" fails if any `-hdd.img` is requested.
`check_site.py` resolves literal `disks/`, `roms/`, `tapes/`,
`examples/` and `assets/` paths in staged JS, so a page that still names a
raw image fails `make site`, and it fails any staged site over 1 GB.

## 55. The FreeDOS CD does not ship

The FreeDOS 1.3 LiveCD would be the Disk library's one CD entry, 420 MB of a
520 MB site, and the only file big enough to matter against Pages' soft
100 GB/month bandwidth (about 240 loads). It doesn't compress. C: boots
FreeDOS without it, so it is not in `library.json` or the web build (there
is no `cdrom-image` target). The ISO is still fetched and
verified by `disks/fetch-freedos-cd.sh`, because the C: image install
(§5) and the library floppies are built from it.

The CD-ROM drive is unchanged and starts empty. A visitor inserts their own
`.iso` or `.cue`/`.bin`. The Disk library is floppies only:
`shared/disk-library.js` has no `"media": "cd"` handling and pc486 offers
it drive A: alone.

Tests: `cdrom.spec.ts` "boots with the drive empty and fetches no disc"
fails on any `.iso` request. `library.spec.ts` checks every library entry
is served as a FAT12 floppy of its format, so a CD entry fails it. There is
no on-demand LiveCD test or `loadFreedosCdrom` helper.

## 56. One floppy controller for both PCs

`fdc765` lives in `shared/pc/`, using ibmpc-at's version: FORMAT TRACK
fills the sectors its IDs name after one revolution (PC486_PARITY.md S2), 0x3F7 latches the data rate, RESET drops INT and
DRQ, and DOR bit 3 gates both INT and DRQ (WD1003-WA2 OEM manual,
FDMAEN). pc486's chipset gates DRQ on it too; the chipset tests write DOR `1Ch`,
as a BIOS does.

What differs between the machines is the drive, a `pc::DriveType`
passed to the constructor with the CPU clock: sectors per track on HD
media (15 or 18), DD cylinders (40 or 80), DD step time, spindle RPM
(one FORMAT revolution: 360 on the AT's 1.2MB drive, 300 on 360KB and
3.5"), and the 3.5" disk-change rule (TEAC FD-235HF: eject asserts
DSKCHG, a step only clears it with a disk in). pc486 keeps its inline
`tick()` and `set_cpu_hz()` for Turbo, which scales the DD step time
too. The tests live in `shared/pc/tests/fdc765_test.cpp`, with a 3.5"
fixture for the 486's drive, and both machines run them.


## 57. An empty drive is not ready

The shared floppy controller holds READ DATA, WRITE DATA, READ ID and FORMAT
TRACK in the execution phase when the drive has no disk, since a drive
with no disk gives no INDEX pulses (TEAC FD-235HF spec 8.3.9) and the
uPD765 ends those commands only on the index hole. The BIOS times out and
DOS reports the drive not ready. Details are in IBM_PCAT_REVIEW.md §60.

## 58. The floppy controller's media checks stay off here

The shared `fdc765` reads only what a real drive would: media at its
own data rate, IDs from the track under the head, SPECIFY's step rate, and
MA, ND or WC after two index pulses otherwise (IBM_PCAT_REVIEW.md §69).
The AT's patched BIOS seeks and sets the rate. This machine's prebuilt
BIOS-bochs-legacy never seeks, sends SPECIFY or writes 3F7h, so its
chipset sets `lenient_media`: reads land at the command's cylinder,
whatever the head and rate, and seeks use the DriveType's fixed step
time. That's a departure from the real controller until this BIOS gets
the same patches.
