# IBM PC/AT (5170-339): design notes and sources

This is the citation trail for the IBM PC/AT (5170) emulator: the sources
behind each behavior, and the design notes that go with them. Open gaps
against real hardware are in [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md),
which cites these section numbers.

## 0. Status

An 8 MHz 80286 (`machine.h` `kCpuHz`) that boots FreeDOS 1.3 to a live
`C:\>` in the browser, in real mode and in 286 protected mode with extended
memory (§63). The page runs at the real clock. The labelled test-only CPU
multiplier is `?test=1&fast=1` (§36, §66), and a bare page load stays real
speed. System firmware is the pinned `BIOS-bochs-legacy` stand-in, built
from source with patches (§68.1). The video BIOS is the from-scratch EGA
stand-in `egabios` (§48.1), not VGABIOS-lgpl (that stays on the 486). COM1
serial mouse (opt-in) and LPT1 print capture are §67. Native GoogleTest
and Playwright suites cover the machine (§66). The open backlog is in
`IBM_PCAT_PARITY.md`.

## 1. Scope: real mode and 286 protected mode

The core runs real mode and 286 protected mode (§63). PC-DOS 3.30 and
FreeDOS stay in real mode; extended memory, XMS drivers and the BIOS block
move enter protected mode. LOADALL / STOREALL are not implemented (C14).

The 80286 keeps a segment's cached limit and rights on a real-mode load,
so a stale limit survives a return to real mode until RESET or a reload
from protected mode. The descriptor caches (§63.1) model that: real-mode
loads set only the base.

## 2. Real-mode-legal 80286-over-8086 additions implemented

`PUSHA`/`POPA` (0x60/0x61), `BOUND` (0x62), `PUSH imm8/imm16` (0x6A/0x68),
`INS`/`OUTS` (0x6C-0x6F), three-operand `IMUL reg,r/m,imm` (0x69/0x6B),
shift/rotate-by-immediate (0xC0/0xC1, alongside the 8086's by-1/by-CL forms
at 0xD0-0xD3), `ENTER`/`LEAVE` (0xC8/0xC9). Source: Intel iAPX 286
Programmer's Reference Manual (1987), "Instruction Set Differences from the
8086."

**Quirks of the silicon** (documented CPU-generation behavior):
- `PUSH SP` pushes the value SP had *before* the push on the 286; the 8086
  and 80186 push the decremented value. `push_reg()` in `cpu80286.cpp`,
  tested in `PushSpPushesValueFromBeforeThePush`. Software of the era used
  exactly this instruction to tell an 8086 from a 286 (§44.1).
- Shift/rotate counts are masked mod 32 on the 286; the 8086 used the raw
  unmasked count (so a shift by 200 took 200 cycles on real 8086 silicon).
  Tested in `ShiftCountMaskedMod32`.
- `INC`/`DEC` never touch `CF`. True on the whole 8086 lineage, so an
  `ADD`/`SUB` helper can't be reused for them unchanged. Tested explicitly.
- `OF` after a multi-bit shift/rotate (count != 1) is left **undefined** by
  Intel's own documentation for this family; the core simply leaves the bit
  untouched rather than guessing, matching the documented contract.
- `AF` after `AND`/`OR`/`XOR` is likewise architecturally undefined and left
  untouched, for the same reason.

## 3. Known simplifications (documented, not silent)

- **RESET vector**: the first fetch is the genuine `0xFFFFF0`, through
  CS's hidden FF0000h base and the board's ROM alias at the top of 16MB
  (§45.1).
- **`POPF`/`IRET` flag mask** (`0x0FD5`) restores CF/PF/AF/ZF/SF/TF/IF/DF/OF
  from the popped word and always zeroes bits 12-15. That's the real 286 in
  real mode, not a simplification (§44.2).
- **Cycle counts** (§40, §41, §46, §56): the core charges per-opcode
  appendix timings, a prefetch-queue refill tax, AT bus wait states,
  refresh, and EGA memory contention. The remaining TOPBENCH gap is E20 in
  `IBM_PCAT_PARITY.md`. T9 stays open only because it needs a real 5170
  to close (§62, §75).

## 4. Opcode coverage gaps

Undefined opcodes raise #UD (INT 6) everywhere, ROM included, and so do
`0x66` and the 386 `0x0F` opcodes. The BIOS is built for a 286 (§70).
Beyond that:
- `0x0F 0x05` (LOADALL) is a reported no-op (C14). Descriptor caches exist
  (§63.1); the open question is a primary-source layout of the 286 LOADALL
  table at 800h.
- `0xD8`-`0xDF` (x87 coprocessor ESC) decode the ModR/M byte (so instruction
  length stays correct for whatever follows) but perform no FPU operation,
  which is correct for this system's spec, since it has no 80287 installed.
  MSW.EM or TS set makes them #NM (INT 7).
- `0xF1` is a LOCK-alias prefix, as tested on a real 286 (§45.4).
  `F1 0F 04` (STOREALL) isn't modelled (C14).
- `AAM`/`AAD` are implemented for the standard base-10 encoding but not
  exhaustively tested against non-standard bases.

None of these are expected to matter for PC-DOS/FreeDOS or ordinary
DOS-era compiled code; flagged here so a gap is a documented decision, not
a silent one, per this repo's fidelity conventions (`CLAUDE.md`).

## 5. Chipset devices: scope and simplifications

- **PIC (8259A x2)**: the full 8259A is §50.1 and the cascade is §50.2.
  Cascading is wired in `chipset.cpp` the way real hardware does: the
  master's IR2 input tracks `slave.has_interrupt()`, and an INTA cycle
  whose highest-pending master line is IR2 forwards the actual vector
  fetch to the slave.
- **PIT (8254)**: each mode has its own waveform (§47.2). IRQ0's ~18.2 Hz
  edge rate is checked exactly via divisor 0/65536 in `Pit8253Test`.
- **i8042**: the A20 gate is open at reset and after the 0xAA self-test,
  and closed again before DOS boots (§45.2-§45.3). The CPU-reset trick
  (Output Port bit 0, or command 0xFE) is exposed as
  `reset_requested()`/`clear_reset_request()` for the embedding `Machine`
  to act on, matching this codebase's host-agnostic device convention (a
  device never reaches into the CPU object directly).
- **CMOS/RTC**: the clock advances and drives IRQ8 (§53.6).
- **DMA (8237 x2)**: the full channel model, including live memory to
  device transfers, is §50.3. Channel 2 carries the floppy controller (§7).
- **`Machine::run_cycles()`** polls and acknowledges the PIC only when the
  CPU's `IF` flag is set, mirroring real hardware (the CPU never begins an
  INTA cycle otherwise). Polling unconditionally would consume a pending
  IRQ (e.g. a timer tick) during a `cli`-protected section that never got
  serviced.

## 6. BIOS integration: the pinned Bochs BIOS stand-in

The system BIOS is `BIOS-bochs-legacy`, built from the pinned source with
patches (§68.1). The video BIOS is the from-scratch EGA stand-in
`roms/egabios/` (nasm, §48.1), not `VGABIOS-lgpl-latest.bin`. The 486 keeps
VGABIOS-lgpl and the prebuilt BIOS pin. `roms/fetch-bios.sh` fetches the
BIOS source, dev86 and the `fntcol16.zip` fonts egabios needs.

**Why the Bochs BIOS.** SeaBIOS needs an i386-targeting compiler (16-bit
real-mode codegen) plus either the `dev86` toolchain (`as86`/`ld86`/`bcc`)
or a sufficiently i386-aware GCC, and it publishes no prebuilt binaries.
The Bochs Project's firmware is committed as a prebuilt binary in its own
source tree (`bochs/bios/BIOS-bochs-legacy`, `bochs/bios/VGABIOS-lgpl/
VGABIOS-lgpl-latest.bin`), and `BIOS-bochs-legacy` is the plain
ISA/no-PCI/no-ACPI variant, a better match for an AT-class machine than
SeaBIOS's PCI-oriented default build target. The project prefers a pinned,
checksummed binary over a source build when the choice arises. Both
binaries are LGPL (`Copyright (C) 2001-2026 The Bochs Project`), pinned to
commit `bochs-emu/Bochs@ff17a0c2bbabccf96d33af4e08ba8061889b079d`, and
SHA-256-verified via `roms/fetch-bios.sh` (same pattern as
`fetch-basic.sh`). They are gitignored like every other fetched binary in
this repo. `BIOS-bochs-legacy` is exactly 64KB (F0000-FFFFF);
`VGABIOS-lgpl-latest.bin` is exactly 32KB (C0000-C7FFF).

**Labelling** (per CLAUDE.md's substitution rule): `VGABIOS-lgpl-latest.bin`
is a full VGA-compatible video BIOS, not an EGA-only one, and no maintained
EGA-only open BIOS project exists. This machine ships `egabios` instead.
The EGA device itself enforces the real 640x350x16 ceiling regardless of
what modes a video BIOS offers (§12).

**Native harness.** `bios_host.cpp`, the `dos_host`-style diagnostic host,
boots the real BIOS image against `chipset`/`machine` and reports POST
progress via port 0x80 codes and HLT state. A genuine AT keyboard sends
`0xAA` **unsolicited** after its own power-on self-test, independent of
the controller's own `0xAA` self-test command. The BIOS's keyboard-presence
check waits for exactly that byte, so `i8042.cpp`'s `reset()` delivers it
(`ResetDeliversUnsolicitedKeyboardBatByte`).

**Video BIOS dependency.** POST walks `0xC000-0xDFFF` for a `0x55 0xAA`
option-ROM signature and far-calls into what it finds. It executes
`XOR BX,BX ; INT 10h` (video services) at `F000:0679`, so INT 10h's vector
(physical `0x40`) must already point at the video BIOS, which installs it
in its init routine at `C000:0003`. Without a video BIOS the vector is
zero and POST runs into `0000:0000`. The BIOS is built for a 286 (§70.1),
so its code needs no 386 instructions: `0x66` and the `0x0F 0x80-0x8F`
`Jcc rel16` forms raise #UD (§4).

- **Drive detection**: the BIOS reads the fixed disk from the CMOS type
  table, not from ATA `IDENTIFY DEVICE` (§71).

## 7. FDC, EGA text mode and the BIOS boot path

Parts: `fdc765` (NEC uPD765 floppy controller, both drives, a real
command/parameter/execution/result state machine), live DMA channel-2
transfers in `dma8237` (address/count advance with the real page-boundary
wrap and N-1 terminal-count conventions), and `ega` (CRTC/Sequencer/
Graphics Controller/Attribute Controller register files, the color
text-mode VRAM window, the Input Status 1 retrace toggle, the AC
address/data flip-flop and its reset-on-reading-0x3DA quirk). `chipset`
orchestrates the FDC to DMA to memory handoff the same way it orchestrates
the PIC cascade. A chipset-level test proves a full floppy READ DATA
command moves real bytes from a mounted disk image into system RAM via
DMA. The video BIOS and the EGA renderer are §48 and §12.

**What the real BIOS needs of the hardware**, from `rombios.c` at the
pinned commit:

1. **Keyboard POST sequencing.** `keyboard_init()` (line ~1965 at the
   pinned commit) sends the keyboard an explicit `0xFF` RESET command and
   expects **two separate responses**: an immediate ACK (`0xFA`), then, on
   a second, later read, the Basic-Assurance-Test-passed byte (`0xAA`).
   That is separate from the power-on unsolicited BAT byte (§6). `i8042.cpp`
   special-cases command `0xFF` to queue `0xAA` right after its `0xFA` ACK
   is read (`ResetCommandGetsAckThenBatByteOnSeparateReads`). With it,
   `keyboard_init()` runs to completion and POST code reaches `0x77`, its
   own final checkpoint.
2. **Video BIOS init.** `rom_scan` finds the video BIOS and far-calls its
   init routine (`C000:0003`), which installs the INT 10h vector. The
   system BIOS then prints its banner through its `wrch()` helper and INT
   10h, character by character.
3. **Opcode coverage.** `Cpu::on_unimplemented` is a permanent diagnostic
   hook, called with the exact opcode and address whenever `step()` falls
   through to the unimplemented-opcode path. The prebuilt
   `BIOS-bochs-legacy` was built for a 386: it used the `0x66`
   operand-size prefix with 32-bit registers throughout, even in 16-bit
   real-mode code (5,818 hits in the first million steps alone), plus
   `Jcc rel16` and the 386's common `0x0F` instructions (`SETcc`,
   two-operand `IMUL`, `MOVZX`, `MOVSX`). That is why the BIOS is built
   for a 286 instead (§70.1, §44.3). The 32-bit *address* size (`0x67`,
   SIB bytes, "big real mode") is not implemented, since no BIOS code
   needs it.
4. **No bootable device.** With no floppy or hard disk mounted, POST
   completes and the BIOS prints its banner (`Bochs 3.0.devel BIOS - build:
   08/23/26`, the build date fixed at the pinned binary's) and then `FATAL:
   No bootable device.`, which is the correct response. The CPU, chipset,
   keyboard, PIT/PIC, DMA, FDC and EGA run the BIOS's complete POST
   sequence together, confirmed against `rombios.c` and the BIOS's own
   reconstructed output (every character it sends through `wrch()`).

## 8. FreeDOS floppy boot and factory CMOS

The boot floppy is the FreeDOS 1.3 official "Floppy Edition" 1.2MB
(`120m/`) image, fetched by `disks/fetch-freedos.sh` (pinned URL plus
SHA-256, same pattern as `roms/fetch-bios.sh`). Its size (1,228,800 bytes =
80x2x15x512) matches Drive A:'s geometry exactly, so no conversion is
needed. It is mounted with `chipset.fdc.mount(0, ...)`. The BIOS reads the
FDC, accepts the media, prints `Booting from Floppy...` and runs the boot
sector (§9, §10).

The BIOS tries A:, then C:, with no CMOS setting (§70.4).
`configure_factory_cmos()` writes neither 3Dh nor 3Fh, which are reserved
bytes on the AT.

`Machine::configure_factory_cmos()` (called once, automatically, by
`Machine`'s constructor) seeds the factory CMOS: base memory size, floppy
types (`0x10=0x21`: A: 1.2MB, B: 360KB), equipment byte (`0x14=0x41`),
extended-memory size at 17h/18h and 30h/31h (§63.4), the fixed disk type
(§71.1), and a computed checksum. A real AT's Setup diskette would have
written them once, at the factory. Covered by
`MachineTest.ConstructorSeedsFactoryCmosConfiguration`.

## 9. CMOS persistence and edge-triggered interrupts

**CMOS survives a reset.** CMOS is battery-backed, like `mem`, `rom_` and
mounted floppy media in this codebase, so `Chipset::reset()` does not call
`cmos.reset()` and does not clear `rom_`'s write-protection flags. A
`Machine` that is constructed and then reset keeps its factory
configuration (`MachineTest.FactoryCmosSurvivesAnExplicitResetCall`).

**IRQ6 is edge-triggered.** The BIOS's floppy IRQ6 handler (`int0Eh`) has
two paths. If the FDC's Main Status Register doesn't yet show a completed
result phase, it issues its own SENSE INTERRUPT STATUS and drains it. If a
result phase is already ready, it takes the shorter path: EOI the PIC, set
a BIOS-data-area completion flag, return, and leave the FDC's result bytes
for **foreground** code to drain later. `fdc.irq_pending()` therefore stays
true for a while, and every real ISA interrupt is edge-triggered, firing on
the transition rather than for as long as the condition holds. `chipset.cpp`
tracks the 0 to 1 transition (`fdc_irq_prev_`) and raises IRQ6 only on that
edge, as the 8259 wiring does. Raising it on every tick would re-enter the
ISR every ~42 cycles and starve the foreground code. Tested at the chipset
level (`ChipsetTest.Irq6IsEdgeTriggeredNotReRaisedWhileStillPending`):
service once without draining, verify no re-trigger.

With both in place the BIOS loads and executes the real boot sector at
`0000:7C00` and continues into FreeDOS kernel code (§10).

## 10. FreeDOS to a live DOS prompt

**Reading the machine.** A halted CPU is read from live RAM, not just ROM.
At the first boot-sector stage, `0000:7C00` holds the real boot sector, and
the halt sits inside an ordinary DOS idiom: `INT 2Fh AX=1680h` ("release
current virtual-machine time slice", the standard DOS-idle multiplex call)
followed by `HLT`, looping until a shared flag byte exceeds 1. The EGA text
VRAM, read directly, shows what DOS drew with `INT 10h` (the `wrch()`
character log only sees the system BIOS's own print helper): a complete
FreeDOS 1.3 installer screen with `Do you want to proceed [Y,N]?`, waiting
for a keystroke.

**IRQ1 is edge-triggered and tied to the output buffer.** `chipset.cpp`
raises IRQ1 only on the 0 to 1 edge of `kbc.irq1_pending()`. Real hardware
ties IRQ1 to the "output buffer full" condition, so `i8042::in(0x60)`
clears `irq1_pending_` as the byte is read. Anything waiting on the
*vectored* keyboard interrupt, rather than polling port 0x60, wakes on it
(`ChipsetTest.KeyboardIrq1ReachesThePic`).

**Input round trip.** Injecting a scancode (`chipset.kbc.inject_scancode()`)
travels scancode, IRQ1, PIC, CPU, the system BIOS's `INT 9h` handler, the
BIOS keyboard buffer, FreeDOS's own input routine, and a character drawn to
EGA VRAM by FreeDOS's own code. Injecting `N` echoes it
(`Do you want to proceed [Y,N]?N`), and `Enter` completes the prompt:

```
The installation of FreeDOS 1.3 has been aborted.
A:\>
```

The CPU is not halted at that prompt. It is COMMAND.COM's own input loop,
waiting for the next command. This is `BIOS-bochs-legacy`, the video BIOS
and the FreeDOS 1.3 kernel and FreeCOM, booted from a real 1.2MB floppy
image, with keyboard input and video output working end to end.

## 11. WD1003 hard disk controller

Sources: IBM PC/AT Technical Reference 6183355 (the DISK module) and the
WD1003-WA2 OEM manual. The BIOS finds the drive through the CMOS type table
(§71), and the controller is a WD1003, not an ATA device: `IDENTIFY DEVICE`
(`0xEC`) aborts like any unknown command, and Drive/Head bit 6 does not
select LBA (§71.2). The command set is §51.1.

**Geometry.** The drive is an ST-4038: 733 cylinders, 5 heads, 17 sectors
a track, no write precompensation, landing zone 733. `Machine::
configure_factory_cmos()` describes it as IBM fixed disk type 8 (§71.1).
The shipped image is a raw 31,900,160-byte file.

**Port wiring: 0x3F6, not 0x3F7.** An AT's hard disk control block uses
only 0x1F0-0x1F7 plus 0x3F6 (Device Control / Alternate Status). 0x3F7 is
the floppy controller's (DIR on read, CCR on write). `wd1003.h`'s header
comment records this wiring. `Fdc765::owns()` claims `0x3F0-0x3F5` and
`0x3F7` only, matching what `fdc765.cpp`'s `in()`/`out()` switch statements
implement, so 0x3F6 reaches the hard disk (`Fdc765Test.DoesNotOwnPortThreeF6`,
and `ChipsetTest.HddIdentifyAndReadGoesThroughAtomicSixteenBitBusPath`
end to end). `Chipset::io_in`/`io_out` check `fdc.owns(port)` before
`hdd.owns(port)`, so the narrow claim matters.

**Atomic 16-bit port I/O.** Almost every other AT I/O device decomposes a
16-bit access into two adjacent 8-bit ports. The data register at 0x1F0 is
atomically 16-bit, and 0x1F1 is a *different* register (Error/Features),
not "the high byte of 0x1F0". `cpu80286.h`'s `Bus` has `in16`/`out16`
alongside the 8-bit `in`/`out`, used by the four 16-bit port opcodes
(`IN AX,imm8`/`OUT imm8,AX`/`IN AX,DX`/`OUT DX,AX`) and `INSW`/`OUTSW`.
`Chipset::io_in16`/`io_out16` send port 0x1F0 straight to
`Wd1003::data_in16()`/`data_out16()` and compose two 8-bit accesses
everywhere else. Tested at the CPU level
(`Cpu80286Test.{In,Out}{Ax,Dx}*AtomicSixteenBitPath`,
`InswStoresAtomicSixteenBitPortReadAtEsDi`,
`OutswSendsAtomicSixteenBitPortWriteFromDsSi`).

**Pacing.** Transfers are paced to the platter, one sector slot at a time
(§52.4, §68.2). WRITE SECTORS asserts DRQ immediately, per real behavior
(no seek delay on the CPU side), and raises BSY once the CPU has supplied
the sector, pacing the *commit* to media rather than the CPU's data
delivery.

**Edge-triggered IRQ14**, on the slave PIC's line 6 (global IRQ14 = slave
IR6), by the same rule as IRQ6 and IRQ1 (§9, §10): raised only on the 0 to
1 transition of `hdd.irq_pending()`. Reading the primary Status register
(0x1F7) acknowledges and clears it. Reading the Alternate Status register
(0x3F6) does not, so a driver can poll status without disturbing a pending
interrupt (`ChipsetTest.Irq14IsEdgeTriggeredNotReRaisedWhilePending`).

**Drive 1 is permanently absent.** A command to drive 1 aborts with DRDY
clear (§71.2). The gate is on the command register (0x1F7) alone: Sector
Count, Sector Number, Cylinder Low/High and Features are latches on a
shared parallel bus that the present device absorbs regardless of the DEV
bit, with no per-device routing. The gate is keyed off the fixed fact of
which drive index is populated (`Wd1003::selected_drive_absent()`), not off
whether `mount()` has been called, since the register-protocol tests
exercise drive 0 without an image
(`Wd1003Test.AbsentSlaveDriveOnlyRefusesToExecuteCommands`).

**Disk change line (`fdc765.h`).** The Digital Input Register (0x3F7) bit
reports DSKCHG for whichever drive the DOR's select bits address.
`Drive::disk_changed` starts true (real hardware asserts it at power-on),
`mount()` re-asserts it, as a real media swap does, and it clears only once
a RECALIBRATE/SEEK command steps that drive afterward, as a real
controller re-latches the new media
(`Fdc765Test.DiskChangeLineSetByMountAndClearedBySeek`). The installer's
file-copy routine polls the bit to confirm the user swapped a diskette
before it trusts a re-read of the drive.

**The shipped C: image comes from the real installer.** No pre-built
733/5/17 raw HDD image exists, and no filesystem is hand-built. The image is
produced by driving the genuine FreeDOS 1.3 multi-floppy installer
(`FD13-FloppyEdition.zip`'s `BOOT` plus `DSK01`-`DSK06`) against this
emulator end to end, with screen-text matching and injected keystrokes
standing in for a person.

- `disks/build_freedos_hdd.cpp` is the tool. It has a fixed step list
  through the first floppy swap, then dynamic per-file and per-disk logic,
  the keypress nag below, and final-completion handling.
- `disks/fetch-freedos-install-set.sh` fetches and SHA-256-verifies
  `x86DSK01.img`-`x86DSK06.img` from the same official distribution point
  `fetch-freedos.sh` uses.
- `make hdd-image` fetches the BIOS and both floppy sets, builds the tool,
  runs it, and produces `disks/freedos-hdd.img`. It is not part of
  `check`/`test`, because the real installer takes real minutes.
- "Automatically partition drive C:" writes the MBR, then the installer
  reboots through the boot floppy back to its welcome screen with the new
  MBR persisted on the image.
- "Press a key" prompts: the installer flushes its keyboard queue just
  before it waits, so a burst sent the instant the prompt text appears can
  vanish. The tool nags with one keypress every ~0.25 s of emulated time.
- File-to-disk layout: past the first floppy the installer names files
  individually ("Insert diskette containing file A:\FREEDOS.NNN"). Each
  install floppy's FAT12 root directory gives DSK01=FREEDOS.001-019,
  DSK02=.020-039, DSK03=.040-059, DSK04=.060-079, DSK05=.080-099,
  DSK06=.100-114. The installer processes files in numeric order and asks
  for a new diskette only at a range boundary.
- Extraction: the installed disk is the drive's own in-memory
  `Wd1003::Drive::image`, not the buffer originally passed to `mount()`,
  which holds only the starting state.

The installer runs format, MBR, boot partition and all 114 split files
across 6 disks, and ends with its own "The installation of FreeDOS 1.3 has
completed" message. The image has a genuine MBR with the `55 AA` signature
and boots with no floppy mounted to a live `C:\>`: `FDCONFIG.SYS`/
`FDAUTO.BAT` process, FreeCOM reports real conventional-memory figures for
`SYSTEM` (the FreeDOS kernel) and `COMMAND` (FreeCOM itself), and the
welcome banner and prompt appear. Anyone building this project can
regenerate it from a clean checkout.

**CI caching:** `deploy-emulator.yml` and `nightly-full.yml` cache
`ibmpc-at/disks/freedos-hdd.img` and run `make -C ibmpc-at hdd-image` only
on a miss. See §66.

## 12. Planar EGA memory (write modes, read modes, latch)

`ega.h`/`ega.cpp` model the genuine 4-bitplane engine: 256KB VRAM stored
byte-interleaved as `vram[(plane_offset << 2) + plane]`, a 4-byte read
latch, the full Write Mode 0-3 / Read Mode 0-1 state machine, Set/Reset,
Enable Set/Reset, Data Rotate's rotate-count + ALU function, Map Mask,
Read Map Select, Bit Mask, Color Compare/Color Don't Care, and the Graphics
Controller's Memory Mapping field. That field picks which legacy window is
decoded: 128K@A0000, 64K@A0000, 32K@B0000 mono, or 32K@B8000 color. Real
hardware answers one at a time, never all three. A real EGA's odd/even
chaining reduces to a flat character/attribute byte pair for text mode, and
the planar engine is what graphics modes need.

**Source**: the memory-access algorithm (latch-on-every-read, the four
write modes, both read modes, the odd/even-vs-map-mask gating) follows the
IBM EGA Technical Reference. Register *values* (the standard mode-3 and
16-color-graphics-mode table used in `tests/ega_test.cpp`'s setup helpers)
are the IBM EGA/VGA-standard values every compatible BIOS reproduces. A
real video BIOS booted against the engine programs mode 3 to exactly what
this implementation expects.

**Out of scope: Chain Four** (VGA mode 13h's chained-pixel addressing).
`VGABIOS-lgpl-latest.bin` is a full VGA BIOS (§6) and offers mode 13h, but
Chain Four is VGA silicon, not EGA. A genuine 5170-339 with an EGA card has
no such mode, so the device does not implement it. The emulated hardware,
not whatever the substitute BIOS thinks it can offer, enforces the real EGA
ceiling (640x350x16, no chained or linear-framebuffer modes).

**Tests** (`ega_test.cpp`):

- `TextModeMemoryReadWriteRoundTrip` calls a `SetupTextMode80x25()` helper
  first, because a freshly reset card has no planes enabled and an all-zero
  Bit Mask. On real hardware a bare `mem_write` right after reset, with no
  mode-set, is a no-op.
- `MemoryMappingSelectsWhichLegacyWindowIsDecoded`: switching the Memory
  Mapping field changes which one window answers. The previous window's
  bytes are preserved but no longer reachable, not aliased. No register
  state makes A0000, B0000 and B8000 live at once.
- Six cases cover the write-mode, read-mode, Set/Reset, Data-Rotate,
  Map-Mask and Bit-Mask machinery and the odd/even-routed
  character-generator-plane-2 quirk individually.
- A native harness boots the real BIOS and video BIOS against the shipped
  `disks/freedos-hdd.img` to a live `C:\>` prompt with the expected text
  (memory-usage table, `FDCONFIG.SYS`/`FDAUTO.BAT` processing, the FreeDOS
  banner). Its screen reconstruction reads the CRT controller's own
  display-refresh path, character = plane 0 at `vram[(plane_offset<<2)+0]`,
  not the CPU's I/O path (`mem_read`), which depends on Read Map Select and
  would give a wrong answer if a BIOS font-load routine left it pointed
  elsewhere.

**`render_screen.cpp`** is a permanent diagnostic, in the same category as
`bios_host.cpp`. It boots the real BIOS plus optional HDD/floppy images and
renders the live EGA text-mode screen to a BMP. That shows whether the
*real* character generator and palette are correct, which the ASCII
`ScreenText()` reconstruction in `disks/build_freedos_hdd.cpp` (fine for
matching prompts) does not. It uses only public `Ega` state (`vram` for the
plane 0/1 text data and plane 2's character-generator bitmap, and the
`attr_palette()` accessor for the live Attribute Controller palette
registers, beside `cursor_offset()`/`start_offset()`) plus the 6-bit EGA
color decode cited in this section, with no separate font or color data of
its own. Run against `disks/freedos-hdd.img` it produces a legible,
correctly colored screenshot of the live `C:\>` boot: readable glyphs
confirm the plane-2 character-generator addressing, and the cyan filenames,
red CD-ROM warning and green FreeDOS banner confirm the default palette
register table and 6-bit color decode. It is not gated by `check`/`test`,
matching `bios_host.cpp`'s precedent (see the Makefile's comment on
`render_screen`).

## 13. PC speaker

`pcspeaker.h`/`.cpp` model the real speaker circuit as what it is in
hardware: a 2-input AND gate between Port 0x61 bit 1 ("Speaker Data
Enable") and PIT channel 2's output (already gated by Port 0x61 bit 0, see
`pit8253.h`/§5). That one AND gate is why two unrelated programming
techniques both work through the same two bits: standard tone generation
(gate the PIT on, leave data enable high, the speaker follows channel 2's
square wave) and "digitized"/direct-toggle playback (park the PIT, gate
off, forcing its output permanently high, and toggle data enable directly
under CPU control, e.g. Access Software's RealSound and many disk-based PC
speaker sample players).

`PcSpeaker` doesn't synthesize audio itself (the front end does, §14). It
records a continuous *edge trace*, `(cpu_cycle, level)` whenever the AND
gate's output actually changes. That is the representation a renderer
resamples into PCM, and the only one faithful to a real speaker's
continuously variable cone position rather than some sample rate the
native core has no business choosing. The trace is bounded to 65536 pending
edges (real hardware has no such limit; this only protects memory if a
consumer never drains, dropping the oldest transitions the way an unread
hardware FIFO would).

**PIT channel 2 gating** matters for the digitized-playback technique
specifically. In Mode 3, gate low freezes channel 2's counter and forces
`output` high immediately, regardless of phase, so a program parking the
PIT to drive the speaker directly starts from the clean high level real
hardware guarantees. Gate's rising edge reloads the counter, so ungating
restarts the square wave cleanly instead of resuming mid-phase. Both are
8253 Mode 3 behavior (see `pit8253.h`'s header), covered by
`Gate2LowForcesOutputHighEvenMidCycle` and
`Gate2RisingEdgeReloadsCounterInsteadOfResumingMidPhase`, both derived by
hand before implementing.

**Tests**: `pcspeaker_test.cpp`'s 6 cases exercise the AND gate, edge
deduplication, the digitized-playback relay case, reset, and the
overflow-drops-oldest behavior directly. A plain, silent `C:\>` boot never
changes Port 0x61 from 0x00, so the speaker produces zero edges on that
boot path, and the device logic is exercised by its own unit tests instead.

## 14. The front end (`web/`)

`ibmpc-at/web/` follows the per-machine shape (`wasm_machine.cpp` Embind
wrapper, `app.js`/`index.html`, own `Makefile`). Serving uses
`../../serve_nocache.py` (same as every other machine and `make preview`).
Themes and other identical chrome live in `shared/` (§17, §65). The
top-level `Makefile` has `wasm`/`serve` targets delegating to `web/`,
matching `altair8800`'s and `assembler6502`'s own top-level Makefiles.

**Scope.** Text-mode rendering (what a plain DOS session is), keyboard
input, the two floppy bays, a status-only HDD LED, and a muted-by-default
Web Audio speaker. Graphics modes are §15 and §16, from the CRTC-driven
`RenderScreen` entry point in `ega_render.h`.

**`wasm_machine.cpp`** is a thin `WasmMachine` wrapping `ibmpcat::Machine`,
exposing `loadRom`/`runCycles`/`renderFrame`/`injectScancode`/floppy
mount-eject-status/`mountHdd`/`hddBusy`/`speakerLevel`+`speakerEdges`.
`renderFrame` calls the same `ega_render.h` the native `render_screen.cpp`
diagnostic uses, one tested C++ implementation rather than a second copy of
the pixel-decode logic in JavaScript. `Wd1003::busy()` (`ST_BSY` or an
in-flight paced transfer, see `wd1003.h`) feeds the activity LED, covered
by `BusyReflectsAnInFlightTransfer`.

**Firmware and HDD image placement.** The deployed site only ever stages a
machine's `web/` directory (see CLAUDE.md), so `web/Makefile`'s
`roms`/`hdd-image` targets copy the native-core Makefile's fetched or
built output into `web/roms/` and `web/disks/`, and the page never fetches
`../roms/...` at runtime. `hdd-image`'s copy is gated on the parent's own
output file being missing (a real file-based Make dependency, not a phony
target re-invoked on every `make serve`). `bios`'s recipe is cheap and
idempotent (`fetch-bios.sh` checksums and skips what's already correct) and
safe to re-run unconditionally. `hdd-image`'s recipe is a multi-minute
installer run and must not fire by accident.

**Keyboard.** `app.js`'s `SET1` table maps `KeyboardEvent.code` directly to
IBM AT Set 1 scan codes (a plain number is one byte, a two-entry array is
an 0xE0-prefixed extended key: the arrow cluster, Insert/Delete/Home/End/
PageUp/PageDown, right Ctrl/Alt, numpad Enter/Divide). `i8042.h`'s
`inject_scancode()` is a verbatim Set-1 pass-through (see the file's own
header), so this table supplies exactly what a real AT keyboard's own
Set-2-to-Set-1 translation hands the host, and no separate translation
stage is modeled.

**Keyboard timing.** The keyboard holds its own bytes in a 16-byte FIFO
and sends each across the link as an 11-bit frame (§53.1), so a make code
followed immediately by its break code both reach the guest, in order. The
page puts a whole scan code sequence in at once (`injectScancodeSequence`,
§31) and the keyboard paces it. The installer tool,
`disks/build_freedos_hdd.cpp`'s `SendKey()`, runs make, real cycles, break,
real cycles.

**Floppy bays**: file input to mount, a per-drive motor LED polled each
frame via `floppyPresent`/`floppyMotorOn`, and an Eject that offers the
modified image back as a download via `floppyImage`/`floppyDirty` when the
session wrote to it.

**PC speaker, muted by default.** The `#speakerEnabled` checkbox starts
unchecked on every page load and is never restored from a saved
preference, so it stays off until the visitor opts in on every visit, not
just because browsers block audio autoplay until a user gesture. When
enabled, `pumpAudio()` drains `speakerEdges()` once per animation frame and
turns the real edge trace into samples for a persistent `AudioWorkletNode`
(§37), holding the last known level between edges via `speakerLevel()` for
frames with none. The worklet is registered from a Blob URL, not a
separate module file, and the page assumes no waveform of its own. The
plain `C:\>` boot path produces zero speaker edges (§13), so audible output
needs a program that touches Port 0x61.

**Test hook.** `window.__test` (`{ machine, sendKey, screenEl }`, active
under `?test=1`) follows `altair8800`/`assembler6502`'s `window.__test`
convention. The formal Playwright suite is §34 and §66.

**Speed.** This machine has no visitor-facing floppy/HDD load-speed picker.
The only sanctioned test override is the CPU multiplier `?test=1&fast=1`
(§36). A real-time boot to `C:\>` takes about 29 s at 8 MHz (§61).

## 15. CGA-compatible 4-color graphics mode rendering

**Detection.** `GR06` bit 0 (graphics, not alphanumeric) set and `GR05`
bits 5-6 (Shift Register field) = 1 is the EGA/VGA "CGA-compatibility"
320x200 4-color mode (what INT 10h mode 4/5 programs). `DetectScreenMode()`
reads those two registers, exactly what a real CRT controller consults,
not a BIOS-video-mode-number guess. A renderer that decodes graphics VRAM
as characters and attributes shows a field of colored blocks.

**Mechanism** (see `ega_render.h`'s file header for the full account). A
CGA-unaware program writes what it thinks is one flat 8000-byte bank (even
scanlines in the first 8K, odd in the second, 80 bytes/scanline, 4
pixels/byte). Odd/even plane chaining (§12) splits consecutive bytes
across planes 0 and 1 by address parity, so each CGA-style byte pair lands
at the same plane offset, one byte per plane. Real hardware's shift
registers read plane 0's byte as pixels 0-3 and plane 1's byte as pixels
4-7 of that pair, each 2-bit value indexing the *live Attribute Controller
palette* (registers 0-3), not a hardcoded CGA color table, the same way
text mode's colors work.

**Implementation.** `ega.h` has two small host/front-end accessors in the
same pattern as `attr_palette` and `cursor_disabled`:
`graphics_mode_active()` and `gc_shift_register_mode()`, reading the same
register arrays `in()`/`out()` use, with no new state. `ega_render.h`/
`.cpp` have `DetectScreenMode()`, `RenderCgaGraphics4Screen()`, a
`RenderedFrame{width,height,rgba}` struct, and `RenderScreen()`, the one
entry point `render_screen.cpp` and `wasm_machine.cpp` both call, so one
tested implementation decides what's on screen. An unrecognized graphics
mode renders as a plain black frame: Shift Register 2 is a VGA-only
variant no genuine EGA sets (`kUnsupportedGraphics`, §16), and value 0 is
native 16-color EGA (§16).

**Front end.** `wasm_machine.cpp`'s `renderFrame`/`renderWidth`/
`renderHeight` go through `RenderScreen`/`RenderedFrame`. Resolution varies
by mode, so width and height are read after each `renderFrame()` call.
`app.js` resizes the canvas element's own pixel buffer to match whenever
it changes and displays it at its own native aspect ratio rather than
stretching a lower-resolution mode into the text mode's box.

**Activity LEDs.** The floppy motor and HDD indicators show real activity.
Sampling `busy()`/`motor_on` once per animation frame, after that frame's
whole cycle budget, could miss a transfer that starts and finishes inside
one frame's ~133,000 cycles. `WasmMachine::runCycles()` therefore
sub-chunks (2000 cycles at a time, cheap, since the core already
interprets one instruction at a time) and latches activity along the way.
`hddBusy()`/`floppyMotorOn()` consume and clear that latch, the same
"pulse-stretched to since last read" convention
`altair8800/web/wasm_machine.cpp`'s `int_seen_`/`busActivityCounts()` use
for their once-a-frame-polled indicators.

**Tests.** `EgaRenderTest` has the mode-detect, CGA-decode and dispatch
cases. The Oregon Trail (MECC, 1990, a legitimately owned copy built into a
FAT12 floppy image with `mtools`, not in this repo) runs as `OREGON CGA`
through the browser file picker and shows its MECC splash screen and main
menu at 320x200, with the canvas resized and the floppy motor LED lit
during the load. (A headless-Chromium check needs the real Shift+Semicolon
key sequence for `:`, because Playwright's high-level `keyboard.type()`
doesn't reliably send one. A real keyboard reports Shift and the letter as
two independent, properly sequenced events, which this page's per-key-code
architecture handles.)

## 16. Native 16-color EGA graphics rendering, and the BIOS/hardware mismatch

**Mode 13h is a firmware/hardware mismatch.** `GR05` Shift Register = 2 with
`SEQ[4]` bit 3 (Chain-Four) set is VGA's 256-color mode 13h, which does not
exist on 1984 EGA silicon. A full VGA video BIOS (`VGABIOS-lgpl-latest.bin`)
offers it, and a program that auto-detects (The Oregon Trail's plain
`OREGON`) picks VGA's best mode instead of EGA's. A real EGA card wired to
that BIOS would have the same problem: the BIOS offers a mode the card
cannot display. The renderer treats Shift Register 2 as
`kUnsupportedGraphics` (VGA-only) and draws a black frame. This machine
ships the EGA stand-in BIOS instead (§48.1), and `OREGON CGA`, which forces
the CGA-compatible path (§15), is the period-correct choice per the game's
own README.

**Native 16-color EGA graphics** (`GR05` Shift Register = 0, real modes
0x0D/0x0E/0x10, 320x200/640x200/640x350). The renderer derives resolution
from the CRTC registers rather than a mode table. The machine's own BIOS,
invoked directly (`cpu.ax = 0x0010; cpu.interrupt(0x10);`, a real INT 10h
injection that pushes flags/cs/ip and jumps through the real-mode IVT),
sets mode 0x10, and the registers it programs read back as Horizontal
Display End = 79, so (79+1)*8 = 640, and Vertical Display End = 349 (0x5D
plus the Overflow register's bit 1), so 349+1 = 350. That is real mode
0x10's resolution. `ega.h` has `crtc_horizontal_display_end()` and
`crtc_vertical_display_end()` accessors (same host/front-end convenience
pattern, reading the existing register arrays). `ega_render.h`/`.cpp` have
`RenderEgaNative16Screen()` and `ScreenMode::kEgaGraphics16`. Odd/even
chaining is disabled in this mode, so addressing is linear across all 4
planes. The CRT controller reads the same plane offset from all 4 planes
every cycle, since planar VRAM always answers on all 4 planes at once, and
combines each pixel's 4 bits (plane 0 = LSB through plane 3 = MSB of the
color index, the standard EGA/VGA convention) into the live Attribute
Controller palette, same as every other mode.

**Tests.** A native probe draws a rectangle through the real Write Mode 2
path (the standard technique for plotting a solid color: the CPU byte's
bits select per plane, not a raw `vram` poke) after invoking real mode
0x10. The frame shows a correctly positioned, correctly colored rectangle
at the expected pixel coordinates and palette color, at 640x350.
`EgaRenderTest` has the matching cases. The WASM front end needs no mode
awareness: `wasm_machine.cpp`'s `renderFrame`/`renderWidth`/`renderHeight`
dispatch through `RenderScreen()`, and `app.js`'s canvas resize logic is
generic (§15).

**Preview server.** `make preview`, `preview-bg`, `preview-install` and
each machine's own `make serve` use `serve_nocache.py`
(`Cache-Control: no-store`). A bare `python3 -m http.server` sends no
cache headers, which makes a stale wasm module or `app.js` after
re-staging indistinguishable from a regression.

## 17. Page themes, screen size, front panel, and power/reset

**Themes.** The page's shared theme tokens (colors, fonts, shadows) match
`altair8800/web/index.html`'s values, in the `:root`/`:root[data-theme=...]`
custom-property blocks. The site has five themes (`win`, `winxp`, `web94`,
`subtle`, `aurora` / Atmosphere) via shared `theme-picker.js` (§65).
`.page { max-width: var(--page-max); }` is token-driven, with
`:root[data-theme="subtle"] { --page-max: min(1600px, 95vw); }`. The
theme block's CSS comments must close with `*/`. An unclosed one swallows
every rule up to the next `*/` and drops whole theme blocks silently, so a
wrong computed style is worth checking against
`document.styleSheets[...].cssRules.length` and `cssRules[0]` before
assuming the one property under inspection is the whole story.

**Screen size.** The base CSS width is 860px (in the spirit of
`altair8800`'s 792px-wide CRT), still `max-width:100%`, and whatever the
current mode's resolution is (640x350 text, 320x200 CGA graphics, and so
on) upscaled with `image-rendering:pixelated`. No real resolution
changes.

**Front panel.** One `.at-case` fascia (beige AT-plastic gradient, inset
and outset shadows for a moulded-plastic look) holds both floppy bays (each
a slot plus door-lip graphic, with the loaded diskette marked by a
`.loaded` state, the same idea as `altair8800`'s `.dcdd-slot`, adapted
from an 8-inch to a 5.25-inch drive's proportions) and the HDD activity
LED together, as they share one real bezel. `setBayLoaded()`/
`setBayEmpty()` in `app.js` set that state. The `data-role`/`data-drive`
hooks (`.at-bay[data-drive]`, `[data-role=...]`, `#hddLed`) are what
`app.js` and the tests select on. Proportions are §59.

**Power switch (default off).** A rocker switch, a visually hidden
`<input type="checkbox">` under a styled `<span>`. A user clicks the label
and it works via native label-to-input association, so Playwright clicks
the label, not `.check()` on the hidden input. The semantics follow real AT
hardware:

- Powering off discards the whole `Machine` instance (RAM is gone the
  instant power is cut) and blanks the canvas.
- Powering on builds a fresh `Machine`, reloads the already-fetched
  firmware bytes (fetched once, up front, regardless of power state, as
  the web delivery mechanism), and remounts whatever floppy images are
  still "in the drive" (tracked in JS, surviving the discarded `Machine`).
  A real diskette stays seated whether or not the computer has power.
- A reset pulses the CPU's RESET line without touching power, so RAM and
  any seated diskette are untouched: `Machine::reset()`. The page has no
  reset button (§18); Ctrl+Alt+Del warm-boots (§57).
- Floppy insert and eject work whether the machine is powered on or off,
  as on a mechanical drive. Inserting while off and then powering on
  remounts the diskette.

At load the power switch is unchecked, power is off, and the modern theme's
page width follows the `min(1600px, 95vw)` formula while Windows 95 and
web94 stay at the fixed 920px cap.

## 18. Front panel details of the genuine 5170 bezel

Details of the case fascia against a real 5170 reference photo. Proportions
are §59.

**No front-panel reset button.** The genuine IBM 5170 has no dedicated
reset control. A round reset button is a later clone-era convention, so
the panel has none. `Machine::reset()` stays as a correct piece of machine
semantics (Ctrl+Alt+Del reaches the BIOS warm boot, §57), and `app.js` has
no `resetButton`.

**No boxed "hard disk" panel.** The real machine's HDD activity light is a
bare LED on the bezel, not a labelled sub-panel. `#hddLed` is another dot
in the same `.at-indicators` row as the power LED, unlabelled like its
real counterpart. The floppy activity LEDs and the power LED carry no
`.led-caption` spans. The switch keeps its own "POWER" text, because that
is a control a user operates, not a light being explained.

**Stacked bays.** The two 5.25" half-height floppy bays sit one above the
other: `.at-drives` is `flex-direction: column`, and each `.at-bay` is a
full-width horizontal strip (drive letter, slot, latch, activity LED on
one line; filename label plus Insert/Eject below).

**Both drives are 5.25".** Per `fdc765.h`'s geometry, Drive A is 1.2MB (80
cyl/2 head/15 sec, 360 RPM) and Drive B is 360KB (40 cyl/2 head/9 sec, 300
RPM). A real 5170 of this vintage shipped this way, and 3.5" drives didn't
reach the PC platform until the AT-compatible clones and the PS/2 era.
Drive B therefore has no 3.5" face. Both bays get the same slot and latch
treatment, distinguished by their capacity label, suffixed `, 5.25″` on
both via `driveDefaultLabel()` in `app.js`.

**Vents and latches.** A decorative `.at-vents` slat block mirrors the
reference photo's proportions, and a small `.at-latch` bar per bay stands
for the real lever-style latch of this drive era. A loaded diskette shows
as a background shift on `.at-slot` when `.loaded`, and the filename sits
in the `.drive-label` text beneath each bay.

**Screen focus.** `#screen:focus` has `outline: none`. The screen has its
own black bezel, and the "Click to focus" hint (§42) tells a visitor when
it lacks focus.

**Power switch position.** The genuine 5170's power switch lived on the
side or rear of the case, not the front bezel. The switch stays on the
on-screen front panel as a labelled web-UI concession, since there is no
"side of the browser window" to put it on. It makes no claim about where
the real switch sat.

At load `#screen:focus` computes `outlineStyle === 'none'` and
`document.getElementById('resetButton')` is `null`.

## 19. Page background and display sizing

**Mid-1990s Web background.** `:root[data-theme="web94"] body { background:
... url(...) repeat; }`, the tiled "blue marble" desktop texture every
other machine's `web94` theme uses, is a `body` rule keyed off the theme
selector, outside the `:root`/`:root[data-theme=...]` token blocks. It is
copied from `altair8800/web/index.html` verbatim, with its identical base64
JPEG asset, because the token sync (§17) doesn't carry it. The page's
`web94` `body` computed `background-image` is non-empty.

**Display and panel sizing.**

- The page has no "Click the screen, then type" paragraph. The
  `#bootStatus` line is absent (§24 covers where its state lives).
- In the modern (`subtle`) theme the display fills the page width:
  `.monitor` and `#screen` are both `width: 100%`, `#screen` capped at
  `max-width: 1400px`, and `height: auto` plus the `aspect-ratio` `app.js`
  keeps in sync with the current video mode does the rest. It is a bigger
  upscale of whatever the real resolution is, never a change to it. Other
  themes keep the fixed 860px CRT size. At a 1800px viewport `#screen`
  measures 1400x765.6 (ratio 1.829, matching 640/350).
- `.at-case` has a fixed width in modern (§20). A real case fascia doesn't
  get physically wider because the browser window did.

## 20. Front panel width per theme

The front panel's width policy matches the CRT's. It stretches with `.page`
in the Windows 95 / Mid-1990s Web / dark-default themes (all fixed around
920px, so nothing runs away) and stays capped in modern's much wider
layout.

- `.at-case` has `width: 100%`, stretching with `.panel`/`.page` like every
  other control on the page in every non-modern theme.
- `:root[data-theme="subtle"] .at-case { max-width: 640px; margin: 0 auto }`
  keeps it from following modern's wide `--page-max`, as the CRT
  deliberately does. At a 1800px viewport in the modern theme, `.at-case`
  holds at exactly 640px.
- The drive-bay column is narrow (~190-280px depending on theme and
  viewport), too tight for the capacity label and both Insert/Eject
  buttons on one line. `.at-bay-ctl` is `flex-wrap: wrap` with
  `.drive-label { flex: 1 1 100% }`, so the label gets its own full line
  and the buttons wrap below it. Both labels (`empty (1.2MB, 5.25″)` /
  `empty (360KB, 5.25″)`) render in full.

The grille and badge cluster is the dominant width of the bezel, roughly
3/4 of it, with the two stacked floppy bays in a narrow column at the
right, as on a genuine 5170. The drawn proportions are §59.

## 21. Front panel: lights and switch above the grille, wider drives

- **Indicator lights and the power switch sit above the grille**, as on
  the real 5170, where the keylock/LED cluster is above the ventilation
  slats. `.at-indicators` and the `.at-switch-group` sit in a `.at-toprow`
  flex row, after the brand plate and before `.at-vents`.
- **Drive-to-grille ratio.** `.at-left` and `.at-drives` split at 2:1
  (63/37 at a 1400px viewport in the Mid-1990s Web theme), with
  `.at-drives`'s `min-width` at 230px and `.at-vents`'s `min-height` at
  40px. The grille stays dominant like the real machine, and the drive-bay
  controls aren't starved for room. Function wins over reproducing the
  reference photo's exact proportions where the controls would feel
  cramped. The drawn proportions are §59.

## 22. Front panel: square boxed lights, inset grille border

- **Each indicator light sits in its own small recessed black box**
  (`.led-box`, 15x15px, dark background plus inset shadow), as the real
  panel's power/HDD lights sit in individually boxed instrument-style
  cutouts. `#powerLed`/`#hddLed` are toggled by id in `app.js` and
  oblivious to the wrapper `<span class="led-box">`.
- **The lights are square**: `.at-indicators .drive-led { border-radius: 1px }`
  overrides the shared circular `.drive-led` shape. The floppy-bay activity
  LEDs stay round.
- **The grille has a wider left and bottom border** than its top and right
  (`margin: 0 4px 14px 16px` on `.at-vents`), matching the real bezel's
  plain-plastic framing around the vent field.

## 23. Power LED starts dark

`#powerLed` starts dark, like `#hddLed`, and `#powerSwitch` defaults
unchecked. The LED markup has no `power-on` class. `app.js`'s `powerOn()`/
`powerOff()` toggle the class from the first `change` event, and nothing
calls `powerOff()` on load, so the markup's own initial class is the
starting state. At page load `#powerLed.classList` does not contain
`power-on`.

The rocker switch has no `"O I"` legend (`.at-switch::before`, computed
`content` is `none`), and its label reads "Power On", the imperative
convention of the buttons on this panel ("Insert…"/"Eject"). Both indicator
lights render dark at load and the switch face is unlabelled chrome.

## 24. Display without a header or status text; sound under the screen

The display panel has no "Display · EGA 80×25" `<h2>` and no `#bootStatus`
paragraph. The "Enable sound" checkbox sits directly below the display,
inside `#displayCard`, centered, with its explanatory text. `#bootStatus`
and `#speakerCard` are absent from the DOM, and "EGA 80" does not appear
in the page text.

What `#bootStatus` would say is carried by controls that already exist:

- **Loading vs. ready**: the power switch is `disabled` while firmware is
  fetching and enables the instant it's ready.
- **Powered off**: the power LED is dark and the screen is blanked
  (`clearScreenToBlack()`).
- **A load failure** has no on-page surface. The power switch never
  enables, and `console.error(err)` still fires for diagnosis. A real
  firmware-fetch failure in the wild could be silent enough to confuse a
  visitor, and the status line's old normal-case text isn't worth
  bringing back for it.

There is no `.boot-status` CSS rule and no `bootStatus` variable in
`app.js`.

## 25. Persisting C: across power cycles (IndexedDB), and a blank-drive option

The browser remembers what's on the hard disk instead of reverting to
pristine factory FreeDOS on every power cycle, and a genuinely blank C: can
be mounted so a visitor can install a real DOS from floppy, the authentic
way.

**Why.** `powerOff()` discards the whole `Machine`. That is right for RAM
(cut the power and it's gone), but wd1003's `image` vector would go with
it, and a real fixed disk keeps whatever was last written, power or no
power.

**C++/WASM.** `wd1003.h` has `image(int)`, `dirty(int)` and
`clear_dirty(int)`, mirroring fdc765's dirty/image-readback convention for
its floppy drives. `wasm_machine.cpp` has `hddDirty()`, `clearHddDirty()`
and `hddImage()`, built with the same `Uint8Array.new_()` +
`typed_memory_view` copy-out idiom `floppyImage()` uses. `mount()` doesn't
care where its bytes came from. The front end decides: factory image, a
blank buffer, or a saved one (documented in the file's own top-of-file
JS-surface comment).

**Persistence (`app.js`).** Storage goes through shared
`hdd-worker.js?model=WD1003&db=ibmpcat-hdd` (chunked sectors, factory
fingerprint, legacy `c-drive` conversion; §61, §65). The saved image is
loaded once at page load, in parallel with the firmware/HDD fetch
(`Promise.all` alongside the existing `fetch()` calls). `powerOn()`'s
`mountHdd()` prefers the saved image over the pristine fetched one.
`powerOff()` checks `machine.hddDirty()` *before* discarding the Machine
(the last moment the data exists anywhere) and, if anything changed, pulls
`hddImage()`, keeps it in memory (so the next power-on in the same tab sees
it immediately) and writes it to IndexedDB (so it survives a reload or new
visit). A failed IndexedDB open, read or write only `console.error`s,
matching the page's no-on-page-error-surface convention (§24).

**"Hard disk" panel.** A status line ("Using: factory FreeDOS (default)" /
"Using: saved state ..." / "Using: blank drive, unformatted ...") plus two
buttons: **Reset to factory FreeDOS** (clears the saved state) and
**Mount blank drive...** (saves an all-zero buffer sized to match the real
image exactly, a drive that has never been partitioned or formatted, as a
genuine 30MB fixed disk was before FDISK/FORMAT touched it). Both are
disabled whenever the machine is powered on, since a real fixed disk isn't
swappable while running. Both take effect at the next power-on.

**Tests.** `wd1003.h`'s accessors are thin wrappers over state that
`Wd1003Test.MountedMediaSurvivesControllerReset` and its neighbors already
exercise. Against the live site: a marker file written to C: from a booted
FreeDOS session flips `hddDirty()` true, and after power-off IndexedDB
holds a 31,900,160-byte record (the exact image size). A fresh tab in the
same profile reads "Using: saved state (from a previous visit)" before
power-on, and after power-on the marker file reads back, a round trip
across a full page reload. Both buttons are enabled while off with firmware
ready, disabled the instant power comes on, and re-enabled on power-off. A
blank-drive mount writes a fresh all-zero record, and a reset deletes the
record.

## 26. MS-DOS 3.30 install floppies, no conversion needed

The MS-DOS 3.30 (5.25") archive (`Microsoft MS-DOS 3.30 (5.25).7z`) holds
two raw sector dumps, `DISK01.IMG` and `DISK02.IMG`, both exactly 368,640
bytes. They work with the blank-drive option (§25) as they are. `minfo` and
`mdir` show real MS-DOS 3.30 media: 40 cylinders/2 heads/9
sectors-per-track, boot sector banner `"MSDOS3.3"`, byte-for-byte the
geometry Drive B (360KB) implements, and volume labels `MS330PP01` and
`MS330PP02` ("Program disk 1/2"). Disk 1 is bootable (`COMMAND.COM`,
`FDISK.COM`, `FORMAT.COM`, `SYS.COM`: everything needed to partition and
format a blank C: from scratch, the authentic 1987 installation path).
Disk 2 carries `GWBASIC.EXE`, `DEBUG.COM`, `LINK.EXE`, driver and codepage
files, and the rest of the retail set. The images are floppy media only,
nothing is added to the HDD image, and they are not in the repo. They go
into a drive through the front panel's Insert control.

## 27. Floppy geometry follows the mounted image

Checked with the two MS-DOS 3.30 floppies from §26.

**Drive B is not a boot device.** With A: empty and `DISK01.IMG` in B:,
the BIOS logs `Booting from Floppy... Boot failed: could not read the boot
disk`, then `Booting from Hard Disk... Boot failed: not a bootable disk`,
then `FATAL: No bootable device.` It tries A: (empty) and C: (blank) and
never touches B:. That is period PC/XT/AT BIOS behavior: INT 19h's boot
search was "floppy drive 0, then the fixed disk", and a second floppy
drive was never a boot candidate.

**Drive A reads either media.** A genuine 5.25" high-density (1.2MB) drive
can read a double-density 360KB diskette (a different data rate and step
timing programmed through the FDC's own commands, not a different drive).
`fdc765.cpp`'s `mount()` derives cylinders, heads and sectors-per-track
(and the matching data-rate and step-timing constants) from the mounted
image's size: `<= 368640` bytes is 360KB geometry (40/2/9), otherwise
1.2MB (80/2/15). It does not take them from the bay. A 360KB image in A:
reads past the boot sector (CHS 0/0/1 is offset 0 under any geometry, so
the first read is right under any geometry) and runs the whole real
MS-DOS 3.30 boot chain: date/time prompts, the `Microsoft(R) MS-DOS(R)
Version 3.30 (C)Copyright Microsoft Corp 1981-1987` banner, and a live `A>`
prompt.

A transfer whose computed offset is outside the image is bounds-checked
in `transfer_image_ptr()`, which returns `nullptr`. `chipset.cpp`'s DMA loop
treats that as "nothing to copy" and still calls `finish_transfer()`. A
real controller facing a CHS mismatch reports an ID-not-found or no-data
error. A 1.2MB image in Drive B never reads, because the 360KB drive cannot
read high-density media (§69.1). Existing `Fdc765Test` cases mount an
image sized for the drive they exercise (`MakeImage(80,2,15)` for A:-shaped
tests, `MakeImage(40,2,9)` for the B:-shaped one).

## 28. Downloadable and uploadable HDD image, and the unsaved-changes prompt

**Download and upload C:** are two controls in the Hard Disk panel beside
Reset and Mount blank. **Download image...** takes whatever is current: the
live image via `machine.hddImage()` if the machine is running, otherwise
the staged one (`savedHdd`) or the pristine factory image. It offers a file
through the same Blob + `<a download>` pattern the floppy eject flow uses,
and works while powered on, since it is read-only. **Upload image...**
takes a previously downloaded (or otherwise obtained) image file and
stages it as C: for the next power-on, disabled while running like Reset
and Mount blank. Unlike the floppy controller (§27), WD1003's geometry is
fixed in CMOS regardless of media, since a real fixed disk doesn't change
shape depending on what's written to it. An uploaded image of the wrong
size is refused up front with a clear message. `wd1003.cpp`'s own bounds
check would otherwise report an IDNF disk error deep into a boot.

**`beforeunload`.** C:'s save point is `powerOff()`. Closing the tab,
following a link, or a gesture-based navigation (a trackpad swipe
back/forward) while powered on skips it and discards every write since the
machine was last powered off. A `beforeunload` handler prompts first, gated
on `poweredOn && machine.hddDirty()`, so it doesn't nag a session with no
disk writes and fires only when something real would be lost. Some
browsers' gesture-based navigation can bypass `beforeunload` entirely, a
known browser behavior that script can't fix, which is why **Download
image** exists as a browser-independent backup alongside the automatic
IndexedDB persistence.

**Tests.** A downloaded factory image is exactly 31,900,160 bytes. A
wrong-sized upload is rejected with the expected message and the status
line stays unchanged. A correctly sized upload is accepted, staged and
persisted to IndexedDB. There is no `beforeunload` prompt when powered off
or when powered on with no writes yet, and a real `beforeunload` dialog
appears only after a write while powered on. The reload is blocked until
the dialog is dismissed, and the page's state (still powered on) survives.

## 29. Text-mode scan lines per row come from the CRTC

`RenderTextScreen` (`ega_render.cpp`) takes `int &width, int &height`
out-params, matching `RenderEgaNative16Screen`'s shape. It derives
scan-lines-per-row from `ega.crtc_max_scan_line() + 1`, trusting the real
register, the same "read real CRTC state, don't guess a mode number" rule
every other mode follows. A freshly reset `Ega` (`crtc_[0x09] == 0`, no
BIOS has run yet) would compute a nonsensical 1-line-per-row frame, so that
specific value falls back to the classic 14-line default, since no real
text mode uses 1 line per row. `RenderScreen`'s text-mode case takes its
`out.width`/`out.height` from what `RenderTextScreen` reports.
`kTextRenderWidth`/`kTextRenderHeight` stay as the classic-EGA default and
fallback size, e.g. for the graphics-mode placeholder frame.

A 16-line text mode (an 8x16 font, Max Scan Line 15, Vertical Display End
399, so 400 lines) renders a 640x400 frame. The 8x16 font draws g/y/p/q/j
descenders in rows 12-15. A renderer bounded at 14 lines drops those rows
and loses the letters' tails: row 14 of character 'g' is `0x78`, real
descender pixels. EGA's 14-line convention renders 640x350.

`RowHeightAndFrameSizeFollowTheRealMaxScanLineRegister` programs Max Scan
Line=15 and confirms both the 640x400 frame size and that a pixel in font
row 14 renders. The reset-default 14-line/640x350 behavior is preserved
byte for byte in the other `EgaRenderTest` cases. `wasm_machine.cpp`'s
`renderWidth()`/`renderHeight()` read whatever `RenderScreen` reports each
frame, and `app.js`'s resize logic, built for the graphics modes, handles
text mode's variable height.

## 30. Function and extended keys, and Ctrl+Alt+Del

Mac keyboards have no discrete keys for Insert, Print Screen, Scroll Lock,
Pause/Break, or forward-Delete, and F-keys are often intercepted by macOS
system shortcuts. The page injects those keys, and Ctrl+Alt+Del, as real AT
keyboard scan codes through the `sendKey()` mechanism in `app.js`. They live
on the drawn keyboard (§57).

**Print Screen and Pause/Break scan codes** break the standard "array of
bytes, with bit 7 set on the last for the break code" convention. Real AT
hardware encodes them as fixed byte sequences:

- **Print Screen**: 4-byte make (`0xE0, 0x2A, 0xE0, 0x37`) and a distinct
  4-byte break (`0xE0, 0xB7, 0xE0, 0xAA`).
- **Pause/Break**: a single fixed 6-byte sequence (`0xE1, 0x1D, 0x45, 0xE1,
  0x9D, 0xC5`) sent on make. It has **no break code at all**, a documented
  AT keyboard controller quirk, not an emulator approximation.

`SET1` therefore has a third entry shape, `{ make: [...], break: [...] }`.
`sendKey()` injects the `make` or `break` sequence directly, without the
bit-7 modification applied to normal keys.

**Ctrl+Alt+Del** injects the classic warm-boot sequence: Ctrl make (0x1D),
Alt make (0x38), Del make (0x53). It uses the **non-extended** Delete
scancode (0x53, the original numpad Del/period key), not `SET1.Delete`
(the 101-key extended `[0xE0, 0x53]` forward-Delete). That matches the
BIOS's own Ctrl-Alt-Del check, which, like all x86 BIOSes, looks for this
specific pre-extended code. The BIOS's own keyboard ISR implements the check
and the reboot. The F-key buttons keep a 50 ms make-to-break gap, a human
press, so the break lands as a separate event in the guest's single-threaded
frame loop. The buttons are disabled while the machine is off and enabled
once `poweredOn && machine` is true. A click on Ctrl+Alt+Del triggers a real
warm reboot (BIOS POST re-runs within seconds). The panel wraps through the
existing `.row { flex-wrap: wrap }` rule across the five themes (§65).

## 31. Multi-byte scan code sequences need real time between bytes

A real keyboard clocks one bit at a time over a slow serial line, so the
guest's IRQ1 handler always reads byte N before byte N+1 exists. The 8042
(`i8042.h`) has a 16-byte keyboard FIFO and an 11-bit link frame per byte
(§53.1), so a sequence injected in one burst reaches the guest in order.
`injectScancodeSequence()` in `app.js` is a plain loop that trusts that
queue, and `sendKey()` routes both the plain-array and `{make, break}`
cases through it. The Ctrl+Alt+Del handler sends all 6 bytes (3 make + 3
break) in one call.

The reason this matters: with one unqueued output register, three
back-to-back `injectScancode()` calls in one synchronous JS turn give the
rAF-paced `runCycles()` loop no chance to run and read the buffer between
them, so only the last byte (Del) reaches the guest, and the BIOS's keyboard
ISR sees a lone Del press with no Ctrl/Alt held and never recognizes the
combination. A dropped leading `0xE0` prefix turns an extended key into an
unrelated plain key (Insert's `[0xE0, 0x52]` without its prefix reads as
Numpad 0's make code). Every multi-byte Set 1 sequence is affected the same
way: the E0-prefixed extended keys (arrows, Home/End/Page Up/Down,
Insert/Delete, Numpad Enter/Divide, Ctrl Right/Alt Right), Print Screen's
4-byte sequences, and Pause's 6-byte sequence. Single-byte keys need no
spacing.

## 32. Oregon Trail's "Look at map" screen (open)

MECC's *The Oregon Trail* (1990, run as `OREGON CGA` per its own README's
EGA guidance) renders every screen correctly (title splash, wagon/family
portrait, Independence street scene, Matt's General Store, the oxen
graphic) **except** the "Look at map" option from the trail-status menu,
which renders as dense, glyph-like green-on-black noise instead of a map.
Every other graphics screen in the same game, including ones as visually
complex as the wagon/family portrait, decodes perfectly, so it is not a
generic CGA or text-mode regression and not §29's descender case.

**Two hardware-accuracy points**, both from the CRTC registers a real
6845/EGA CRT controller uses:

- `RenderTextScreen` (`ega_render.cpp`) reads the CRTC's own Horizontal
  Displayed register (R01) for the column count, the way
  `RenderEgaNative16Screen` reads it for graphics resolution. Real 40-column
  text (BIOS mode 0/1) is genuine hardware, and hardcoding 80 would scramble
  any real 40-column screen by reading every row after the first from the
  wrong VRAM offset
  (`EgaRenderTest.ColumnCountAndFrameWidthFollowTheRealHorizontalDisplayedRegister`).
- `RenderEgaNative16Screen` derives its per-scanline VRAM stride from the
  CRTC's Offset Register (R13, `ega.crtc_scanline_stride()`, which real
  hardware specifies in words/2-bytes), not from the displayed width. The
  two are usually equal but are independent registers, and software
  panning or blitting into a wider logical scan line than it displays needs
  them to differ.

**Neither is the map screen's cause.** The live register state at the
moment the corrupted frame renders (read through temporary Embind
bindings) is `hDisplay=79, vDisplay=399` (640x400), Offset Register equal to
the displayed width (no mismatch), Start Address 0, and odd/even chaining
disabled (true linear planar addressing, not a text or CGA-style access
pattern). The one anomaly is the Graphics Controller's Memory Mapping field
reading **3** (32K window at 0xB8000, the CGA/color-text-compatible window)
while the Shift Register field reads **0** (native 4-plane 16-color
addressing). No legitimate BIOS mode-set for a native 16-color mode
produces that pair, since those select Memory Mapping 1, the 64K window at
0xA0000. Forcing the same VRAM through the CGA-compatible 4-color decoder
also produces noise, so it isn't CGA data misread as native EGA.

That points to the game driving the Graphics Controller and Sequencer
directly for this one screen (a common era technique, since full BIOS
mode-sets are slow) while relying on whatever Memory Mapping was left over
from its earlier CGA mode-4 screens, rather than a complete, self-consistent
mode-set. Whether that combination is well defined on genuine 1980s EGA
silicon, or a period compatibility bug in a game whose only two shipped
graphics drivers are `CGA.BGI` and `VGA256.BGI` (no true `EGA.BGI`, and its
README tells EGA owners to run in CGA mode for every screen), is
unresolved. The screen is not fixed, and no fix is guessed without a
confirmed root cause.

## 33. Auto-boot on page load, and screen focus

**Auto-boot.** The power switch flips itself on
(`powerSwitch.checked = true; powerOn();`) the instant the async
firmware-fetch block resolves, so a visitor lands at a running machine
instead of an inert front panel. The switch and `powerOn()`/`powerOff()`
stay fully functional, so a visitor can power off and back on by hand.
Only the first boot is automatic. With the switch never touched,
`powerSwitch.checked` flips to `true` within ~2 s of load, and the machine
boots unattended to a `C:\>` FreeDOS prompt.

**Screen focus.** Every button on the page (F-keys, Ctrl+Alt+Del, floppy
Insert/Eject, HDD controls, the power switch) takes keyboard focus when
clicked, like any button on any page. A real AT keyboard has no notion of
"the front panel has focus", so a keystroke after such a click would go
nowhere. The page shows a "Click to focus" hint while the screen lacks
focus (§42), and `#screen`'s own click-to-focus handler focuses it. There
is no page-wide listener that refocuses `#screen` after every click, since
that made every other control unusable by keyboard (§38).

## 34. Playwright integration suite (`ibmpc-at/web/tests/`)

The suite matches altair8800/assembler6502's depth: `boot.spec.ts`,
`keyboard.spec.ts`, `floppy.spec.ts`, `hdd.spec.ts`, `speaker.spec.ts`,
`theme.spec.ts`, `home.spec.ts`, plus `playwright.config.ts`,
`tests/helpers.ts` and `tests/fixtures.ts`. §54 and §66 cover the full set.
Two details:

- **`Machine::textScreen()`** (`web/wasm_machine.cpp`, a test-only
  convenience behind the `?test=1`-only JS exposure of `window.__test`).
  This machine has no serial-terminal text buffer the way
  altair8800/assembler6502 do, since its output is an EGA-rendered canvas,
  so a test can't assert "the screen shows X" without OCR or a scrape. The
  method reads the live text-mode VRAM back into a plain string (25 rows,
  one line each), using the same character-cell addressing
  `ega_render.cpp`'s `RenderTextScreen` uses (live CRTC Horizontal
  Displayed register for column count, live start offset, plane-0 byte per
  cell). It returns `""` outside text mode. It has no real-hardware
  counterpart (a person just looks at the CRT) and is purely a test seam,
  like `window.__test` itself.
- **Keyboard timing in `keyboard.spec.ts`.** `page.keyboard.press()` fires
  `keydown`/`keyup` with no delay. The keyboard FIFO and 11-bit link
  timing (§53.1) hold Enter's break behind its make, so Enter submits a
  typed `DIR`. `injectScancodeSequence` is a plain loop that trusts the
  queue.

## 35. The HDD image build reads the real video memory layout

The `hdd-image` build target (`disks/build_freedos_hdd.cpp`, which drives
the real FreeDOS 1.3 installer end to end, §11) watches the installer's
prompts (`"Do you want to proceed"`, `"Insert diskette #2"`, and so on)
through its `ScreenText()` helper. `ScreenText()` addresses VRAM the way
the renderer does: `vram[(plane_offset << 2) + plane]`, with `plane_offset
= (start_offset() + row*cols + col) & 0xFFFF`, plane 0 = character byte,
plane 1 = attribute, and no fixed base (`ega.h`'s file header, and what
`ega_render.cpp`'s `RenderTextScreen` reads). A flat byte-pair model with
an arbitrary base would read zeroed, never-written VRAM, every screen would
look blank, no `Step` prompt would match, and the build would burn its whole
cycle budget. The failure mode prints `FAILED: cycle budget (120000000000)
exhausted before the installer reported completion` with a `Last screen`
dump (§46.7 has the default budget).

A clean run reports `install reported complete at cycle N`, well inside the
budget (§52.6 has the count), and produces the correct 31,900,160-byte
image. The Playwright
specs (§34) pass end to end against that image and the wasm module,
including `boot.spec.ts`'s unattended boot to a live `C:\>` prompt. CI's
`ibmpcat-web-test` job runs the build on a cache miss (§66).

## 36. A test-only CPU speed multiplier

Every Playwright spec (§34) pays a real, wall-clock-paced 8 MHz POST and
FreeDOS boot (about 29 s, §61).
`app.js`'s `frame()` feeds `dtSeconds * 8000000` real cycles per real
second, and there is deliberately no other option (CLAUDE.md, "Never speed
these up"). That is the dominant cost in this suite's runtime, unlike
assembler6502's (`CGOAC6502_REVIEW.md` §14, where the CPU was never the
bottleneck). The multiplier is a policy decision authorized directly by the
user ("smoke tests at real speed, rest multiplied up"). `CLAUDE.md`'s
"Current sanctioned overrides" documents the rule, and this section is the
implementation record.

**Mechanism** (`web/app.js`). A module-level `TEST_CPU_MULTIPLIER` is read
once from `?test=1&fast=1`. Both params are required, so `?test=1` alone, or
a bare page load, still runs real 8 MHz. It applies to `frame()`'s
per-frame cycle credit (`dtSeconds * 8000000 * TEST_CPU_MULTIPLIER`). The
value is `20`: real ~45 s boots land around 2-3 s, comfortably under
`waitForScreen`'s generous timeouts, without single-frame cycle counts (up
to ~4M cycles per frame at a 60 Hz rAF cadence) growing large enough to
risk a "page unresponsive" stall from one synchronous `runCycles()` call.

**Audio at any multiplier.** `pumpAudio()` takes the frame's *real*
elapsed `dtSeconds` and derives both the buffer duration and the
edge-to-sample conversion from it, not from `cyclesThisFrame / 8000000`.
Audio is correct at any multiplier, and identical to the 1x behavior when
the multiplier is 1. A fast-mode frame doesn't claim ~20x its real
duration.

**Smoke test** (`tests/smoke.spec.ts`): the one spec that passes
`realtime: true` to `boot()` (skipping the default `fast=1`), then samples
`machine.totalCycles()` across a real 2-second `wallWait`, asserting the
measured cycles/real-second lands near 8,000,000 (bounded `4M`-`8.8M`).
This is the concrete "verify the clock itself" check the override rules
require, mirroring altair8800's "a few tests select `Realistic` on
purpose" convention, extended to the CPU.

**`helpers.ts`'s `boot()`** defaults every call to `fast=1`. A
`realtime?: boolean` option opts back out.

**Deploy strip**: `web/Makefile`'s `STAGE_POST` runs `strip_fast_flag.py`
so the staged `app.js` hard-codes the multiplier to `1`. The root `make
site` also `--check`s it, and `check_site.py` fails any remaining fast
path. Visitors never reach the override (§66).

`disks/build_freedos_hdd.cpp`, the native build-time tool that produces the
shipped `freedos-hdd.img` (§35), runs unthrottled, with no wall-clock
pacing. Its ~15 real minutes is interpreter throughput, not a removable
delay, and the `actions/cache@v4` step in CI (keyed on the native
source/build-script files) means it only runs when one of those files
changes. A push that doesn't touch ibmpc-at shows the build step `skipped`
(cache hit).

altair8800 and assembler6502 don't get this flag. Neither suite's cost is
CPU-bound (altair8800's is solved by the disk/tape `Max`-speed override.
assembler6502's is per-test browser/wasm startup and 2-worker CI
contention, not clock speed, see `CGOAC6502_REVIEW.md` §14). Add it there,
following this same pattern, only if a real CPU-bound bottleneck shows up.

## 37. EGA canvas height and the speaker AudioWorklet

**Canvas height.** `RenderEgaNative16Screen()` takes the height from
Vertical Display End (`ega.h`'s `crtc_vertical_display_end()`) in the EGA
CRTC alone. CRTC R09 bit 7 ("Scan Doubling") is a VGA feature that genuine
1984 EGA silicon never had, and nothing on an EGA sets it (§48.2). A
VGABIOS-style mode-set programs a ~400-line doubled raster with it, and
Prince of Persia's EGA mode left Vertical Display End at 399 with the bit
set, which draws the 200-line playfield in the top half of a 400-line
canvas. The EGA BIOS stand-in (§48.1) programs no such mode.

**Speaker.** A single persistent `AudioWorkletNode` is fed by a ring buffer
(`kSpeakerWorkletSrc`, registered from a Blob URL). The worklet's
`process()` callback runs continuously on the real-time audio thread
regardless of the main thread and holds the last sample on underrun
instead of clicking. There is no `nextPlayTime`/resync bookkeeping, since
no scheduling clock is left to keep in sync. Chaining one one-shot
buffer-source node per animation frame is fragile however precise the
scheduling math, because any main-thread stall (a GC pause, the periodic
HDD autosave's array copy) leaves the previous node's audio running out
with nothing queued behind it: silence, then an abrupt jump back to a
nonzero level on the next frame. Neither has a Playwright test (no reliable
way to assert on actual audio output or rendered canvas pixels from this
suite). Both were checked live with Prince of Persia (EGA mode, sound on)
and a temporary diagnostic (`debugEga()`/`debug_raw_crtc()`, not kept).

## 38. Fullscreen mode, and the Esc key browsers won't let through

A fullscreen toggle expands `#bezel` (CRT frame, vignette, power LED, not
the bare canvas, so it still reads as hardware). A corner Esc button and a
one-time hint dialog go with it. All of it is web-UI convenience with no
hardware precedent (no real 5170 monitor has a "fullscreen mode"), and it
follows the same "labelled, opt-in, defaults to the realistic path" shape
CLAUDE.md's overrides use, though it overrides nothing simulated.

**Esc.** While fullscreen, the browser reserves the physical Esc key
entirely and never dispatches it to the page while using it to exit
fullscreen. DOS gets nothing, and page script can't reclaim it from the
Fullscreen API (confirmed live). A dedicated Esc button, visible only
while fullscreen, injects the real scancode through the existing
`sendKey()`/`data-key` click-tap mechanism (§30), bypassing the native key
event.

**The hint dialog** explains this *before* fullscreen engages, so the
visitor doesn't hit Esc and get bounced out before reading why. It is
tracked via a versioned `localStorage` key (`retro8080.fsEscHintSeen` +
`FS_ESC_HINT_VERSION`) rather than a plain seen/unseen flag, so the hint's
content can change later: bump the version string and everyone who saw an
older one sees it again.

**Focus.** There is no document-wide `click` listener that refocuses
`#screen` after every click (§33). It made every other control unusable
by keyboard right after clicking it (a checkbox couldn't be toggled with
Space, since focus had already bounced back to the canvas). `#screen`'s own
click-to-focus handler stays. `tests/boot.spec.ts`'s focus test asserts
this.

**Tests** (`tests/fullscreen.spec.ts`): first click shows the hint without
entering fullscreen, dismissing it enters fullscreen and reveals the Esc
button, the hint doesn't reappear once its current version has been seen, a
stale stored version does re-show it (planted directly via `localStorage`),
exiting fullscreen hides the Esc button and resets the toggle's label, and
the Esc button clears a typed command line at the DOS prompt while
fullscreen (the scancode really reaches the guest, not just that the
button exists).

## 39. No Google Drive sync

There is no Google Drive sync (no OAuth, no `#gdriveSyncBtn`, no
`shared/gdrive.js`). C: persistence is IndexedDB in this browser only (§25),
plus Download/Upload image for a portable file backup (§28).


## 40. Per-opcode cycle costs in the 80286 core

`cpu80286.cpp` charges per-opcode, per-operand-width, per-addressing-mode
costs. They are cited against the Intel iAPX 286 Programmer's Reference
Manual / 80286 data sheet timing appendix (via the "Art of Assembly",
Randall Hyde, Appendix D reference table) at each constant:
`grp3_unary` (TEST/NOT/NEG/MUL/IMUL/DIV/IDIV), `grp2_shift` (shift/rotate,
including the CL- and imm8-count cases), `string_op` and `io_string_op`
(REP-prefixed MOVS/CMPS/STOS/LODS/SCAS/INS/OUTS, scaling with the actual
iteration count including REPE/REPNE early termination), `loop_group`
(LOOP/LOOPE/LOOPNE/JCXZ taken vs not-taken), PUSHA/POPA/PUSHF/POPF/ENTER/
LEAVE, INT/INTO, IN/OUT, several MOV variants, `grp1_immed`
(ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m,imm), and PUSH reg16. A register
operand costs 3 on the 80286. `tests/cpu80286_timing_test.cpp` pins the
costs down. `cpu80286.h`'s `step()` doc comment describes the model. The
AT's own memory wait-state penalty is §46.1.

`total_cycles_` (the wall-clock-paced budget `Machine::run_cycles()` spends
by repeatedly calling `cpu.step()`) is anchored to real time. The costs
decide which instructions complete in that budget, so a wrong cost lands
more or less real 80286 work in each real second than genuine hardware
does.

**The benchmark.** Landmark System Speed Test v2.00 (1990, Landmark
Research International Corp, `SPEEDCOM.EXE`) runs off a hand-built 360KB
floppy image (`mformat -f 360` / `mcopy`, not committed, since it is
proprietary 1990 shareware, per CLAUDE.md's rule on copyrighted material),
inserted at runtime through the browser's floppy-insert picker like any
user-supplied disk image. With generic bucket costs (2 register / 7 memory
/ 7 taken-branch / 3 not-taken) it reported an apparent ~11.5-14 MHz CPU
against this machine's wall-clock-paced 8 MHz (`app.js`'s `frame()`).
The local build, the staged `_site/` and a live `curl` of the preview
server had matching SHA-256, and `Cache-Control: no-store` was present, so
it wasn't a stale-cache artifact.

**Hot opcodes.** Landmark's methodology reprograms PIT channel 0 into
free-running mode 2 and takes PIT-timestamped snapshots around a workload,
not a naive tight MUL/DIV loop. A per-opcode execution histogram (a
temporary diagnostic in `cpu80286.h`/`.cpp` with throwaway WASM bindings and
a throwaway Playwright spec that ran `SPEEDCOM`, none of it kept) found
`0x83` (ALU with a sign-extended immediate, e.g. a loop-bound `CMP
reg,imm8`) as the single hottest opcode, ~18% of all executed
instructions. It costs the register-operand 3 on the real 80286, not 7.
`grp1_immed` and PUSH reg16 carry the real values.

**What the number means.** The goal is real per-instruction accuracy, with
every per-opcode timing independently cited against Intel figures and
covered by its own test, not "Landmark's own number reads exactly 8 MHz."
Correcting an overcost speeds up wall-clock throughput the same way
correcting an undercost slows it down. Landmark's self-reported MHz is not
pinned to a target: a 1990-era benchmark's PIT-based self-timing has known
era-specific quirks, and there's no citable reason to expect exactly "8.00"
even on genuine period hardware. Chasing Landmark's displayed number to a
specific value would be a separate, narrower investigation into Landmark's
measurement method itself.

## 41. A labelled prefetch-queue refill tax for taken control transfers

**No effective-address add-on.** On the 8086/8088, memory operands pay a
separate, addressing-mode-dependent EA time on top of the opcode's base
cost, marked `+EA` in Intel's Appendix D table. The `+EA` suffix appears
only in the 8088 and 8086 columns, never in the 80286 column, across every
memory-operand instruction checked (ADC, ADD, AND, CMP, MOV, DIV, IMUL, JMP,
LEA, and more). The 8086/8088 shared their address adder with instruction
execution and serialized EA calculation on top of the base cost, while the
80286 moved address calculation to dedicated hardware that overlaps with
execution, so Intel's published 80286 timings are already the total cost.
An EA add-on would port an 8086-specific quirk onto a chip whose published
numbers don't work that way. (Note 1's +1 clock for base+index+disp is the
one EA charge, §62.)

**The refill tax.** The 80286 has a 6-byte prefetch queue that every
taken or unconditional control transfer invalidates, and it must refill
before the next opcode decodes. Intel's Appendix D table gives *ranges*,
not fixed values, for every taken transfer on the 80286 (Jcc-short-taken
7-10, LOOP/JCXZ-taken 8-11, CALL/JMP near 7-10, CALL/JMP far 11-14/13-16,
RET 11-14, RETF 15-18, IRET 17-20, INT/INT3 23-26, INTO-taken 24-27). The
exact cost depends on how full the queue is when the flush occurs, and
Intel doesn't publish the formula. The core is an aggregate-cost-per-
`step()` interpreter, so a single named constant, `kQueueRefillTax`
(`cpu80286.h`, = 2), is added on top of each instruction's already-cited
floor-of-range cost for every queue-flushing control transfer: Jcc taken,
unconditional JMP (short/near/far), CALL (near/far), RET/RETF (with and
without an imm16 stack-adjust), IRET, INT3/INT/INTO-taken, and
LOOP/LOOPE/LOOPNE/JCXZ taken. Floor plus tax lands inside Intel's documented
range for every one of these, checked case by case. Not-taken branches pay
no tax, since no flush occurs on the fall-through path. `cpu80286.h`'s
comment documents it as an approximation, not a simulation of the real
queue. The queue's fetch side, wait states and idle bus time are §46.2.

CALL near (`0xE8`) charges floor(7) plus the tax, like JMP near (`0xE9`), so
it sits inside Intel's 7-10 range.

`tests/cpu80286_timing_test.cpp` covers every one of these opcodes (Jcc
taken/not-taken, JMP short/near/far, CALL near/far, RET/RETF with and
without imm16, IRET, INT3/INT nn/INTO-taken, LOOP/JCXZ-taken).

## 42. A "Click to focus" hint for when the screen doesn't have keyboard focus

`powerSwitch`'s auto power-on (`powerSwitch.checked = true; powerOn();`,
once firmware finishes fetching) never calls `screenEl.focus()`, and nothing
does on a fresh load until the visitor clicks the CRT. After a page refresh
typing goes nowhere until then. A real AT keyboard has no such state (it is
always "connected"), so it comes from the page having several other
focusable controls (bays, switches, buttons) that are easy to click before
typing.

The indicator is a semi-transparent orange banner reading "Click to
focus", spanning the full width of the bezel flush to its bottom edge
(`.focus-hint` in `index.html`). It doesn't dim the screen, and needs no
fullscreen-specific inset override the way `.crt-overlay` does, since it's
flush to the container's edges in both cases. It shows only while
`poweredOn` is true and `document.activeElement !== screenEl`
(`updateFocusHint()` in `app.js`), wired to the screen's own focus/blur
events and called from both `powerOn()` and `powerOff()`, so the auto
power-on case is covered immediately. `pointer-events: none` keeps the
banner from stealing the click meant to focus the screen underneath it. It
is web-UI convenience, not governed by CLAUDE.md's realism rules, on the
same footing as the fullscreen button.

`tests/keyboard.spec.ts` confirms the hint is visible right after `boot()`
(which, like the real auto power-on path, never focuses the screen), hides
on `focusScreen()`, returns when focus moves elsewhere, and disappears once
powered off regardless of focus state. Having the auto power-on grab focus
itself would avoid the problem outright on a fresh load. That is a
separate judgment call and is not done.

## 43. POPF/IRET: FLAGS bits 12-15 always read 0

POPF and IRET load FLAGS with bits 12-15 forced to 0. That is the real 286
in real-address mode (§44.2). `Cpu80286Test.PopfAndIretAlwaysClearIoplAndNt`
pins it.

Letting IOPL and NT round-trip makes software see a 386: "bits 12-15 of
FLAGS are always clear on the Intel 286 processor in real-address mode",
and a 386 lets them stick (Intel AP-485, CPU identification). The FreeDOS 1.3
installer then takes a 386 path on a core that only half-runs 386 code and
dies with a **deterministic** `Runtime error 200 at 10C2:3D6D`, partway
through extracting `FREEDOS.SAF`, right after "Insert diskette #2... Press a
key to continue". It is the same address every run, which rules out
CI-runner-speed flakiness. `disks/build_freedos_hdd.cpp`, re-run locally
against the real install floppies (~10 min per attempt), shows reverting
both POPF and IRET to always-0 completes the install, while changing only
POPF, or only IRET, still crashes at the identical address.

The always-0 behavior is also the classic period technique DOS diagnostics
(CheckIt, MSD, Norton) use to tell an 8086-family chip from a real 286
with no CPUID instruction. CheckIt's "80188, 0.74 MHz" misidentification of
this machine came from PUSH SP (§44.1). IRET with NT=1 on real hardware
performs a hardware task-switch return via the TSS back-link, machinery
this real-mode-only core doesn't implement, so a set NT reaching IRET would
do a plain return while the guest believes a task switch happened.

## 44. CPU parity: PUSH SP, a 286 that can't run 386 code, exceptions, single-step

Items C1-C9 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). C10 (protected
mode) is §63, and C11 (the reset state that comes with it) is §45.

### 44.1 PUSH SP

`push_reg()` pushes SP as it was before the instruction, as the 286 does
(Intel iAPX 286 PRM, "Instruction Set Differences from the 8086"). The 8086
and 80186 push the decremented value. `PUSH SP / POP AX / CMP AX,SP` is how
period software told an 8086 from a 286, so a core that pushes the
decremented value looks like an 8086-class part to every detector. With
PUSHA, BOUND and the mod-32 shift count saying 186, that is the likely
cause of CheckIt's "80188" in §43.

### 44.2 FLAGS bits 12-15 are always clear in real mode

POPF and IRET's always-0 IOPL/NT is the chip: "bits 12-15 of FLAGS are
always clear on the Intel 286 processor in real-address mode", and a 386
lets them stick (Intel AP-485, CPU identification). Letting them round-trip
makes software see a 386 (§43). The FreeDOS installer then takes a 386 path
on a core that only half-runs 386 code, which fits its runtime error 200
better than anything reading FLAGS downstream. The code's comment and test
say it's correct.

### 44.3 No 386 instructions, and undefined opcodes

A 286 raises #UD (INT 6) on the `0x66` prefix with 32-bit registers, Jcc
rel16, SETcc, MOVZX/MOVSX and two-operand IMUL, and programs probe for a
386 by trying one. The core raises #UD on all of them wherever they run,
ROM included, because the BIOS is built for a 286 (§70.1, §70.2).

These undefined opcodes raise #UD, with IP on the instruction: `63h` (ARPL
is protected mode only), `64h`, `65h`, `67h`, `0F 00`, `0F 02`, `0F 03`,
`0F 01 /5` and `/7`; a register operand to LEA, LES, LDS, BOUND, or far
CALL/JMP; `MOV CS`; and `FF /7`. `D6h` is SALC, undocumented but present on
every Intel part from the 8086 on (Ralf Brown's OPCODES.LST). `F1h` is a
prefix (§45.4).

### 44.4 The real-mode system instructions

Unknown `0F` opcodes raise #UD, so the ones a 286 accepts in real mode are
implemented:

- **SMSW / LMSW.** MSW resets to FFF0h. LMSW loads MP, EM and TS, and sets
  PE (it can't clear it). What PE does is §63.2.
- **CLTS** clears TS.
- **LGDT / LIDT / SGDT / SIDT** load and store a 24-bit base and a limit.
  SGDT and SIDT write FFh in the sixth byte where a 386 writes 00h, another
  period 286-or-386 test (Intel386 PRM, SGDT/SIDT, 80286 compatibility).
  The IDT resets to base 0, limit 3FFh, and real-mode INT n vectors
  through IDTR's base, so LIDT moves the vector table as it does on the
  chip. The IDT limit isn't checked.
- **ESC with EM or TS set** raises #NM (INT 7), and so does WAIT with MP
  and TS set. With MSW as the BIOS leaves it, an ESC still does nothing,
  as on an AT with no 80287 fitted.

`0F 05` (LOADALL) is a reported no-op. Descriptor caches are §63.1, and
the open item is C14 (primary-source table layout).

### 44.5 Faults restart the instruction

A word access at offset FFFFh, or a dword past FFFCh, raises #GP
(INT 13), or #SS (INT 12) when the segment is SS. So does an instruction
that runs past FFFFh or is longer than 10 bytes (Intel iAPX 286 PRM,
real-address-mode exceptions 12 and 13). The fault is found
mid-instruction, so the core drops any further bus traffic for that
instruction, puts back the registers it started with, and vectors with IP
on the first prefix. #UD and #NM use the same path. Divide errors and
BOUND also restart at the first prefix, not at the opcode.

A REP string op saves its registers at each iteration, so a fault partway
through keeps the iterations already done, as the chip does.

Exceptions and the single-step trap cost an INT n (23 clocks plus
`kQueueRefillTax`).

### 44.6 Single-step and the interrupt shadow

An instruction that starts with TF set is followed by INT 1, with the next
instruction's address saved, so DEBUG's `T` single-steps. A POPF that sets
TF runs untrapped; one that clears it still
traps. INT n, INT3 and INTO enter their handler untrapped. A faulting
instruction doesn't trap.

STI (from IF clear), MOV SS and POP SS hold off INTR and the trap for one
instruction, so `MOV SS / MOV SP` can't take an interrupt onto a
half-loaded stack. `Cpu::interrupt_shadow()` exposes it and
`Machine::run_cycles()` checks it before polling the PIC. Same design as
`PC486_REVIEW.md` §40.4-§40.5.

### 44.7 A REP can be interrupted

A 64KB REP MOVSW is about 131,000 clocks, 16 ms at 8 MHz. The 286 takes
interrupts between iterations and resumes after
IRET. `Cpu::rep_yield_cycles`, which `Machine` sets to one PIT clock (6
cycles at 8 MHz), is the budget: a REP that reaches it puts IP back on its
first prefix and returns, the machine services its devices and any
interrupt, and the next step continues. A continuation is charged only its
own iterations, so an uninterrupted REP still costs exactly its published
5+4n. REP LODS has no published per-iteration figure and doesn't yield.

Under TF a REP runs one iteration per step and traps after each, with IP
still on the instruction until the last one. Same design as
`PC486_REVIEW.md` §45.3.

### 44.8 Checks

- Native cases cover each item above, plus `Machine` cases: MOV SS holds
  off a pending IRQ for one instruction, and a 64KB REP yields after one
  PIT clock.
- The shipped HDD image boots to `C:\>`. A probe that logs every exception
  vector taken saw no INT 6, 7, 12 or 13 and nothing reported as
  unimplemented.
- The real FreeDOS 1.3 installer (`disks/build_freedos_hdd`) completes
  (§46.7 has the cycle count), and its image boots with no exceptions. It
  detects a 286 and writes the 286 menu to `FDCONFIG.SYS`: `DOS=HIGH`, and
  `FDXMS286.SYS` as the default choice.
- One unexplained extended-key panel failure (`^C` from Pause/Break, then
  no response to typing) came in a run made while the installer build had
  the host busy, and did not recur. The likeliest cause was K1 in
  `IBM_PCAT_PARITY.md`: the 8042 overwriting an unread byte when a late
  frame delivers two bytes of the Pause or Num Lock sequence back to back.
  §53.1 closes K1.

## 45. The real reset vector, the A20 gate, and F1h

Items C11 and C12 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md).

### 45.1 The first fetch is FFFFF0h

After RESET the 286 sets CS to F000h but its hidden base to FF0000h, so
the first fetch is FFFFF0h, near the top of the 24-bit address space. The
base stays there until the first far transfer that loads CS (Intel iAPX
286 PRM, "Reset"). `Cpu::reset()` sets `cs_high_`, code
fetches use FF0000h as the base while it's set and CS is still F000h,
and JMP/CALL far, RETF, IRET, the indirect far forms and every
interrupt entry clear it. A fault restores it with the other registers.

That fetch only finds code because the AT decodes its 64KB system ROM
twice, "at the top of the first and last 1M address space (0F0000 and
FF0000)" (IBM PC/AT Technical Reference, ROM subsystem). `Chipset` mirrors
F0000-FFFFF at FF0000-FFFFFF, read-only. Anything in the alias without ROM
behind it reads open bus.

A data access through a `CS:` override while the high base is in force
still uses F000h's ordinary base. The BIOS's first instruction is a far
jump, so nothing exercises it.

### 45.2 The A20 gate holds only A20

The gate holds only A20 low and passes A21-A23 through
(`addr &= ~0x100000`), not a 20-bit mask that folds everything into the
first megabyte. FFFFF0h with the gate closed is EFFFF0h, where nothing
decodes. That's why rebooting a PC with A20 closed can hang it.

So the first fetch needs A20 open, and the 8042 gives it: its port 2
comes out of reset high (Intel UPI-41A/42 data sheet), releasing the
reset line and opening A20. The output port starts at FFh instead of
00h. The 0xAA self-test also leaves A20 open (OS/2 Museum, "IBM PC/AT
8042 Keyboard Controller Commands").

### 45.3 Closing A20 before boot, for the stand-in BIOS

IBM's POST opens A20 to test memory and closes it before handing over to
the operating system (Wikipedia, "A20 line"), so DOS sees the 8086's 1MB
wrap. `BIOS-bochs-legacy` sends the 0xAA self-test and never closes the
gate: a probe of every 8042 and port 92h write during a FreeDOS boot saw
no 0xD1. Without help, DOS would run with A20 open, and EXEPACK-packed
programs loaded low fail with "Packed file is corrupt".

`Machine::run_cycles()` covers it: when code fetched from ROM lands at
0000:7C00, it closes A20 through the 8042, which is where IBM's POST
leaves it. That's a labelled stand-in for missing BIOS behaviour, not
hardware. It re-arms on every boot, cold or warm. A boot sector's own
jump to 7C00h (an MBR handing to a VBR) runs from RAM and doesn't
trigger it.

### 45.4 F1h

On a real 286 `F1h` is a prefix that does nothing to the instruction after
it, the same LOCK alias it is on the 8086 (hardware tests in rep lodsb,
"Intel 286 secrets: ICE mode and F1 0F 04"). The 80186 raises #UD on it
and the 386 treats it as ICEBP. The core decodes it as a prefix. `F1 0F
04` is STOREALL, which dumps state and waits for an in-circuit emulator.
Without one the machine hangs, and the dump layout isn't published, so it
isn't modelled (C14). `0F 04` raises #UD.

### 45.5 Checks

- Native tests cover the ROM alias, the bit-20-only gate, A20 open at reset
  and after 0xAA, the first fetch at FFFFF0h and back to F0000h after the
  far jump, the boot-sector shim from ROM but not from RAM, and F1h as a
  prefix.
- Both the shipped HDD image and the freshly installed one (§44.8) boot to
  `C:\>` with A20 closed at the prompt and no INT 6, 7, 12 or 13.
- The FreeDOS installer completes and its image boots the same way (§46.7).

## 46. Bus timing: wait states, the prefetch queue, refresh, the EGA raster

Items T1-T5 and T8 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md), plus
P5 and E11, which come out of the same signals.

### 46.1 The AT's bus cycles

The per-opcode costs (§40) are Intel's figures, which assume "bus cycles
do not require wait states" and an instruction already in the prefetch
queue (80C286 data sheet, Instruction Clock Count Assumptions). The AT
doesn't meet either. Its Technical Reference ("System Performance"):

- A bus cycle "requires 3 clock cycles (which includes 1 wait state)".
- "Eight-bit bus operations to 8-bit devices take 6 clock cycles (which
  include 4 wait states)."
- "Sixteen-bit bus operations to 8-bit devices take 12 clock cycles."

That TR edition describes the 6 MHz board. The 339's announcement letter
gives the same RAM, 150 ns access and 275 ns cycle. A zero-wait cycle at
8 MHz is 250 ns, too short for it, and one wait state is 375 ns, so the
339 runs one wait state too. That's derived, not quoted.

`Chipset::mem_clocks` is a 4KB-page table of byte-cycle lengths: 3 for
system-board RAM, the system ROM and its alias; 6 for the EGA's memory
and ROM (an 8-bit card) and for empty slot space, where no card claims a
16-bit cycle. Ports are all 6 except the WD1003 data register at 1F0h,
the one 16-bit port. `Cpu` charges each data cycle's clocks over the 2
the published count includes: a byte or aligned word to RAM +1, an odd
word +4 (two cycles), a byte to an 8-bit device +4, a word +10. Port I/O
goes through the same accounting. The unit-test fixtures leave the
tables unset, so they still check Intel's numbers exactly.

### 46.2 The prefetch queue

With wait states, the bus unit can't always keep the 6-byte queue ahead
of execution. `Cpu::prefetch()` models it after each instruction: the
instruction's bytes come out of the queue, and if it's short, the CPU
stalls while the missing words are fetched at the code's cycle length.
Whatever part of the instruction's own time its data cycles leave the bus
idle goes to fetching the next words, carried over between instructions
so a fetch can straddle two of them. A full queue stops the bus unit.

A taken transfer empties the queue (Intel iAPX 286 PRM, bus unit), even
`JMP $+2`. The published transfer counts already include fetching the
target's first bytes at zero wait states, so the next instruction pays
only the wait states on those words. Every taken transfer goes through
`Cpu::flush()`, which adds `kQueueRefillTax` and marks the flush;
indirect transfers and interrupts mark it too.

Checks it lands on: a run of 3-byte, 2-clock `MOV AX,imm16` costs 4.5
clocks each, the rate 1.5 three-clock word fetches allow, and a run of
13-clock MULs costs exactly 13, with the fetches hidden.

### 46.3 Refresh

"The refresh controller steps one refresh address every 15 microseconds.
Each refresh cycle requires 5 clock cycles", about 5% of the bus (IBM
PC/AT Technical Reference, "System Performance"). PIT channel 1 is the
refresh request generator, "programmed as a rate generator to produce a
15-microsecond period signal". Channel 1's rising edges reach the
chipset, which steals 5 clocks for each one (`Chipset::take_held_clocks`)
and toggles port 61h bit 4 with it (P5).

IBM's POST starts refresh early with `OUT 43h,54h` and `OUT 41h,12h`
(channel 1, LSB only, mode 2, count 18, per the TR's BIOS listing).
`BIOS-bochs-legacy` programs only channel 0; a probe of every PIT write
during a FreeDOS boot saw `43h <- 34h` and two writes to 40h, nothing
else. A real AT left like that would never refresh. `Machine::start_refresh()`
writes IBM's two bytes at power-on and on reset, labelled as standing in
for the BIOS. Software that reprograms channel 1 later still gets what it
asked for.

### 46.4 The EGA raster

`recompute_timing_()` derives the raster on any write to Misc Output, the
sequencer or the CRTC, per the IBM EGA Technical Reference:

- Dot clock: Misc Output bits 2-3, `00` the 14.318 MHz I/O-channel clock,
  `01` the on-board 16.257 MHz. Clocking Mode bit 3 halves it.
- Characters are 8 or 9 dots by Clocking Mode bit 0.
- Horizontal Total is "the total number of characters less 2";
  Horizontal Display Enable End is one less than the displayed count.
- Vertical Total "represents the number of horizontal raster scans ...
  including vertical retrace", with bit 8 in Overflow bit 0. Vertical
  Display Enable End and Vertical Retrace Start take bit 8 from Overflow
  bits 1 and 2. Vertical Retrace End is a 4-bit compare.

IBM's 350-line text parameters check it: 93 characters of 8 dots at
16.257 MHz is the EGA's 21.85 kHz line, and 364 lines make 60.04 Hz.

Input Status 1 bit 3 is vertical retrace from that raster, and bit 0 is
live too (E11): it reads 1 whenever display enable is off,
horizontally or vertically. The TR's text says "logical 0 indicates the
CRT raster is in a horizontal or vertical retrace interval", the reverse.
The CGA reads 1 there, CGA software polls for it to avoid snow, and the
EGA runs that software, so this follows the CGA. Unprogrammed or
implausible registers (outside 30-120 Hz) fall back to a 60 Hz raster.

VGA mode values read as an EGA reads them give about 42 Hz: `67h` in Misc
Output selects 16.257 MHz and a 449-line VGA frame follows. A real EGA
given those values would run at that rate too. That is E1 in the parity
doc (§48.1), not this timing.

### 46.5 Hardware interrupts

`Cpu::hardware_interrupt()` costs INT n's 23+m (80C286 data sheet; m as
`kQueueRefillTax`), plus the two INTA bus cycles that read the vector from
the 8259s, timed as the 8-bit device they are, plus the wait states on the
three pushes and the vector fetch.

### 46.6 DMA

The FDC's DMA holds the CPU off the bus. "All DMA data-transfer bus
cycles are 5 clock cycles or 1.66 microseconds", with the DMA controller
at 3 MHz on the 6 MHz board (IBM PC/AT Technical Reference, "System
Performance"), and the CPU is off the bus for each one. The 3 MHz is half
the CPU clock; on the 8 MHz board that relationship gives 10 CPU clocks a
transfer, which is an inference from the 6 MHz figures. Each byte the
FDC moves holds the CPU off for 10 clocks, through the same
`Chipset::take_held_clocks()` as refresh. The bus-control handover the TR
leaves out isn't charged.

### 46.7 Checks

- Native tests cover each cycle length in the table, word and odd-word
  costs, ports, the fetch-bound and execute-bound queue cases, a jump to
  the next instruction still flushing, IRQ cost on both buses, refresh at
  5 clocks per channel 1 period, port 61h bit 4 following it, and the EGA
  raster against IBM's 350-line parameters, including the retrace lines
  and the halved dot clock.
- The shipped HDD image boots to `C:\>` at 455.6M cycles, about 57 s at
  8 MHz, with no exceptions. Fixed real-time waits like FreeDOS's boot-menu
  countdown don't scale with CPU cost, which fits one wait state, 6-12
  clock EGA accesses and refresh.
- The FreeDOS installer completes at cycle 50,216,919,471, and its image
  boots to `C:\>` at 469.1M cycles with no exceptions. The default budget
  in `build_freedos_hdd` is 120 billion cycles.
- The real-speed smoke check passes.
- Landmark and Norton SI are proprietary and not in the repo. TOPBENCH is
  the period benchmark (T7 in the parity doc, §47).

## 47. EGA memory contention and a TOPBENCH calibration

Items T6 and T7 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). The T7
calibration covers P1-P3, the REP LODS count and the three-element EA clock
(§47.2, §47.4).

### 47.1 The reference: TOPBENCH

TOPBENCH, the Oldskool PC Benchmark (Jim Leonard), is MIT-licensed with
published source (github.com/MobyGamer/TOPBENCH) and ships a database of
results measured on real machines. Two entries matter here:

| Entry | MemTest | MemEA | Opcodes | VidMem | 3DGames | Score |
|-------|--------:|------:|--------:|-------:|--------:|------:|
| IBM PC/AT 319 or 339, 8 MHz, EGA | 923 | 514 | 397 | 1470 | 388 | 13 |
| IBM PC/AT, 6 MHz (01/10/84 BIOS), EGA | 1245 | 705 | 542 | 1477 | 526 | 11 |

Each test is a fixed instruction sequence timed in microseconds with
Abrash's precision Zen timer (PIT channel 0, mode 2, count 0, latched and
negated). VidMem copies 160 words out of B800h, writes two, and copies
160 back. It's the same on both boards, so the EGA sets its speed, not
the CPU clock. None of TOPBENCH is in the repo; the runs below put
`TOPBENCH.EXE` 0.40c on a scratch copy of the HDD image and drove its
menus from a native probe.

### 47.2 The PIT's modes (P1-P3)

`Pit8253` is an 8254-2 (IBM PC/AT Technical Reference, "System Timers";
`pc486` has the same part): all six modes, BCD, counter latch, read-back
and status (Intel 8254 data sheet, "Mode Definitions", "Read-Back
Command"), with channel 1's rising edges reported for refresh (§46.3). A
timer that ran every mode as mode 3 and counted down from half the reload
would read a mode 2 count of 0 back from 32768, and the Zen timer would see
32768 extra ticks (27.5 ms) every time, about 28,000 µs for every test.
§40's Landmark readings go through the same mode 2 readback.

The 8254 spends one clock loading a count, so channel 1's first refresh
request lands 19 clocks after `OUT 41h,12h`, not 18.

### 47.3 EGA processor memory cycles (T6)

The EGA TR's sequencer "allows the processor to access memory during
active display intervals by inserting dedicated processor memory cycles
periodically between the display memory cycles", and Clocking Mode bit 1
gives the split: "CRT memory cycles occur on 4 out of 5 available memory
cycles", or 2 of 5 for the medium-resolution modes. It doesn't give the
cycle length. Five cycles have to fetch four characters in high
resolution, one 32-bit fetch per character, so five memory cycles are
32 dots of the undivided dot clock (36 with 9-dot characters). The same
32 dots give exactly two fetches in the 320-wide modes, where the halved
dot clock makes each character 16 dots, which is the TR's 2 of 5. That
length is derived, not quoted.

The CPU slot is the fifth cycle; in 2-of-5 mode the CPU gets three of the
five, placed at cycles 1, 3 and 4 since the TR doesn't say which. Pages
A0000-BFFFF are flagged `Bus::kMemSlotted`, and each byte access the
EGA's window decodes asks `Ega::cpu_access_clocks()` for the next free
slot after the access starts. The cycle costs the longer of that and the
6-clock 8-bit bus cycle (§46.1). A word is two byte cycles, each waiting
for its own slot. Back to back, that's one byte per 1.97 µs at
16.257 MHz, against the 2.0 µs per byte the 6 MHz and 8 MHz entries both
imply.

### 47.4 Results

Against the 339 entry (the table's VidMem is a 9-dot mode 3, see below):

- VidMem: 1500 with the slots under a 9-dot mode 3. Nothing is fitted, and
  the match is partly luck: see the note under the table.
- Score: 13, the same as the real machine. TOPScore counts full-suite runs
  in 50 ms, and VidMem is most of the gap.
- MemTest is all string instructions. The data sheet's REP counts plus a
  wait state per bus cycle and §46.3's refresh add up to about 929 µs by
  hand. REP LODS costs `5 + 4n` (80C286 data sheet), so the emulator reads
  941, +2%.
- The data sheet's asterisk (`(Note 1)` in the scanned copy) adds one
  clock "if offset calculation requires summing 3 elements". The decoder
  charges it for base + index + displacement.
- MemEA sits on the bus limit, so that clock doesn't move it. The kernel
  is 1368 code bytes and 520 data cycles, 3612 clocks at three clocks a
  cycle, 471 µs with refresh. The emulator reads 476. The real 339 read
  514, so the real 286 leaves its bus idle about 0.85 clocks an
  instruction somewhere the data sheet doesn't describe. Opcodes (-5%)
  and 3DGames (-4%) are likely the same thing. That's T9 in the parity
  doc. A fitted constant would close it but isn't a source. (T9 is out of
  reach without a real 5170, §75.)

| Test | Real 339 | Emulator | |
|------|---------:|---------:|--:|
| MemTest | 923 | 941 | +2% |
| MemEA | 514 | 476 | -7% |
| Opcodes | 397 | 379 | -5% |
| VidMem | 1470 | 1500 | +2% |
| 3DGames | 388 | 373 | -4% |
| Score | 13 | 13 | |

The submitter's monitor isn't recorded. Both IBM AT entries (6 MHz and
8 MHz) are modem7's, with `VideoAdapter=EGA` and nothing about the
display. VidMem depends on it, because a memory cycle is a fixed number
of dots (§47.3): character width over dot clock. Under §48's EGA BIOS,
IBM's mode 3 is 8-dot (the table's 1500 is a 9-dot mode 3), so:

| Display (switches) | Mode 3 clock | VidMem | vs 1470 |
|--------------------|-------------:|-------:|--------:|
| Enhanced Display, high resolution (9, shipped) | 16.257 MHz | 1335 | -9% |
| Colour Display, 200 lines (7) | 14.318 MHz | 1514 | +3% |

The VGA BIOS's 9-dot mode at 16.257 MHz (1500) happened to land between
the two. With a 200-line display the slot model matches the real machine
as well as everything else does, and score is 13. With an Enhanced
Display it's 9% fast, and either the 32-dot memory cycle derived in
§47.3 is short or the real run used a 200-line monitor. The TR doesn't
give the cycle length, so this stays open until a 5170-and-5154 reading
turns up. The shipped machine, with the Enhanced Display, scores 14.

### 47.5 Checks

- Native tests cover the 8254 suite from `pc486` plus the Zen timer's mode
  2 readback and channel 1's refresh rate, REP LODS, the three-element EA
  clock, a slotted word asking the device once per byte, and the EGA's
  slot spacing in both bandwidth modes, after an idle gap and outside the
  mapped window.
- The shipped HDD image boots to `C:\>` at 458.0M cycles with no
  exceptions. The probe's INT 1 count is a resident program chaining INT
  2Ah to DOS's default handler, which INT 1 and INT 3 share. The same
  count shows up with the EGA slots switched off.
- The FreeDOS installer completes at cycle 50,234,086,633 and its image
  boots to `C:\>` at 472.2M cycles. `build_freedos_hdd` answers a prompt
  once the screen has been still for half a second, because with slower
  EGA writes "ready to install FreeDOS" loses its `y` to the prompt's
  keyboard flush. It arms a swap's "Press a key" straight away since both
  lines print together.
- The real-speed smoke check passes.
- TOPBENCH: repeat runs land within 2 µs of the §47.4 table.

## 48. The EGA: an EGA BIOS, and the CRTC and attribute rules

Items E1-E14 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md) (E11 is
§46). The renderer and the BIOS go together. `VGABIOS-lgpl` leans on two
VGA-only bits, scan doubling and the cursor-off bit, which an EGA renderer
doesn't honour.

### 48.1 An EGA BIOS stand-in (E1)

`VGABIOS-lgpl-latest.bin` answers INT 10h AH=1Ah as a VGA, sets VGA's
400-line text with scan doubling, and turns the cursor off through a
VGA-only bit, so programs that probe for a VGA find one. IBM's EGA BIOS is
IBM's copyright, and no maintained open EGA-only BIOS exists, so
`roms/egabios/egabios.asm` is a new one: about 2,200 lines of nasm,
286-only, written to the INT 10h interface of the IBM EGA Technical
Reference (August 1984). No copy of that Technical Reference was
available while writing it or since (§49.1), so the interface details
below are as the author knew them, not checked against it. None of IBM's
code is copied.

- **Parameter table.** The 23 64-byte VIDEO_PARMS entries are laid out
  in the order of IBM's table (modes 0-7, the reserved 8-0Ch, D-10h for
  64KB and again for more, then 0-3 for the Enhanced Display). Mode set
  reaches them through the save pointer at 40:A8, so a program can hand
  the BIOS its own table. Unsourced: the register values in those
  entries. No primary source for them is on file, and they were not
  checked against the EGA Technical Reference.
- **Switches.** Power-on reads the four DIP switches through Input
  Status 0, one clock select at a time, and stores them in 40:88. Switch
  setting 9 (Enhanced Display, high resolution, EGA primary) picks the
  350-line text entries and the 8x14 font. The TR's two switch tables
  give the full map.
- **Cursor.** AH=01h follows IBM's documented emulation: CGA-style
  start/end values above line 4 move down five lines in a 14-line cell,
  the end register is one past the last line and wraps to 0 at the cell
  height, and CX=2000h becomes 1E00h since the EGA has no cursor-off bit.
  40:87 bit 0 turns the emulation off.
- **Functions.** 00h-13h as the TR lists them, AH=1Ah absent. Character
  generator calls 00-03h, 10-12h (with the documented ROWS, POINTS,
  CRT_LEN and CRTC 09h-0Bh, 12h recalculation), 20-23h and 30h. AH=12h
  BL=10h reports a colour EGA with 256KB and the switch setting, and
  BL=20h installs a print-screen routine that prints every row. Graphics
  characters, dots and scrolling cover modes 4-6 and D-10h.
- **Print Screen.** BIOS-bochs-legacy's INT 9 calls INT 15h AH=4Fh but never
  INT 5 (no `CD 05` in the ROM), so the Enhanced Keyboard's E0 2A E0 37
  make lands as keypad `*`. egabios installs INT 5 at power-on and hooks
  INT 15h AH=4Fh: on E0-prefixed 37h it sets a pending flag and returns CF
  clear so INT 9 drops the key; fake-shift 2A/AA and break B7 are swallowed
  the same way. INT 1Ch then issues INT 5 after IRQ1 has EOI'd, since INT 5
  nested inside INT 9/15 would leave IRQ1 in service and INT 17h could
  fail, so a keypress would dump nothing while `ECHO>PRN` still worked. The
  old INT 15h and INT 1Ch vectors are kept in unused IVT slots 60h/61h, and
  the pending flag is 40:F0. D6.
  The INT 5 routine saves AH=0Fh's column count across AH=03h, because
  that call returns the cursor shape in CX.
- **Fonts.** The 8x8 and 8x14 sets are `VGA-ROM.F08` and `VGA-ROM.F14`
  from Yossi Gil's `fntcol16.zip` font collection (Simtel, msdos/screen;
  its file list describes them as the VGA built-in font). `fetch-bios.sh`
  fetches the zip from a Simtel mirror and pins its SHA-256
  (`ee875bb96a5be0cb70f87e1fc3ce85aa4c0c1ed78f958bb8160f9023a25d32a6`),
  and `mkfont.py` reads the two files out of it. There is
  no 9x14 alternate set; AH=11h AL=30h BH=5 returns an empty list.
- **Labelling.** The ROM prints `EGA BIOS: open stand-in for IBM's ROM`
  after its mode set.

The build is `make -C roms/egabios` (nasm plus a checksum byte).
`tests/egabios_test.cpp` boots the ROM on the emulated machine with no
system BIOS and calls INT 10h directly: 26 tests across the header,
power-on, cursor emulation, pages, characters, scrolling, strings, the
three graphics families, palettes, fonts and AH=12h.

The Data Rotate function (AND, OR, XOR) applies to CPU data in write mode
0 and also to Set/Reset data and write mode 2 data, and IBM's documented
XOR dots use write mode 2.

The EGA BIOS runs as plain 286 code, and so does the system BIOS (§70.1).

### 48.2 Renderer (E2-E7, E12, E14)

`ega_render.cpp` is a port of `pc486`'s renderer with the EGA's rules in
place of the VGA's:

- Start address and the 9-bit Line Compare (CRTC 18h plus Overflow bit 4)
  in every mode (E2, E4). Preset Row Scan and AR13 pel panning, with the
  9-dot sequence 8, 0-7 (E3). No byte panning and no pan-split; AR10 has
  bits 0-3 only.
- Text rows come from Vertical Display End and Max Scan Line, so the
  43-line mode draws 43 rows (E5). AR10 bit 3 picks blink or bright
  backgrounds, and blinking characters and the cursor both toggle every
  16 frames (E6).
- SR03 character maps A and B at 16KB banks in plane 2, only map 0
  without Memory Mode bit 1 (E7).
- CRTC 17h bits 0 and 1 put row scan bits 0 and 1 on MA13 and MA14, which
  is how modes 4-6 reach the CGA's interleaved banks (E12). Bit 7 clear
  holds the CRTC in reset, drawn black.
- Graphics blink (AR10 bit 3 set): the pixel passes Color Plane Enable,
  then the blink logic, then the palette (IBM VGA/XGA Technical
  Reference, May 1992, Figure 2-3 p.2-10; Enable Blink field p.2-93).
  A pixel with bit 3 set drops it in the off phase. Unsourced: that bit 3
  is the blinking bit, that the on phase forces bit 3 to 1, and that
  with plane 3 disabled nothing blinks (the masked bit 3 is 0).
- Cursor skew and the underline location, with underline only under
  monochrome attributes (E14).
- GR05 bit 5 picks the CGA 2-bit shift. Bit 6 and R09 bit 7 (VGA's
  256-colour shift and scan doubling) and CRTC 0Ah bit 5 (VGA's cursor
  off) are not implemented, since nothing on an EGA sets them.

### 48.3 Ports (E8-E10)

- Only CRTC 0Ch-0Fh read back, plus 10h-11h, the light pen latch, which
  reads 0 with no pen (E8). Everything else reads open bus. 3C1h isn't
  decoded, and 3CAh and 3CCh are the write-only Graphics 1 and 2
  Position registers. Programs tell an EGA from a VGA this way.
- Input Status 0 returns the switch the clock select picks in bit 4 and
  the vertical interrupt in bit 7 (E9). The interrupt latches at the start
  of vertical retrace when CRTC 11h bit 5 is clear and bit 4 is set,
  clears when 11h is written with bit 4 clear, and arrives on IRQ9 (E10).

### 48.4 Screen (E13)

The 5154 is a 4:3 tube in every mode. `app.js` scales each frame by a
whole factor per axis into a backing canvas and lets the browser smooth
that to a 4:3 box, so 640x350 and 320x200 both fill the same shape.

### 48.5 Checks

- Native tests include the 26 INT 10h tests and the write mode 2 Data
  Rotate test.
- The shipped HDD image boots to `C:\>` at 640x350 under the BIOS.
- The FreeDOS installer completes under it at cycle 50,177,611,554, and
  that image boots to `C:\>` too.
- Playwright includes a check that the boot frame is 640x350.

## 49. EGA addressing, latches and the monochrome ports

Items E15-E17 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md), found while
doing §48.

### 49.1 Odd/even addressing (E15)

`Ega` keeps the CPU address whole and substitutes a different bit for A0,
instead of shifting the address right one. With
Graphics Miscellaneous bit 1 set, A0 becomes the inverted page select
(Miscellaneous Output bit 5), or A16 in the 128KB map. With Memory Mode
bit 1 clear, each plane is 16KB. Plane selection is split the way the
card splits it: Memory Mode bit 2 gates writes to the odd or even
planes, and Graphics Mode bit 4 puts A0 on the read map.

Sources: IBM Personal System/2 Video Graphics Array (VGA/XGA) Technical
Reference (May 1992, bitsavers `IBM_VGA_XGA_Technical_Reference_Manual_May92.pdf`):
Graphics Miscellaneous register p.2-86, where the Odd/Even field "directs
the system address bit, A0, to be replaced by a higher-order bit" and the
Memory Map field 00 selects A0000 for 128KB; Memory Mode register p.2-54,
where Extended Memory "enables the video memory from 64KB to 256KB", so
clear means 64KB total, 16KB a plane.

Unsourced: that the substituted bit is the inverted Miscellaneous Output
bit 5, that it is A16 in the 128KB map, and the 0x3FFF mask itself. The VGA
manual leaves Miscellaneous Output bit 5 undefined, and no copy of the IBM
EGA Technical Reference was available to check these against. The same
holds for §48.1: the EGA BIOS parameter-table values are unsourced.

The display side is `Ega::display_address()`. In byte mode (CRTC 17h bit
6) the CRTC address is the plane offset. In word mode it shifts up one,
with MA15 in bit 0, or MA13 when 17h bit 5 is clear. 17h bits 0 and 1
clear then put row scan bits 0 and 1 on offset bits 13 and 14. Every renderer path, the page's `textScreen()`
and the installer's screen reader all fetch through it, so what the CPU
writes and what the CRTC reads agree by construction, not by both using
the same shortcut.

With IBM's tables the page bit is always set. Character n of a text page
sits at plane offset 2n, not n. A program that turns odd/even off to read
text or attributes straight from the planes finds them where a real card
keeps them.

Modes 4 and 5 take a character clock as 4 pels from plane 0 and then 4
from plane 1, with planes 2 and 3 as the high colour bits.

### 49.2 The Bit Mask merges with the latch (E16)

Bits the Bit Mask leaves out come from the latch, loaded by the last read
wherever it was, not from the byte already at the write address. Code that
reads first, which includes the BIOS and every well-behaved program, gets
the same result either way. Code that doesn't copies the stale latch bits,
as on the real card.

### 49.3 The monochrome I/O addresses (E17)

Miscellaneous Output bit 0 moves the CRTC and Input Status 1 between 3B4h,
3B5h and 3BAh and 3D4h, 3D5h and 3DAh (IBM EGA TR). Only the selected set
is decoded. The register resets to 0, so a bare card answers at 3Bxh until
the BIOS programs it. With monochrome switch settings (4, 5, A, B) the
renderer drives a 5151: palette bit 3 is video and bit 4 is intensity
(EGA TR, Direct Drive Connector), shown on green phosphor. The stand-in
BIOS's mode 7 reaches the CRTC and renders 720x350.

No page control sets the switches, so the shipped machine is the
Enhanced Color Display. Monochrome is native-only.

### 49.4 Checks

- Native tests cover A0 and the page bit, the Graphics Mode read select,
  16KB planes, byte and word display addressing with both row-scan
  banks, latch merging, the 3Bx/3Dx port switch, a CPU text write landing
  where the CRTC fetches it, the monochrome palette, and the stand-in
  BIOS's mode 7 on a monochrome setting.
- The shipped HDD image boots to `C:\>`.
- The FreeDOS installer, whose screen reader goes through
  `display_address()`, completes at the same cycle as in §48.5,
  50,177,611,554.
- Playwright passes.

## 50. The 8259A and 8237A in full

Items P4, P6 and P9 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md), plus
the rest of both chips, which the backlog didn't list. Sources are the
Intel 8259A data sheet (231468-003) and the Intel 8237A-5 data sheet
(231466).

### 50.1 The 8259A

The PIC implements fully nested mode, level-sensed request lines and the
full command set. Two behaviors matter to any DOS program:

- **Fully nested mode.** The resolver walks the levels in priority order
  and stops at the first IS bit. "While the IS bit is set, all further
  interrupts of the same or lower priority are inhibited." A handler that
  re-enables interrupts can't take a lower or equal level before its own
  EOI. `pc486` does the same in its §13.
- **Requests are levels.** Devices drive their IR line with `set_line()`.
  On the chip the request latch is a transparent D latch behind the edge
  sense latch (Figure 9, "Be sure to note that the request latch is a
  transparent D type latch"). IRR is the line ANDed with the edge latch,
  which sets while the line is low and clears when that level's IS bit
  sets. A line that drops before INTA withdraws its request. If that
  happens after INT went out, the INTA gets the default IR7 with no IS bit,
  as in "Edge and Level Triggered Modes". The visible effect: an IRQ0
  square wave that rises and falls while the CPU runs with IF clear loses
  that tick, as it does on the real board.

The rest of the command set is implemented: rotation on non-specific and specific
EOI, set priority, rotate in AEOI, special mask mode, special fully
nested mode, LTIM level triggering, buffered mode's M/S bit, and the
MCS-80/85 vector byte. The AT never programs most of these, but programs
that take over the PIC do.

Special mask mode needs one reading of the data sheet. It says a masked
level "enables interrupts from all other levels (lower as well as
higher)", and that "an IS bit that is masked by an IMR bit will not be
cleared by a non-specific EOI" in that mode. Both hold if the priority
logic sees ISR ANDed with the inverse of IMR, so that is the model, for the
resolver and for the EOI alike.

**P4, poll.** OCW3 with P=1 makes the next read of either port an
interrupt acknowledge. It returns bit 7 set and the level in bits 2-0,
and sets the IS bit. "Polling overrides status read when P = 1, RR = 1."
With nothing pending, the data sheet only defines bit 7 as 0, so the
other bits read 0 here.

**P6, ICW1.** The data sheet lists what ICW1 resets: the edge sense
latches, IMR (cleared, not masked), IR7 to lowest priority, the slave
address to 7, special mask mode, the read select back to IRR, and every
ICW4 function when IC4=0. IRR follows from the edge sense
reset: a line already high has to fall and rise again.

ISR isn't on Intel's list, and Figure 9 doesn't run master clear to the IS
latch. ICW1 clears ISR here anyway. The stand-in BIOS's INT 9 handler
jumps to POST without an EOI on Ctrl+Alt+Del, so POST's ICW1 would leave
IRQ1 in service, that would block IRQ6, and the reboot would hang at
"Booting from Floppy...". That's an inference from how the stand-in BIOS
warm-boots, not something Intel documents. A real 8259A's answer, or IBM's
AT BIOS listing showing whether its CAD path sends an EOI, would settle it.

Left out: the MCS-80/85 sequence's third INTA, which a 286 never sends,
so AEOI does nothing in that mode and the chip isn't left waiting for
the pulse.

### 50.2 The cascade

`Chipset::poll_interrupt()` runs one INTA sequence the way the board
wires it. The slave's INT pin is the master's IR2 line. The master's
`cascades()` checks its ICW3 bitmap, so IR2 vectors through the slave
only when ICW3 says so. The slave answers only when its ICW3 ID matches
the CAS code; a mismatch leaves the data bus undriven, read as FFh. The
slave can give its own default IR7 (vector 77h on the AT) when its
request is gone by the time the master forwards INTA.

"If a higher priority interrupt occurs between the two INTA pulses, the
INT line goes inactive immediately after the second INTA pulse." The
slave's INT therefore drops after each sequence, which re-arms the
master's IR2 edge latch. Without that, an IRQ8 arriving while IRQ14 is in
service would never reach the CPU after both EOIs.

### 50.3 The 8237A

The 8237A model has the full register file and transfer cycle, and the
FDC's copy goes through it.

**P9.** The 8237's Current Address register is 16 bits, and the AT's page
register is a separate 74LS612 that never counts. A transfer crossing a
64KB boundary wraps to the bottom of the same page. This is why DOS's
DMA boundary checks exist. The copy runs one transfer cycle per byte
through `Dma8237::transfer()`, and the 24-bit address goes to the bus
behind the A20 gate. DMA drives the address lines itself, so the gate
doesn't apply, and a page above 0Fh reaches memory above 1MB (none is
fitted yet, P8) rather than wrapping onto low memory. DMA into the EGA
window goes through the EGA's planar logic, as a real write would.

The rest of the chip:

- **Transfer type.** Write (I/O to memory), read (memory to I/O) and
  verify come from the mode register. "Verify transfers are pseudo
  transfers ... the memory and I/O control lines all remain inactive." A
  BIOS verify leaves RAM alone. When the mode and the FDC's direction
  disagree, no data moves.
- **Terminal count.** TC sets the channel's status bit and clears its
  software request. Without autoinitialize it sets the mask bit; with it,
  the base address and count reload and the mask stays. TC is also EOP,
  so the FDC's transfer stops there.
- **Decrement**, from mode bit 5.
- **Base registers**, written alongside the current ones and never
  changed by a transfer.
- **Status.** Bits 0-3 are the TC latches, cleared by the read. Bits 4-7
  are live DREQ ORed with the request register.
- **Command register** bit 2 disables the controller.
- **Master clear and reset** clear command, status, request, temporary
  and the flip-flop, and set all four mask bits. They don't zero
  address, count, mode and the page registers, which aren't on Intel's
  list (the page registers aren't even in the 8237).
- **Write-only registers.** The mask register isn't readable on an 8237A
  (Figure 6 lists those reads as illegal), so port 0Fh reads FFh, not the
  mask. Port 0Dh reads the temporary register.

Two pieces of AT wiring come with it. DMA1's HRQ goes to DMA2's channel 0
(system channel 4), so DMA1 transfers only while that channel is in
cascade mode and unmasked, and DMA2 is enabled. The stand-in BIOS sets
this up in POST. The FDC's DRQ is gated by DOR bit 3, like
its INT (IBM PC/AT Technical Reference, diskette adapter).

Not modelled: software-requested block transfers and memory-to-memory,
since nothing on this board starts one; command bits 3, 5, 6 and 7
(compressed timing, extended write, DREQ and DACK polarity); rotating
DMA priority, with only one active channel. These are P11 in the parity
doc.

### 50.4 The FDC drops INT on reset

With level-sensed IR lines, `Fdc765` has to drop INT when it is held in
reset through the DOR. Otherwise IRQ6 never falls after a reboot, and the
re-initialized PIC never sees an edge. "The RESET input places the FDC in
the idle state. ... INT and DRQ also go low" (NEC uPD765A data sheet).
Reset clears both.

### 50.5 Checks

- Native tests: the 8259A suite (fully nested
  blocking, edges and lost requests, default IR7, LTIM, poll on both
  ports, every OCW2 rotation, special mask and special fully nested, the
  ICW1 reset list, single mode, buffered M/S, the MCS-80 vector), the
  8237A suite (page wrap, decrement, autoinit, status, TC mask, verify,
  disable, master clear, the write-only mask, DMA2's port aliasing), and
  chipset cases for the 64KB wrap, verify, the A20 gate, the DMA2 cascade
  gate, DOR bit 3, slave nesting, the slave's default IR7, ICW3 and the
  CAS ID, a timer tick lost under CLI, and the FDC reset edge.
- The shipped HDD image boots to `C:\>`, and the FreeDOS floppy boots to
  the installer.
- The FreeDOS installer completes at cycle 50,177,611,554, the same as
  §49.4.
- Playwright includes the Ctrl+Alt+Del reboot behind §50.1's ISR question.


## 51. Storage: the WD1003 command set, FORMAT on both controllers, the data rate

Items S1-S4 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). Sources are
the WD1003-WA2 OEM manual (preliminary, 3/06/86, bitsavers
`pdf/westernDigital/pc_disk_controller/WD1003/`), the Seagate ST4038
product manual (36012-001D, 3/86, bitsavers `pdf/seagate/mfm/ST-4038/`)
and the NEC uPD765A data sheet. The WD1003-WA2 is the combined
Winchester and floppy board, so its manual covers 3F2h, 3F6h and 3F7h as
well as the task file.

### 51.1 The WD1003 command set (S1)

Table 5-1 lists eight commands: Restore 1xh, Seek 7xh, Read Sector 20h,
Write Sector 30h, Format Track 50h, Read Verify 40h, Diagnose 90h and Set
Parameters 91h. All eight are implemented:

- **SEEK** completes with an interrupt, like RESTORE.
- **READ VERIFY** reads the sectors without sending data to the host,
  with "a single interrupt ... upon completion of the command". It's
  paced at the disk rate like a read. Past the end of the disk it reports
  IDNF.
- **FORMAT TRACK** takes a 512-byte buffer holding a (flag, sector
  number) pair per sector. "Command completion will leave all data
  fields initialized to zeroes. The completion interrupt is generated
  after each track has been formatted." It takes one revolution: the
  ST4038 spins at 3,600 RPM (product manual 1.3), so 16.7 ms.
- **DIAGNOSE** puts 01h ("no errors") in the error register, sets the
  write precomp register to 32, the sector count to 1, and the cylinder
  and SDH registers to 0, then interrupts.

**Bad blocks.** A flag of 80h in the format table marks that sector bad.
Read, verify and write then stop at it with BB (error bit 7), after
moving any sectors before it. A raw image has nowhere to store the mark,
so marks live in memory only. They survive a reset, and a remount or page
reload clears them.

**Write precomp.** Writing 1F1h loads the write precomp register, which
is separate from the error register you read there (Figure 6-1).

**IDENTIFY.** ECh is an ATA command, and a real WD1003 aborts it. The drive
is found through the CMOS type table (§71).

**Long mode.** The L bit (22h, 23h, 32h, 33h) sends 512 data bytes plus 4
ECC bytes (§52.2, S6).

### 51.2 Per-sector read interrupts and the IEN latch

Two more from the same manual, both under READ SECTOR and the Fixed Disk
register:

- "Interrupts occur as each sector is ready to be read by the system. No
  interrupt is generated at the end of the command." The controller reads a
  sector (BSY), raises DRQ and IRQ14, and starts on the next one once the
  host has drained the buffer. The BIOS's disk code follows along, waiting
  on IRQ 14 for each sector (§71.1).
- WRITE SECTOR interrupts per sector too, as the manual says (§68.2).
- IEN- (3F6h bit 1) "does not clear the interrupt level in the disabled
  state. A pending interrupt would occur when the interrupt is enabled
  again." The controller latches the interrupt whatever IEN- says, and
  IEN- only gates the line.

### 51.3 FORMAT on the floppy (S2)

FORMAT A TRACK runs as the data sheet describes (the stand-in BIOS's INT
13h AH=05h waits for IRQ6). DMA supplies C, H, R and N for
each sector (4 x SC bytes, mode 4Ah from the BIOS), the data fields are
filled with the D byte, and the result phase and IRQ6 follow one
revolution later (index to index): 1/6 s in the 360 RPM A: drive, 1/5 s
in the 300 RPM B: drive.

Where the sectors land needs one call. The 765 formats the track under the
head, and the IDs it writes are whatever the table says. A raw image can
only hold a sector at its ID's address, though, and READ DATA finds sectors
by the C/H/R it's given rather than by head position. So each sector goes
where its ID says, the one placement the image can represent (§69.1). The
BIOS seeks before AH=05h (§69.2). IDs off the image's geometry, or with N
other than 2, are skipped.

### 51.4 The data rate (S3)

3F7h, when written, is the Floppy Control register: 00 = 500 kbps, 01 =
300, 10 = 250, 11 = 125 kbps FM, with 500 kbps the default after reset
(WD1003-WA2 manual, Table 6-2). It's latched and cleared to 500 kbps by
reset. The rate is enforced (S7, §69.1), and the BIOS writes 3F7h (patch
0003, §69.2).

### 51.5 IRQ6 and DOR bit 3 (S4)

"FDMAEN ... Setting this bit to 1 gates floppy disk DMA and interrupt
requests to the I/O interface. Setting to 0 disables the DMA and interrupt
request drivers" (Table 6-2). The FDC's own INT is unaffected, so an
interrupt raised with the bit clear shows up when it's set. `Fdc765`
gates its IRQ output with the bit, the same way §50.3 gates DRQ, and
holds INT internally. So a seek or a reset with the bit clear raises no
IRQ6 until it's set, and a transfer that finishes with it clear keeps its
interrupt.

### 51.6 Drive B and the CMOS equipment byte

CMOS 14h is the equipment byte, and `rombios.c` copies it straight into the
BDA equipment word at 40:10. The factory CMOS sets it to 41h: two drives,
and bits 5-4 = 00 for a display adapter with its own BIOS, the EGA. A
value of 01h (diskettes fitted, bits 7-6 = 00) means one drive, and DOS
would then treat B: as a phantom of A: ("Remove diskette in drive A:") and
no disk in B: would ever read. Source: IBM PC/AT Technical Reference, CMOS
equipment byte.

### 51.7 What FreeDOS FORMAT does

`FORMAT B: /U` on a blank 360KB disk runs all 80 tracks through INT 13h
AH=05h and reaches "100 percent completed". It then writes the boot
sector, which reads back (`DIR B:` shows the new serial number). The next
write, the first FAT sector, comes back from INT 26h with 0207h. FORMAT
retries with AL | 80h, and the kernel rejects that as unit 81h ("unknown
unit for driver"). In the FreeDOS kernel, `blockio()` returns that code
when the sector is past the drive's BPB size. §52.5 traces it to the
kernel, which never built a drive parameter block for B:, not to the
controller or the stand-in BIOS's INT 13h answers (S5).

### 51.8 Checks

- Native tests: WD1003 SEEK, READ VERIFY timing and IDNF, DIAGNOSE, the
  precomp register, per-sector read interrupts and the BSY gap between
  sectors, FORMAT TRACK's timing and zero fill, bad blocks on read, verify
  and write, bad blocks cleared by a reformat and a remount, the IEN-
  latch, and Long mode. FDC FORMAT timing in both drives, fill, ID
  placement and skipped IDs, the data-rate latch, and DOR bit 3 holding
  INT. A chipset case formats a track through DMA mode 4Ah and takes IRQ6.
  The machine test checks 14h = 41h.
- `COPY` to and `DIR` of a disk in B: work.
- The FreeDOS installer completes at cycle 50,167,844,209.
- Playwright includes a `FORMAT B:` test that checks the fill byte on the
  last track.

## 52. Storage: ECC and Long mode, the turning platter, and FreeDOS FORMAT

Items S5, S6 and the rotation half of S8 from
[`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). New sources: the WD2010-05 and
WD11C00-13 data sheets in WD's 1986 Storage Management Products Handbook
(bitsavers `components/westernDigital/_dataBooks/`), the FreeDOS FORMAT
source (`FDOS/format`, `createfs.c` and `hdisk.c`), and a 5170 owner's
notes on the IBM adapter (retrocmp.de, `ibm/5170/5170-t3.htm`).

### 52.1 The ECC (S6)

The WD1003-WA2 puts its data-field ECC in a WD11C00, a 32-bit shift
register with the polynomial x^32+x^28+x^26+x^19+x^17+x^10+x^6+x^2+1.
The WD11C00 sheet gives the rest. The register is preset to FFFFFFFF
before the A1 F8 data mark, "As the first data byte is reached, the
polynomial control logic will have generated ECC bytes B517894A", and it's
read out most significant byte first. A plain MSB-first shift of A1 F8
from FFFFFFFF gives exactly B517894A, which pins the bit order. Data plus
its four ECC bytes leaves a zero syndrome. `Wd1003::data_ecc()` is that
register, and a test checks it against a value computed separately.

### 52.2 READ LONG and WRITE LONG

With the L bit set (22h, 23h, 32h, 33h), "no ECC or CRC characters are
generated or checked ... the 4 ECC bytes are handled as an additional 4
bytes of data" (WD2010 sheet). They're sent a byte at a time after the 512
data bytes (WD1003 manual 5.2.3). READ LONG returns the sector's recorded
ECC. WRITE LONG records the host's four bytes. If they don't match the
data, the mismatch is kept per sector in memory, like the bad-block marks,
since a raw image only holds data. A normal write, a FORMAT TRACK of that
track, or a remount clears it.

### 52.3 Correction

A normal READ or READ VERIFY of a sector whose recorded ECC doesn't match
works out the syndrome. "Data errors up to 5 bits in length will be
automatically corrected", and "For ECC errors, eight Read retries are made
... ECC correctable data errors are corrected after two consecutive
matching ECC syndromes" (WD1003 manual 5.2.3, 5.2.6). So:

- **Correctable.** If some burst of 5 bits or less anywhere in the 516-byte
  record gives that syndrome, the controller rereads once (one
  revolution), flips those bits in what it hands over, and sets CORR,
  status bit 2. The data on the platter is left as it is, the same as on
  the drive.
- **Uncorrectable.** After eight rereads (eight revolutions) it sets ERR
  and error bit 6. "If an uncorrectable error occurs, the data transfer
  will still take place. A multi-sector read, however, will terminate
  after the sector in error is read by the system." READ VERIFY stops
  there too.

The search tries every start bit and the 16 bursts that begin there, and
it's linear in the syndrome, so a table of single-bit syndromes is enough.

The manual's text on the T bit is garbled in the scan ("verify the sector
for ten disk revolutions, if T is set to 1"). The reading used here: T
limits the WD2010's ID search (10 revolutions, 2 with T set, before IDNF),
and the eight ECC rereads happen either way. The WD2010 sheet describes T
on the bare chip, where it stops correction entirely. The WD1003's
firmware does its own correction, so that doesn't carry over.

One conflict: the WD2010 sheet says FORMAT fills data fields with FFh,
and the WD1003 manual says the board leaves them zeroed. The board manual
is the product being modelled, so it's still zeroes.

### 52.4 The platter turns (S8, rotation)

The platter's angle follows the cycle count at 3,600 RPM, with index at
angle 0 and 17
sector slots a track. A read waits until its sector's slot has passed
under the head, so a single sector takes anywhere from one slot to one
revolution plus a slot, depending on where the platter is.

Which slot a sector sits in is the interleave. The owner's notes say the
IBM adapter's "default interleave is 3", and that it can't go below 2:1,
so the factory layout is 3:1, sector r in slot 3(r-1) mod 17. FORMAT TRACK
replaces that per track with the table's order, and a sector left out of
the table has no ID, so reading it searches for 10 revolutions and then
reports IDNF. A test shows why AT drives weren't formatted 1:1. With the
next sector in the very next slot, the host can't empty the buffer in
time, and a two-sector read costs a whole extra revolution.

Writes wait for their sector slots (§68.2), and seeks take time (§72).

### 52.5 FreeDOS FORMAT (S5)

A trace of every INT 13h, 25h, 26h and 21h/44h call during `FORMAT B:`
settled §51.7. FORMAT writes the boot sector itself with INT 13h AH=03h,
which succeeds. Then the first INT 26h returns 0207h without the kernel
issuing any INT 13h, so no hardware is involved. The kernel has never
built a drive parameter block for B:, and its FAT32 guard in
`int2526_handler()` rejects the write.

FreeDOS 1.3 ships FORMAT 0.91v (`FORMAT.EXE` dated 2017-09-19). FORMAT's
own source calls `Force_Drive_Recheck()` (INT 21h AH=0Dh and AH=32h) right
after writing the boot sector, with this note: "from 0.92, if we don't do
this then formating changes floppy disk paramters then first time
attempting to format can fail if no previous format". The trace shows no
AH=0Dh or AH=32h. With DOS having read B: once first, FORMAT 0.91v runs to
"Format complete" with 368,640 bytes total, on this emulator as it would
on an AT. FreeDOS 0.91v gets this wrong. Later FreeDOS corrects it, and it is not an emulator gap.

### 52.6 Checks

- Native tests: the WD11C00 generator against an independent value, READ
  LONG returning data then its ECC, WRITE LONG with a matching ECC, a 5-bit
  burst corrected after one reread with CORR set, a two-place error left
  uncorrectable after eight rereads with the data still delivered, a
  multi-sector read stopping after it, READ VERIFY correcting and
  reporting, a normal write and a remount clearing a stored ECC, the 3:1
  factory interleave in READ VERIFY's timing, a read waiting for its
  sector, the busy time to the next sector, FORMAT starting at index, the
  1:1 interleave miss, a sector left out of the format table, and the
  10-revolution IDNF search.
- The FreeDOS installer completes at cycle 59,543,817,039, about 19.5
  emulated minutes more than a fixed-time read model needs, all of it
  reads waiting for their sectors at 3:1.
- The shipped image boots to `C:\>` at 57.3 s of emulated time.
- The `FORMAT B:` Playwright test runs the whole command: DIR, FORMAT,
  "Format complete" with 368,640 bytes, the BLANK label, and the fill byte
  on the last track.

## 53. Keyboard and RTC: the keyboard's buffer, typematic, commands, and a ticking clock

Items K1-K5 and R1 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). Most
of it ports `pc486` (`PC486_REVIEW.md` §40.1-40.2), with the AT's own
differences. New sources: the IBM Keyboard Technical Reference (6523261,
as quoted on geekhack, "Rollover, where keyboard data might go"), HelpPC's
8042 page for the AT input port and command byte, and Chapweske's "The
AT-PS/2 Keyboard Interface" for the link timing.

### 53.1 The keyboard holds its own bytes (K1)

A keyboard byte doesn't go straight into the 8042's single output
register. A second byte before INT 9 read the first would destroy it, and
a page timer spacing each byte could interleave two keys' timers, which is
how `pc486` got stuck keys.

The real keyboard has "a 16-byte first-in-first-out (FIFO) buffer ... that
stores the scan codes until the system is ready to receive them". The
model has that buffer, and each byte crosses the link as an 11-bit
frame, 825 us at the middle of Chapweske's 10-16.7 kHz clock range. A byte
only starts when the output register is empty and the controller isn't
holding the clock low (command byte bit 4, which ADh and AEh set and
clear). Three things follow:

- IRQ1 drops when INT 9 reads port 60h and rises again a frame later, so
  every byte is a new edge for the edge-triggered 8259. Delivering the
  next byte on the read itself would have held the line high and lost the
  interrupt.
- The Bochs INT 9 handler brackets its read with ADh and AEh. A key
  pressed inside that window waits in the keyboard.
- "An overrun code replaces byte 17. If more keys are pressed before the
  system allows keyboard output, the additional data is lost." The code
  is Set 2's 00h, which the 8042 translates to FFh.

A response to a command (ACK, echo, ID, BAT) goes ahead of buffered keys,
and the controller takes it even with the clock inhibited, since it's
waiting for it. A command written while a key byte is in flight restarts
that byte. The controller's own answers (55h, the command byte, the ports)
still go straight into the register with no link delay.

The page puts each sequence in at once (`injectScancodeSequence` is a
plain loop) and the keyboard paces it. The F-key buttons keep their 50 ms
make-to-break gap, which is a human press, not a workaround.

The BAT itself arrives one frame after power-on, not after the real
self-test's few hundred milliseconds.

### 53.2 Typematic (K2)

Ported from `PC486_REVIEW.md` §40.2: the most recently pressed key repeats after 500 ms at
10.9 cps, `F3` changes both, `F5`, `F6` and `FF` restore them, a grey key
repeats with its `E0`, Pause never repeats, and a break matched on its
code alone ends the repeat. The timing runs in cycles against the 8 MHz
clock. One AT-specific rule from 6523261: "If a key is pressed and held
down while keyboard transmission is inhibited, only the first make code is
stored in the buffer", so no repeats are generated while the clock is held
low.

The repeat period `(8+A) * 2^B * 4.17 ms` (+-20%) and the 250 ms delay steps
come from the IBM PC AT Technical Reference (6139362, Sep 1985) p.4-7,
"Set Typematic Rate/Delay (Hex F3)".

The page drops the browser's `keydown` repeats and tracks held keys.
Leaving the screen (blur, or the tab hiding) sends a break for each, or a
key whose `keyup` went elsewhere would repeat forever.

The Bochs BIOS doesn't implement INT 16h AH=03h (AH=09h reports it
absent), so `MODE CON RATE=` has no effect under the stand-in. Software
that sends `F3` itself works.

### 53.3 Keyboard commands (K3)

`ED` and `F3` ACK their argument too, and a command byte (bit 7 set) in
place of an argument runs as a command. `EE` echoes `EE`. `F2` answers
`FA AB 41`, the Enhanced Keyboard's `AB 83` with the 8042 translating 83h
to 41h; the 339 shipped with the Enhanced Keyboard. `F4` clears the buffer
and starts scanning, `F5` restores defaults, clears the buffer and stops
scanning, `F6` restores defaults and clears the buffer. `F7`-`FD` (Set 3
key types) are ACKed. `FE` resends the last byte the keyboard sent. `FF`
ACKs, resets and sends `AA`. Anything else gets `FE`. `F0` takes its
argument but sets nothing, since only Set 1 is modelled. LED state is
held, not shown on the page.

### 53.4 Status bit 4 and the keylock (K4)

Status bit 4 is the front-panel keylock, 1 = not inhibited (HelpPC, "1=
keyboard enabled, 0=keyboard disabled (via switch)"). It reads 1, since the
keylock is open. ADh and AEh set and clear command byte bit 4 instead of a
private flag, so writing the command byte directly has the same effect, as
on the chip. The keylock itself isn't a control on the page. `pc486` has the
inverted
bit.

### 53.5 Input port and test inputs (K5)

`C0` returns BFh: keylock open (bit 7), colour display (bit 6 low, the
EGA drives an Enhanced Color Display), no manufacturing jumper (bit 5),
512KB on the system board (bit 4, "1=enable 2nd 256K"), and bits 0-3 high,
since they're unconnected and the 8042's quasi-bidirectional port pulls
them up. `E0` returns T0 (keyboard clock) and T1 (keyboard data): 03h
idle, 02h while the controller holds the clock low.

### 53.6 The RTC ticks (R1)

`CmosRtc` is `pc486`'s MC146818A (`PC486_REVIEW.md` §40.1): its own 32.768 kHz time base in guest
time, UIP 244 us before each update, the 1984 us update, BCD and binary,
12- and 24-hour, SET, the divider's half-second, the every-fourth-year
leap rule, the alarm's don't-care bytes, the periodic rate from register
A, and IRQ8 on slave line 0 while an enabled flag is set, until register C
is read. It stays 64 bytes, the 5170's part, so 40h-7Fh alias 00h-3Fh.

The constructor runs `reset()` on it. Without that, registers A, B and D
read 00h, so the divider is off, the clock is in 12-hour mode and the
battery-good bit is clear.

Each power-on loads the visitor's local time (`Machine.setRtc` from
`app.js`), since a real AT's battery kept the clock running. Native
harnesses start at 1986-01-01 00:00:00, a Wednesday.

### 53.7 Checks

- Native tests: the 8042 suite (33) covers the link delay, IRQ1 dropping
  between bytes, the inhibit holding keys, responses jumping the queue, the
  overrun on byte 17, each keyboard command, the input port, the test
  inputs, status bit 4, and the typematic cases, including held keys under
  inhibit. The RTC suite (19) is `pc486`'s at 8 MHz with the 64-byte alias
  and the power-on registers. `chipset_test` checks the periodic interrupt
  reaching IRQ8 and register C dropping it.
- Bochs POST gets through `keyboard_init` (POST code 77h).
- Playwright covers a held key repeating at the prompt and stopping on
  release, browser repeat events not reaching the guest, leaving the screen
  stopping a held key, and `DATE` showing today's date (`rtc.spec.ts`).
- The installer tool's `SendKey` waits 2.5 ms between make and break, well
  over a frame.

## 54. Test parity: a smoke boot, the thin native suites, every control, and coverage

Items X1-X4 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). X5 (review
corrections for §3, §5, and §44.3) is closed: those sections state current
behaviour. §66 has the current totals.

### 54.1 Smoke boots and types (X1)

`smoke.spec.ts` has two cases. One measures real-speed pacing, which needs
the machine running but not booted. The other boots to `C:\>` under the
fast-test multiplier, types `ECHO SMOKE` and waits for COMMAND.COM's output
on its own line: a boot plus one real interaction, as the smoke rules ask.

### 54.2 Test typing and held keys

A key's make and break have to reach the keyboard together. A helper that
sent a key's make, waited 40 ms of wall time, then sent its break would
hold the key about 800 ms of guest time under `fast=1`, past the
keyboard's 500 ms typematic delay (§53.2), and `VER` would type `veer`
whenever a frame boundary landed in the gap.
`tap()` injects make and break in one call and lets the keyboard pace
them, so a tap lasts the same guest time at any multiplier.

The page's own F-key buttons hold for 50 ms, a human press at real speed
(§53.1). Under `fast=1` that also repeats, so the button tests check the
bytes each button sends, and the end-to-end check uses keys where a
repeat is harmless.

### 54.3 Native suites (X2)

- `machine_test` (13): RTC IRQ8 reaches a CPU handler at vector 70h
  through the slave and the master's IR2, with the AT BIOS's ICWs, ten
  times in 10 ms at 1024 Hz. Leaving out either PIC's EOI stops it after
  one, since that PIC's in-service bit holds the next.
- `fdc765_test` (20): SEEK takes 3 ms a cylinder on the 1.2MB drive with
  the drive-busy bit up and reports the new PCN, SENSE DRIVE STATUS shows
  track 0 only at cylinder 0, READ ID returns the cylinder under the head,
  a multi-sector read runs R to EOT on head 1, a 360KB disk transfers at
  half the 1.2MB rate, and a DOR reset abandons a transfer in progress.

### 54.4 Every control (X3)

- `hddLed`: lights during the boot, stays dark at an idle prompt, lights
  for a `DIR` of an unread directory, and goes out at power-off.
- Floppy LEDs: A:'s lights for `DIR A:` and goes out
  when the BIOS stops the motor, and B:'s stays dark. A:'s motor is still
  running down from the boot probe when the prompt first appears, so the
  test waits for it first.
- The F-key and extra-key rows: each button sends its exact Set 1 make
  and break (`E0` pairs, Print Screen's four-byte sequences, Pause's six
  bytes with no break), and the test fails if a button is added without
  an entry. F3 recalls the last line in COMMAND.COM, and Home and End
  move its cursor. Esc is covered in `fullscreen.spec.ts`.
- **Upload image**: a full-size
  image with a marker in its last sector mounts at the next power-on and
  boots, and a wrong-size file gets the geometry alert and isn't mounted.

### 54.5 Coverage (X4)

Coverage is measured on the chip and machine sources and on `app.js`.

- Native (`make coverage`, llvm-cov): 97.8% lines and 93.9% branches over
  the chip and machine sources (§55.6).
- `app.js` (`make -C web coverage`, V8): 85.0% lines, 71.7% branches,
  81.1% functions.

### 54.6 Checks

- The native and Playwright suites run in full, with coverage on one
  worker. §66 has the current counts.

## 55. CPU test coverage

Every reachable region and function in `cpu80286.cpp` runs under a
test.

### 55.1 Coverage includes the timing suite

`make coverage` runs every unit-test binary, including
`cpu80286_timing_test`, which is in the Makefile's `UNIT_TESTS` list. Its
runs count toward §54.5's CPU figure.

### 55.2 AAA and AAS use the 286's arithmetic

"On the 80286 and later processors, the first addition is performed on AX
instead of AL, incrementing the AH register if a carry is generated out of
AL. ... If AX contains 00FFh, executing AAA on an 8088 will leave AX=0105h.
On an 80386, the same operation will leave AX=0205h" (Hummel, PC Magazine
Programmer's Technical Reference: The Processor and Coprocessor, AAA).
AAA adds 106h to AX. AAS takes 6 from AX and then 1 from AH, as in the
Intel SDM's pseudo-code. Hummel's note covers AAA only, and the AAS form
rests on the SDM and on the two instructions sharing the arithmetic. Valid
ASCII digits give the same result
either way. Only an out-of-range AL (FAh-FFh for AAA, 00h-05h for AAS)
tells the two apart.

### 55.3 16-bit IDIV of -2^31 by -1

The 16-bit IDIV computes `int32 / int16` in C++, and `INT32_MIN / -1`
overflows. ARM returns INT32_MIN, which fails the quotient check and
raises #DE as a 286 does, so the native tests pass either way. wasm's
`i32.div_s` traps on that overflow, so in the browser a program dividing
DX:AX = 8000:0000 by FFFFh would abort the module. The divide runs in 64
bits. The 32-bit form checks for `INT64_MIN / -1` first.

### 55.4 LOOPE and LOOPNE timing

All four loop instructions cost 4 clocks when not taken on the 286
(HelpPC 2.10, LOOPE and LOOPNZ: "no jump 5 4 ? 6" for the 8086, 286, 386
and 486; the 4 is the 286 column). TOPBENCH (§47.4) has no tests built on
these loops.

`FE /2` to `FE /7` do nothing. That's open as C13, without a test, so the
behaviour isn't locked in.

### 55.5 What the tests cover

Twelve 286 instructions are covered that line coverage alone would hide:
CBW, CWD, SAHF, LAHF, CMC, STD, XLAT, WAIT, XCHG r/m16, TEST AX imm16,
PUSH imm8 and POP r/m16. In the long `else if (op == ...)` chain the test
and the body share a line, so the line counts as covered even when the
body never ran. Branch coverage shows it. The tests also cover
BCD adjusts in every carry state, every rotate both ways in all three
widths, the Jcc conditions, LOOPE and LOOPNE, the group 3 and group 5
forms in register and memory, the divide errors on both sides, DF=1
strings, REP with CX=0, a fault in the middle of a REP, REP INS yielding
and resuming, the descriptor-table operand checks, and the 0x66 forms the
BIOS ROM uses.

Three conditions that could never be false aren't in the code: the `is_rep`
re-test at the bottom of both REP loops, the lower bound of the INC, DEC,
PUSH and POP ranges (every lower opcode is dispatched first), and
`string_op`'s `default`.

### 55.6 Checks

- `cpu80286.cpp`: 100% regions and
  functions, 99.5% lines, 99.7% branches. The 6 lines are closing braces
  after a `break`, which llvm-cov counts as separate regions. The 4
  branches are C13, a switch with no default whose reachable opcodes all
  have a case, and the prefetch model's safety net in `prefetch()`, which
  only fires if a control transfer forgets `flush()`.
- All native: 97.8% lines, 93.9% branches.
- Playwright smoke, boot, keyboard and floppy specs pass on the
  rebuilt wasm, including FreeDOS FORMAT B:.

## 56. Per-opcode instruction costs

Items T10-T12 from [`IBM_PCAT_PARITY.md`](IBM_PCAT_PARITY.md). §40 gives
the hot groups their own figures, and the opcodes below have theirs too,
in place of the flat `CYC_REG` (2) or `CYC_MEM` (7). Every figure here is
the 286 column of HelpPC 2.10, which follows the iAPX 286 PRM appendix
(the same table §55.4 uses).

### 56.1 The opcodes (T10)

| Instruction | Flat | 286 |
|-------------|----:|----:|
| NOP, XCHG AX,reg | 2 | 3 |
| XCHG reg,reg / reg,mem | 7 | 3 / 5 |
| LEA | 2 | 3 |
| ALU acc,imm, TEST acc,imm | 2 | 3 |
| TEST r/m,reg, memory | 7 | 6 |
| CMP reg,mem and CMP mem,imm | 7 | 6 |
| PUSH imm | 2 | 3 |
| PUSH sreg | 7 | 3 |
| PUSH mem | 7 | 5 |
| POP reg, sreg, mem | 7 | 5 |
| DAA, DAS, AAA, AAS | 2 | 3 |
| XLAT | 7 | 5 |
| BOUND | 7 | 13 |
| IMUL r,r/m,imm | 7 | 21 reg, 24 mem |
| INC/DEC through `FE`/`FF`, register | 7 | 2 |
| JMP / CALL near indirect | 7 | 7 reg, 11 mem, plus the refill tax |
| JMP / CALL far indirect | 7 | 15 / 16, plus the refill tax |

The `FF` transfers set `transfer_` for the prefetch model and pay
`kQueueRefillTax` like every other taken transfer (§41).
CMP's memory forms are a clock cheaper than the other ALU ops because
nothing is written back; CMP mem,reg stays 7, as the table has it.

### 56.2 Shift cost uses the masked count (T11)

`grp2_shift` masks the count once, before the shift and before the 5+n
charge. The 286 masks the count to 5 bits before it runs (Intel iAPX 286
PRM), so n is at most 31, and `SHL AX,CL` with CL=200 shifts and charges
as a count of 8.

### 56.3 Divide errors and BOUND pay for the interrupt (T12)

#DE from DIV, IDIV and AAM 0, and #BR from BOUND, go through
`Cpu::exception()`, which vectors and charges INT n's 23 clocks plus
`kQueueRefillTax`, the same as faults found through `raise()` in
`step()`. They stay off the `raise()` path, which can't carry vector 0
and would roll back registers for no reason.

### 56.4 TOPBENCH

Measured with §47's method: `TOPBENCH.EXE` 0.40c on a scratch copy of the
shipped HDD image, its Realtime screen read from a native probe.

| Test | Real 339 | This build | |
|------|---------:|-----------:|--:|
| MemTest | 923 | 940 | +2% |
| MemEA | 514 | 476 | -7% |
| Opcodes | 397 | 375 | -6% |
| VidMem (Enhanced Display) | 1470 | 1335 | -9% |
| 3DGames | 388 | 368 | -5% |

The corrections go both ways, and POP's two clocks outweigh the rest. So
the flat costs aren't the source of T9's gap, which was a bus-unit
question. (§75 drops T9.)

### 56.5 Checks

- Timing cases cover every row in §56.1, the masked shift count (CL=200,
  and imm 32 as a count of 0), and #DE and #BR paying for the interrupt.
  `Grp1ImmedSignExtended8BitMemory` uses ADD, since CMP is 6.
- The shipped HDD image boots to `C:\>` at 450.3M cycles.
- Playwright smoke (boot and type, real-speed pacing) passes on the
  rebuilt wasm.

## 57. The drawn Enhanced Keyboard

The keys a Mac keyboard lacks, and Ctrl+Alt+Del (§30), sit on a to-scale
drawing of the keyboard the 339 shipped with, the 101-key IBM Enhanced
Keyboard (§53.3: the 8042 answers `F2` with its ID). It sits under the
monitor, shown by the bar's Keyboard box: off by default on a desktop, on
by default on a touch screen with no mouse. The module
is `shared/pc-keyboard.js`, shared with pc486 (PC486_REVIEW.md
§49, which covers the layout, latching and map mode).

- Clicks go through `sendKey()` and the 8042's queue like a real key.
  Ctrl, Alt, Delete on the drawing reboots, so the separate button is gone.
- The lock lamps read `leds()` (§53.3) through the
  `keyboardLeds()` binding.
- This page has the Key Mapper pc486 has, on the drawing, and the bezel
  KEYS button. Mappings persist in `retro8080.ibmpcat.keymap`.
- `keyboard.spec.ts` checks all 101 drawn keys send their Set 1 make and
  break bytes (modifiers on the latch and unlatch clicks).

## 58. No preset hard disk

The machine ships no third-party DOS games. C: is the factory FreeDOS image,
the Disk library holds only FreeDOS packages, and there is no `?disk=`
preset C:. Visitors bring their own floppy and hard disk images.

## 59. Front panel to scale

The fascia is drawn to the real case's proportions, so the drives sit on
it the way they do on a 5170.

- **Case**: 540 mm wide, 162 mm high (IBM announcement letter 184-097).
  It is drawn 540 × 135 mm (`aspect-ratio: 540 / 135`), leaving off the
  plain strip under the grille and the drives. `.at-case` is a size
  container. Everything
  on the right half is in `cqw`, so it scales with the case.
- **Layout**: a raised left panel, a drive recess, and a raised pillar at
  the right edge (*Guide to Operations* 1502490, p. 1-5, and a photo of a
  5170 on Wikimedia Commons, `IBM_PC_AT.jpg`). The left panel holds a small
  IBM badge, the keylock panel with its two square indicator windows, and
  a vertical-slot grille over the left half that stops short of the
  bottom edge.
- **Drive bay**: drive A above B, filling the recess behind a raised lip,
  set high on the case with its lip level with the badge, as in the photo.
  Each drive is a half-high 5.25" face, 41.3 mm high. They are drawn about
  10% wider than the real 146 mm, with the right pillar narrowed to match,
  so the face has room for the readout.
- **Drive face**: Y-E Data YD-380 (1.2 MB) and YD-580 (360 KB), the
  drives the 339 shipped with. The face is light grey (Ingenium museum
  record 1997.0270: "light grey disk drive casing").
- **Lever**: a flat paddle inside a recessed rectangular plate (shape
  from the Commons photo's lower drive). It pivots at its left end, just
  above the slot. Open, it lies flat pointing right. Closed, it hangs down
  across the slot. It turns counterclockwise to open (*Guide to
  Operations* pp. 1-6, 1-8). The slot is notched at its left end and the
  activity light is a small round lamp above the slot's right end.
- Each drive's file name readout and Save button sit on its face, under
  the slot where a paper label would go. Below 640 px they move under the
  case.
- The power switch sits on the right side of a real 5170. Here it is on
  the front panel beside the keylock, as a labelled web control.
- At a case narrower than 520 px, the model line, badge text and switch
  caption drop out and the switch shrinks.

## 60. An empty drive is not ready

A drive with no diskette answers none of READ DATA, WRITE DATA, READ ID
and FORMAT TRACK. If they ended normally, the DMA would move nothing and
DOS would read blank sectors, so `DIR A:` would say "File not found".

A drive with no disk gives no INDEX pulses. The TEAC FD-55 manual's ready
state "means that a disk is inserted, front lever closed, and the disk
rotates normally", and the TEAC FD-235HF spec (8.3.9) gates INDEX with that
ready state. The uPD765 ends those commands only after it sees the index
hole twice (NEC uPD765 data sheet, READ DATA and READ ID), so without a
disk it stays in the execution phase and raises no interrupt. The BIOS
times out waiting for IRQ6 and resets the controller through the DOR, and
DOS reports "Error reading from drive A: DOS area: drive not ready" with
Abort, Ignore, Retry, Fail.

- `Fdc765::run_command()` holds those four commands in execution when the
  drive has no media. A DOR reset ends them.
- `Fdc765Test.AnEmptyDriveNeverEndsAReadUntilReset` covers the controller,
  and `floppy.spec.ts` checks the DOS error and the Abort.
- The change is in `shared/pc/`, so the 486 gets it too (PC486_REVIEW.md §57).

## 61. A faster boot without a faster machine

A real-speed boot takes about 29 s to `C:\>`. The stock FreeDOS 1.3
startup files and one stand-in BIOS prompt would add about 29 s more, and
none of that is hardware. The CPU, disk and floppy timing are untouched.

| Stock timing | Stage | Change |
|---|---|---|
| 0-4 s | POST and drive detect | none |
| 4-7 s | "Press F12 for boot menu" pause | skipped |
| 7-12 s | BIOS tries the empty A:, then C: | none, a real AT does this |
| 12-20 s | FreeDOS kernel and InitDisk | none |
| 20-26 s | FDCONFIG.SYS menu, `MENUDEFAULT=1,5` | `MENUDEFAULT=1,0` |
| 34-52 s | FDAUTO.BAT runs `MEM /C /N` twice on a 286 | both REMmed out |
| 52-55 s | `cdrom.bat display`, though the AT has no CD-ROM | REMmed out |

- **F12 pause.** The stock Bochs BIOS prints "Press F12 for boot menu."
  and waits unless bit 0 of CMOS 3Fh is set (BIOS-bochs-legacy
  disassembly, F000:13AB reads 3Fh through ports 70h/71h and returns early
  when the bit is set). A 5170 has no such prompt, so the patched BIOS is
  built without it (§70.4).
- **Startup files.** `disks/tune-freedos-hdd.py` edits `FDCONFIG.SYS` and
  `FDAUTO.BAT` with mtools when `web/Makefile` copies the installer's image
  into `web/disks/`, so the slow installer image and its CI cache are
  unchanged. It keeps `CTMOUSE` on the 286 path (serial mouse on COM1, §67)
  and REMs `MEM /C /N` plus the CD-ROM probe. Each file gets a dated
  "Changed by oac-emu" line at the top, per GPL v2 section 2(a). No FreeDOS
  program changes, and the menu stays in `FDCONFIG.SYS` for anyone who wants
  it back. The script fails if any line it expects is missing.
- A saved C: that a visitor has changed keeps its own startup files. An
  unchanged one picks up the current factory image (shared/hdd-worker.js
  fingerprints it).
- `boot.spec.ts` checks the served image carries the edits and the notes,
  and that a boot shows none of the removed output.

## 62. TOPBENCH gap against a real 5170-339: no documentable fix

The §47 / §56 calibration gap is measured with the Performance panel (the
486 review's §23 pattern, on this machine) and a scratch native harness
that boots the shipped FreeDOS HDD, mounts `TOPBENCH.img`, and runs
`TOPBENCH /i`. The harness is not in the tree.

| Test | Real 339 | This run | |
|------|---------:|---------:|--:|
| MemTest | 923 | 941 | +2% |
| MemEA | 514 | 476 | -7% |
| Opcodes | 397 | 376 | -5% |
| VidMem (Enhanced Display) | 1470 | 1339 | -9% |
| 3DGames | 388 | 368 | -5% |

Same as §56.4 within a few µs. Suspects from the Intel 80C286 data sheet,
iAPX 286 Programmer's Reference Manual / Hardware Reference Manual, IBM
PC/AT Technical Reference ("System Performance"), and IBM EGA Technical
Reference:

- **EA cost and prefetch overlap.** Note 1's +1 clock for base+index+disp
  is charged (`decode_modrm`). Prefixes are +2. `prefetch()` treats EA and
  other non-bus EU time as idle for code fetch, which matches the HRM Bus
  Unit ("when not performing other bus duties"). No further EA clocks in
  the data sheet.
- **Data vs code sharing the bus.** `idle = exec_clocks - bus_busy_` gives
  the BIU only the clocks the EU is not using the bus; wait states stay in
  `bus_busy_`. The HRM allows Ts directly after Tc (no mandatory idle
  state between cycles). MemEA still sits on the 3-clock-per-cycle bus
  floor plus refresh (§47.4); the real 339 is ~0.85 clocks/instruction
  slower than that floor. Nothing in those documents accounts for it.
- **EGA cycle length beyond the 8-bit 6 clocks.** The AT TR's 6-clock
  8-bit cycle is already the floor in `byte_clocks`. Sequencer slots are
  still the derived 32 dots / 5 (§47.3); the EGA TR states the 4-of-5 /
  2-of-5 split but not the cycle length in dots. Stretching a granted
  slot to 6 clocks isn't done: callers already advance time by the
  charged length, so it would only slow medium-res without a source.
- **339 RAM wait state on code fetches.** `mem_clocks` is 3 for board RAM
  for both data and prefetch. MemTest (data-bound strings) is within 2%,
  so the wait state itself is right; applying it to code is what the TR's
  undifferentiated "bus cycle" wording requires.

MemTest is already inside ~2%. MemEA, Opcodes, VidMem and 3DGames are not,
and no primary source found closes them without a fitted constant. They
stay open as E20 in `IBM_PCAT_PARITY.md`. (§75 drops T9.)

## 63. Protected mode and extended memory

Parity items C10 (protected mode) and P8 (extended memory). LOADALL /
STOREALL are open as C14 (`IBM_PCAT_PARITY.md`). The 512KB base-memory
choice is §71 (P13) and §76.2 (P14). Sources: Intel iAPX 286
Programmer's Reference Manual (1987), chapters 6-9 and the instruction
pages, timing appendix for clocks; IBM PC/AT Technical Reference for the
memory map, shutdown and bus timing; the shipped `BIOS-bochs-legacy` ROM,
disassembled, for what the stand-in firmware does.

### 63.1 Descriptor caches

Each of ES/CS/SS/DS has a hidden base (24 bits), limit and access byte.
Every operand access checks offset+size-1 against the cached limit, so the
real-mode FFFFh overrun faults are the same check with limit FFFFh. Real
mode loads set the base only. RESET leaves CS's base at FF0000h. A host
that writes a segment
field directly between steps (the tests, `bios_test`) is picked up as a
real-mode load at the next step.

### 63.2 What protected mode covers

- 286 descriptors: word 0 limit, 24-bit base, access byte, bytes 6-7
  ignored. System types 1-7 only; 8-15 (386 types) are invalid.
- Segment loads with #GP/#NP/#SS/#TS, null selectors in DS/ES that fault
  on use, the accessed bit written back, expand-down data, execute-only
  and read-only checks.
- Far JMP/CALL/RET/IRET through code segments, call gates (with parameter
  copy and the inner stack from the TSS), task gates and TSSs. An outward
  RET/IRET nulls DS/ES the outer level can't use.
- IDT delivery through interrupt, trap and task gates with error codes
  for 8 and 10-13, EXT in error codes raised during delivery, gate DPL
  checked for software INT only.
- Two contributory faults (0, 10-13) make #DF; a fault delivering #DF is
  shutdown. The AT board pulses RESET on the shutdown cycle, so `Machine`
  resets the CPU (memory and CMOS survive). Real mode raises exception 8
  for a vector past the IDT limit.
- 286 TSS (44 bytes) task switches: state saved and loaded, busy bits, NT
  and the back link, MSW.TS set. Faults after the switch commits belong to
  the new task.
- SLDT/STR/LLDT/LTR/VERR/VERW, LAR/LSL, ARPL. IOPL gates IN/OUT/INS/OUTS/
  CLI/STI and LOCK. CPL 0 gates HLT, LGDT/LIDT/LMSW, CLTS, LLDT, LTR.
  POPF/IRET load IOPL only at CPL 0 and IF only when CPL <= IOPL.
- LMSW sets PE and can't clear it.

Clocks: the published protected-mode counts, charged as the difference
from the real-mode form the opcode already pays (MOV Sreg 17/19, POP Sreg
20, LDS/LES 21, JMP far 23/38/175/180, CALL far 26/41/82/86+4x/177/182,
RET far 25/55, IRET 31/55/169, INT n 40/78/167). Indirect forms reuse the
direct-form difference, which is 1-4 clocks off in a few cases.

Approximations: stack-room faults on a privilege change report #SS(0)
from the push rather than #SS(selector); a task switch through a gate with
a bad TSS selector reports #GP rather than #TS.

### 63.3 Leaving protected mode

A 286 can't clear PE with `LMSW`; only RESET leaves protected mode (Intel
iAPX 286 PRM, LMSW). IBM's AT BIOS returns through the 8042 reset and the
CMOS shutdown byte, and the 286 build of the stand-in BIOS does the same
(§70.2). The core treats `66h` and the 386 `0F` opcodes as invalid
(§70.1).

### 63.4 Extended memory

`Chipset(ext_kb)` decodes RAM from 100000h up, stopping below the ROM
alias at FF0000h (15MB - 64KB at most). Adapter memory is a 16-bit I/O
channel device, so it takes the 3-clock cycle the TR gives 16-bit
devices. `configure_factory_cmos()` writes the size to 17h/18h and
30h/31h, as Setup and POST would; INT 15h AH=88h reads them (E801h is
compiled out, §70.1). A real owner ran the Setup diskette after fitting a card so
CMOS matched it; writing CMOS directly stands in for that.

Base memory is 640KB by default, or 512KB (§76.2). POST takes it from CMOS
15h/16h and writes it to BDA 40:13, and INT 12h returns that word (§71.1).

The page offers none, 1MB, 1.5MB, 2MB, 3MB and 4MB, chosen while
powered off and remembered in the browser. With extended memory fitted the
shipped FreeDOS menu's FDXMS286 loads, DOS=HIGH puts the kernel in the
HMA, and XMSTEST's fill/verify/shift pass completes through the BIOS block
move.

### 63.5 Checks

- `cpu80286_pm_test`: 35 cases over loads, limits, gates, privilege,
  task switches, #DF/shutdown, the system instructions and their clocks.
- `bios_test`: the real ROM's INT 15h AH=88h and an AH=87h copy into
  extended memory, back in real mode afterwards.
- `chipset_test` / `machine_test`: decode, timing, the alias cap, CMOS
  words and checksum, shutdown resetting the CPU.
- `memory.spec.ts`: the control's lock and persistence, MEM with and
  without extended memory, XMSTEST.
- Mutations: no limit check, CMOS 30h left zero, app.js always fitting
  none. Each turns its tests red.

### 63.6 Unsourced

- Reset access bytes: 93h (present, writable data) for ES/SS/DS and 9Bh
  (present, readable code) for CS. They are the rights that make every
  real-mode access legal; not yet checked against the PRM's reset state.
- Extended-memory timing: adapter RAM is charged the TR's 16-bit device
  cycle (3 clocks, as port 1F0h). No IBM memory option's own timing is in
  hand.

## 64. Performance panel (`?perf`, `IBMPCAT_PERF`)

Same shape as the 486: a second wasm binary built with `IBMPCAT_PERF`
(`make -C ibmpc-at/web ibmpcat-perf.js`) carries the emulator's own
counters on the hot paths. `?perf` probes for `ibmpcat-perf.js` and loads
it when present (Tier 2 / "Instrumented build"); otherwise it keeps the
shipped binary and shows Tier 1 host metrics only ("Shipped build"). The
shipped page never links the perf binary. Deploy CI builds both;
`nightly-full.yml` also re-runs `performance.spec.ts` with the perf
binary hidden so the fallback stays covered. Four Playwright cases in
`performance.spec.ts`.

## 65. Shared front-end modules

Front-end modules this machine shares through `shared/`:

- **Themes**: five themes via `shared/theme.css`, `theme-init.js`,
  `theme-picker.js` (`win`, `winxp`, `web94`, `subtle`, `aurora`).
- **HDD persistence**: `shared/hdd-worker.js?model=WD1003&db=ibmpcat-hdd`
  (chunked sectors, factory fingerprint, legacy `c-drive` conversion;
  §61). `hdd-convert.js` covers old-format uploads.
- **Disk library / floppies**: `shared/disk-library.*`, `floppy-store.js`,
  `blank-floppy.js`, `disk-drop.*`. `library-images` builds the library
  floppies from `disks/library.json` via `shared/dos-library/` (needs
  mtools; nightly). Specs: `library.spec.ts`, `floppy.spec.ts`,
  `hddstorage.spec.ts`.
- **Keyboard / paste**: shared `pc-keyboard.js` (keymap key
  `retro8080.ibmpcat.keymap`, §57) plus paste into the guest
  (`paste.spec.ts`).
- **Boot notice / notices / analytics**: Barebones FreeDOS boot notice
  (`bootnotice.spec.ts`), footer third-party notices
  (`notices.spec.ts`, `footer.spec.ts`), GoatCounter via
  `shared/analytics.js` with `data-machine="ibmpc-at"`.

## 66. Tests and CI

Current totals (2026-10-27): about 631 native GoogleTest cases (`ctest`
over `ibmpc-at/tests/build`, including `shared/pc/tests` this machine
links) and about 120 Playwright cases across 21 specs. Per-suite samples:
`machine_test` 15, `fdc765_test` 26, `cpu80286_pm_test` 35, `i8042_test`
34. Prefer `ctest -N` and the spec files over hard counts in older
sections.

- **Playwright ports**: serve `8300`, suite `8310`. Workers: 1 with
  coverage, 2 on CI, 3 locally.
- **Smoke vs full**: `make -C ibmpc-at test-smoke` and
  `make -C ibmpc-at/web test-smoke` (the latter is `smoke.spec.ts` only).
  Deploy CI (`deploy-emulator.yml`) runs smoke; `nightly-full.yml` runs
  `make check` / `make test`. Nightly `ibmpcat-web-test` goes through
  `pick-runner` (self-hosted `retro-mini`, ubuntu fallback with
  `SLOW_HOST=1`); wall-clock-sensitive specs skip on `SLOW_HOST`.
- **`fast=1` contract**: gated in `app.js`, defaulted by `boot()` for
  non-realtime specs, stripped from staged `app.js` (`STAGE_POST` +
  root `--check` + `check_site.py`). Real-speed smoke bounds: 4M–8.8M
  cycles/s via `wallWait` (§36).
- **Build deps**: `nasm` for egabios; `mtools` for `tune-freedos-hdd.py`,
  library floppies, and related disk tools. HDD image cached in CI
  (§11).

## 67. COM1 serial mouse and LPT1 print capture

Parity item P7. Sources: IBM PC/AT Technical Reference (Serial/Parallel
Adapter), NS16450 data sheet, Centronics handshake as described for the
printer adapter, Microsoft serial mouse 3-byte packet (and CuteMouse as a
guest driver, not as a silicon reference).

### 67.1 NS16450 on COM1

`shared/pc/ns16450` at 3F8h. DLAB, loopback, IIR priority, MCR OUT2 gates
IRQ4. RX bytes are paced at the programmed baud × frame length; idle time
does not bank bit credit for a later `push_rx` burst, which would overrun
CuteMouse's one-byte RBR and leave the cursor stuck. Reading the LSR
clears OE/PE/FE/BI (NS16450 data sheet); a sticky OE would make CuteMouse
skip its Microsoft packet parser after any overrun. The stand-in BIOS
probes the port and writes 3F8h into BDA 40:00.

### 67.2 Serial mouse

`SerialMouse` watches DTR/RTS, sends `'M'` on power-up, then Microsoft
3-byte packets from `injectSerialMouse`. Motion accumulates while a report
is still in the UART queue (~40 reports/s at 1200 7N1), so a fast browser
pointer does not backlog old packets behind the wire. The UART is always
fitted; the page's Mouse checkbox (default off, `retro8080.ibmpcatMouse`)
only gates pointer-lock capture, matching the 486 UX. `FDAUTO` runs
`CTMOUSE` on the 286 path (no `/P`). A 1.2MB library floppy
(`disks/build-ctmouse-floppy.sh`) ships the driver plus `MOUSETST`.

### 67.3 LPT1 capture

`shared/pc/lpt` at 378h. Always fitted. A STROBE rising edge appends the
data latch and pulses /ACK immediately (BIOS-bochs-legacy INT 17h samples
status bit 6 once after the pulse). BUSY holds for ~150 cps on the
status port but does not block the next strobe. After the capture stops
growing for a short idle, a dialog offers Download (`ibmpcat-lpt1.prn`) or
Discard. Capture is in-memory only and clears on reset. IRQ7 is wired and
gated by control bit 4. POST writes 378h into BDA 40:08.

### 67.4 Checks

- `ns16450_test` (including idle credit), `lpt_test`, `serial_mouse_test`,
  chipset COM/LPT IRQ.
- `bios_test` PostFillsCom1AndLpt1InTheBda.
- `mouse.spec.ts` (UI plus CuteMouse INT 33h mickeys after inject),
  `lpt.spec.ts` (including `ECHO …>PRN` and Print Screen → LPT1).

## 68. The BIOS built from source, and paced hard disk writes

Parity item S10. Sources: WD1003-WA2 OEM manual (Write Sector), the
pinned `rombios.c` (bochs-emu/Bochs@ff17a0c2), dev86 0.16.21
(lkundrak/dev86@c6f36cba).

### 68.1 Building BIOS-bochs-legacy

Fixing the parity items that live in the BIOS means changing it, so
`roms/bochs-bios/` builds it (§6 covers the prebuilt pin the 486 keeps).
`fetch-bios.sh` fetches `rombios.c`, `rombios.h`, `apmbios.S`,
`biossums.c` and the dev86 tarball, all SHA-256 pinned. The Makefile builds
bcc and as86 from that tarball with the host compiler, applies
`patches/*.patch` in order, and runs Makefile.in's `BIOS-bochs-legacy`
rule (`-DLEGACY`, `bcc -0`, `as86 -j -O`, `biossums`) with the build date
fixed at the pinned binary's `08/23/26`. The 486 keeps the prebuilt pin.

The unpatched rebuild isn't byte-identical to the pin. as86's branch
relaxation picks a 2-byte `EB` jump at 9FAh where the pinned build used the
3-byte `E9` form, and everything after it shifts by one. dev86 0.16.17,
0.16.21 and the current linux86 source all give the same result here, so the
pinned binary came from a different as86 build. The disassemblies match
apart from that jump, and the unpatched rebuild passes the native tests.

Patch 0001 is the LGPL 2.1 §2(a) change notice in `rombios.c`.
`publish_sources.sh` ships `bochs-bios/Makefile` and `patches/` in the
third-party-sources release.

### 68.2 Writes wait for the platter (S10)

The WD1003 takes one sector into its buffer, holds BSY while that sector's
slot comes under the head, then raises DRQ and IRQ14 for the next one, or
completes. `Wd1003` does that with the same `sector_due()` timing reads
use (§52.4). Bad blocks, IDNF and WRITE LONG's stored ECC are handled per
sector.

In the stock BIOS `ata_cmd_data_io()` waits for BSY to clear only after a
read, and on a write reads status straight after `outsw`, so a busy
controller fails it (§51.2). Patch 0002 makes it wait in both directions.
Patch 0007's IBM disk code (§71.1) replaces that driver and waits on IRQ
14 for each sector.

A full FreeDOS install under the patched BIOS and paced writes completes
and leaves the same 1,399 files on C:.

### 68.3 Checks

- `wd1003_test` WriteHoldsBsyUntilEachSectorPassesTheHeadThenInterrupts.
- `bios_test` Int13h03hWritesThroughThePacedController: a real INT 13h
  AH=03h through the patched BIOS. Without patch 0002 it returns AH=0Ch
  with CF set. The test loads the EGA BIOS, since POST stops at "No
  VGABIOS ROM" before it detects the disk.

## 69. Floppy data rate, the head position, and SPECIFY

Parity items S7 and S9. Sources: NEC uPD765A data sheet (READ DATA, READ
ID, SPECIFY, status registers), WD1003-WA2 OEM manual (Floppy Control and
Digital Input registers).

### 69.1 The controller

`fdc765` reads only what the drive under it can:

- **Data rate.** HD media reads at 500 kbit/s. DD media reads at 300 in
  the 360 RPM 1.2MB drive and 250 in the 300 RPM 360KB drive, and is paced
  at that rate. An HD disk never
  reads in the 360KB drive. At the wrong rate no address mark decodes, so
  the command ends with ST1 MA after the index hole passes twice.
- **Head position.** READ DATA and READ ID see the track under the head,
  not the command's C. The 1.2MB drive's 96 tpi head reads 48 tpi media on
  even tracks only: physical track 2N is media track N. An odd track ends
  with MA, which is a choice, since the head sits between two 48 tpi tracks
  there. An ID whose C differs from the command's ends with ND and WC (BC
  for C = FFh), and a sector past the track's last ends with ND.
- **SPECIFY.** SRT steps at 16 - SRT ms at 500 kbit/s, stretched by
  500 / rate at the lower rates. HUT and HLT aren't modelled.
- ST0 carries the head and unit bits on every termination, and READ ID
  raises INT when its result is ready.

FORMAT TRACK places sectors by their IDs wherever the head is. pc486
selects between the two behaviours behind `lenient_media`
(PC486_REVIEW.md §58).

### 69.2 The BIOS (patch 0003)

Stock `rombios.c` never seeks, never sends SPECIFY, never writes 3F7h, and
"senses" media by assuming it matches the CMOS drive type, with a 500
kbit/s rate for every type. Patch 0003:

- `floppy_prepare_controller()` sends SPECIFY from the INT 1Eh table and
  writes the media's rate (media state bits 7-6) to 3F7h.
- `floppy_media_sense()` recalibrates and issues READ ID. The 1.2MB drive
  tries 500 kbit/s, then 300 with double stepping (media state 15h, then
  74h). The 360KB drive uses 250 (93h). A timeout means no disk, and
  INT 13h returns 80h (not ready). No rate that reads gives 0Ch.
- `floppy_seek()` seeks before every read, write and format, doubling the
  cylinder when media state bit 5 is set, and skips the seek when 40:94h
  already holds it.
- `floppy_media_known()` re-senses when DSKCHG is set, so swapping a 360KB
  disk for a 1.2MB one in A: works.
- A failed read or write returns 02h, 03h, 04h, 40h or 20h from ST1/ST2
  and clears "media established". A write error returns instead of
  halting in `BX_PANIC`.
- INT 13h AH=00h (diskette or fixed disk) clears the recalibrated bits
  rather than writing 0 to 40:94h without moving the head. The read and
  format paths don't write 40:94h.

SPECIFY reads the INT 1Eh table, which is IBM's DISK_BASE (§76.1): SRT D,
so 3 ms a step at 500 kbit/s and 5 ms for 360KB media at 300 kbit/s. AH=04h
(verify) reads (§70.3).

### 69.3 Checks

- `fdc765_test`: EachMediaReadsAtItsOwnDataRate,
  TheWrongDataRateFindsNoAddressMarkAfterTwoRevolutions,
  A360KDiskInTheHighDensityDriveReadsOnlyOnEvenTracks,
  ReadIdReportsTheMediaTrackNotTheHeadPosition,
  AHighDensityDiskInThe360KDriveNeverReads,
  SpecifyStepRateStretchesAtLowerDataRates.
- `bios_test` BiosFloppyTest: 1.2MB and 360KB media in A:, 360KB in B:,
  a 360KB-to-1.2MB swap, a 1.2MB disk in B: (0Ch) and an empty drive
  (80h), all through INT 13h AH=02h. Without patch 0003 the five reads
  fail.

## 70. A 286-only BIOS, verify that reads, and boot order

Parity items D1, D4 and S12, and the removal of the 386 layer (§44.3).
Sources: Intel iAPX 286 Programmer's Reference Manual (LMSW, SMSW; only
RESET leaves protected mode), IBM PC/AT Technical Reference (CMOS shutdown
byte 0Fh, 8042 command FEh, BOOT_STRAP), Intel 8237A-5 data sheet (verify
transfer).

### 70.1 Patch 0004: 286 code only

Stock `BIOS-bochs-legacy` is built for a 386. Patch 0004 builds it for a
286 instead, so `cpu80286` needs no 386 layer (§44.3):

- `rombios.h`: `BX_CPU` 2; PS/2 mouse, PCI BIOS, APM, PnP BIOS, SMM and
  El Torito off.
- bcc's long helpers (`lcmp`, `lmul`, `ldivul`, shifts, `linc`/`ldec`,
  `ltst`) are written with 16-bit registers, as are `_write_dword`, the
  IVT setup and `timer_tick_post`. INT 08h's tick moves out of the fixed
  `.org FEA5h` block into `int08_tick`, since the 16-bit form is longer.
- INT 15h AH=86h waits on port 61h bit 4 (refresh) toggles, CX:DX
  microseconds at 15 us a toggle. E820h and E801h are compiled out and
  answer 86h, as on the AT.
- INT 15h AH=89h sets PE with `SMSW`/`LMSW`. INT 70h saves registers with
  `PUSHA`. The stock 386 forms would raise #UD on a 286 at an RTC
  interrupt or a protected-mode switch.
- ATA detection always picks 16-bit PIO, INT 13h DL >= E0h doesn't
  reach the CD-ROM code, and no `$PnP` header is built or handed to
  option ROMs.

`short_jcc.py` assembles until as86's `-j` leaves no `0F 8x` Jcc rel16:
each becomes a short inverted Jcc over a near JMP, with the bytes spelled
out, since as86 makes any forward branch long. It also fails the build when
code runs into a later `.org`, which as86 accepts silently.

Left in the binary but unreachable: `memcpyd` (no callers) and the 32-bit
ATA PIO and ATAPI loops (mode is never PIO32). A scan of the listing for
66h, 67h and the 386-only 0Fh opcodes finds nothing else.

### 70.2 Leaving protected mode (D1)

AH=87h saves SS:SP at 40:67h, writes 09h to CMOS 0Fh, enters protected
mode with `LMSW`, copies with `REP MOVSW`, then pulses reset through the
8042 (`OUT 64h,FEh`). POST checks the shutdown byte first. On 09h it
restores SS:SP from 40:67h and returns to `block_move_return`, which pops
the caller's registers. This is the IBM sequence (§63.3).

The core treats `66h` as an invalid opcode. The 386 0Fh opcodes and
every unknown one-byte opcode raise #UD wherever they run, ROM included.

### 70.3 INT 13h verify reads (S12)

Stock AH=04h returns success at once. Patch 0005 runs it through the read
path with DMA mode 42h (verify) instead of 46h: the controller reads the
sectors and the 8237 counts them without writing memory. A missing sector
returns 04h, the wrong rate 02h, as for a read.

### 70.4 Boot order (D4)

With El Torito off, the BIOS's "Press F12 for boot menu" prompt and its
CMOS 3Dh boot sequence are gone. The stock path that remains tries C:
first unless CMOS 2Dh bit 5 is set. Patch 0006 tries A:, then C:, with no
setting. So `configure_factory_cmos()` writes neither 3Dh nor 3Fh, which
are reserved bytes on the AT.

### 70.5 Checks

- `bios_test`: Int15h87hBlockMove... (through the 8042 reset),
  Int15h89hReturnsInProtectedModeThroughLmsw,
  Int15h83hPostsItsFlagFromTheRtcInterrupt, Int15hE801IsUnsupported,
  VerifyReadsTheSectorWithoutTouchingMemory, VerifyOfAMissingSectorFails,
  BootsFromDriveABeforeTheHardDisk, BootsTheHardDiskWithDriveAEmpty.
- Mutations: with `PUSHAD` in INT 70h and `MOV CR0` in AH=89h, both INT
  15h tests fail.
  Without patch 0006 the A:-first test fails.
- `cpu80286_test` Opcodes386RaiseInvalidOpcode and `machine_test`
  Opcodes386RaiseInvalidOpcodeEvenFromRom.
- A full FreeDOS install on the final BIOS completes with the same 1,399
  files on C: and an identical COMMAND.COM, and the Playwright suite
  passes.

## 71. Fixed disk BIOS from CMOS drive types, and no EBDA

Parity items P13 and D2. Sources: IBM PC/AT Technical Reference 6183355
(Mar 1986, bitsavers `pdf/ibm/pc/at/`): the CMOS map and fixed disk type
tables (pp. 1-62 to 1-65), the 11/15/85 DISK module (DISK_SETUP, DISK_IO,
CHECK_ST, CHECK_ER, CHECK_DMA, WAIT, HD_INT), FD_TBL in ORGS, and
BOOT_STRAP. WD1003-WA2 OEM manual (Mar 1986) for the command set.

### 71.1 Patch 0007: the IBM fixed disk interface

Stock `BIOS-bochs-legacy` finds the disk with ATA IDENTIFY, keeps its state
in a 1KB EBDA at 9FC0:0, builds the INT 41h/46h tables there from CMOS
type 47, keeps its boot-device table at 9FF0:0, and tells DOS it has
639KB. Patch
0007 turns the ATA driver and the EBDA off and replaces the hard disk code
with the interface the TR's DISK module documents, written fresh in the
BIOS's C:

- **Drive types.** CMOS 12h holds C: and D: in its nibbles, Fh meaning
  the extended byte at 19h/1Ah (16-47). The table sits at F000:E401 as on
  IBM's ROM, 16 bytes a type, types 1-23 from the TR and 15, 24-47 zero.
  INT 41h/46h point into it. `configure_factory_cmos()` writes 80h: the
  ST-4038's 733/5/17 with no precompensation and landing zone 733 is IBM's
  type 8. The type-47 bytes at 1Bh-23h aren't written; the TR marks 1Bh-2Dh
  reserved.
- **INT 13h, DL >= 80h.** Functions 00h-05h, 08h-0Dh, 10h, 11h, 14h and
  15h, and 01h (bad command) for the rest, as in DISK_IO's table. Each
  call reads the parameters through INT 41h/46h, sends the precomp
  cylinder / 4 and the control byte, and writes the task file in CHS.
  Reads, writes and seeks wait on IRQ 14 through INT 15h AX=9000h and the
  40:8E flag, which INT 76h sets before INT 15h AX=9100h. Status and the
  error register map to IBM's codes, highest error bit first. AH=08h
  leaves the last cylinder for diagnostics (731 for type 8), AH=15h counts
  cylinders - 1, AH=01h returns the last status in AL and clears it.
  Sectors that would cross 64KB return 09h. AH=00h also resets the
  diskette controller. Read and write long move the 4 ECC bytes.
- **POST.** Disk state lives in the BDA (40:74-40:77, 40:8C-40:8E). POST
  reads 12h, sets the vectors and 40:75, runs the controller diagnostic
  (1782 on failure), then per drive INT 13h 09h and 11h, retried for 60
  seconds (1780/1781), and a verify of the last sector, stepping back on
  errors (1790/1791). A failure on C: sets the HF_FAIL bit in CMOS 0Eh,
  and INT 19h then skips C:.
- **Memory.** POST writes CMOS 15h/16h to 40:13, so DOS sees 640KB, and
  512KB in CMOS gives 512KB. 40:0E stays 0, INT 15h AH=C1h answers 86h,
  and the PS/2 mouse IRQ 12 handler is compiled out.
- **Boot.** INT 19h tries A: then C: with no table in RAM, and INT 18h,
  ROM BASIC on the AT, prints "No more boot devices". The rest
  of the Bochs INT 19h stays: one try a drive, the 55AA check on floppies,
  its messages. IBM's BOOT_STRAP retries, checks the first byte and fill
  pattern instead, and prints nothing (parity D7).

### 71.2 The WD1003 without ATA concessions (D2)

With the BIOS speaking CHS, `wd1003.cpp` has no ATA concessions: ECh
(IDENTIFY) aborts like any unknown command, Drive/Head bit 6 doesn't
select LBA, and the cylinder registers don't float after a reset with
drive 1 selected. A command to drive 1 aborts with DRDY clear, as the
manual gives for a drive without DRIVE READY-.

Two changes come from what IBM's code expects of the board:

- After a soft reset the error register holds 01h, the no-error
  diagnostic code. DISK_RESET fails with 05h otherwise.
- The sector count counts down as each sector is done, and FORMAT TRACK
  leaves it 0. The write path reads it after the last interrupt and
  reports BBh for anything left.

### 71.3 Checks

- `bios_test`: PostTakesBaseMemoryFromCmos, Int41hPointsAtType8InTheRomTable,
  AnExtendedDriveTypeComesFromCmos19h,
  Int13h08hLeavesTheLastCylinderForDiagnostics,
  Int13h15hCountsAllButOneCylinder, Int13h01hReturnsTheLastStatusInAl,
  Int13hReadLongReturnsTheEccBytes, Int15hC1hIsUnsupportedWithoutAnEbda,
  and the earlier INT 13h write and boot-order tests, now through the new
  code. Tests that boot with no disk clear CMOS 12h first, as an owner
  would, since a type with no drive retries for 60 seconds.
- `wd1003_test`: SoftResetLeavesTheNoErrorDiagnosticCode,
  CommandsToTheMissingDrive1Abort, ECh in UnknownCommandsAbort.
  `machine_test` checks 12h is 80h and 19h-2Dh are 0.
- Mutation: without patch 0007 the eight new BIOS tests fail, with the
  three older disk tests.
- A full FreeDOS install on the new BIOS and controller completes with the
  same 1,399 files on C: and an identical COMMAND.COM. The Playwright suite
  passes with the old image and with the new one.

## 72. Hard disk seeks take time (S8)

Sources: Seagate ST4038 product manual (36012-001D, Mar 1986, bitsavers
`pdf/seagate/mfm/ST-4038/`) 1.2 and 4.3; WD1003-WA2 OEM manual (Mar 1986)
Table 5-2, 5.2.1 and 5.2.3. CheckIt's hard disk benchmark times seeks, and
instant ones read as 0.8 ms average and track seeks, both just INT 13h
overhead.

### 72.1 The drive

The ST4038 is a voice-coil drive with a servo surface. With step pulses
10-70 us apart it buffers them and seeks in one move. The manual gives
three numbers for that, all maximums: 11 ms track to track, 40 ms average
over a 243-track (one-third stroke) seek, and 85 ms full stroke. It gives
no curve. `Wd1003::seek_cycles()` uses a + b*sqrt(n) + c*n through those
three points (a = 10.17 ms, b = 0.755 ms, c = 0.0743 ms a track): the
square root for the acceleration-bound short seeks, the linear term for
the coast on long ones. It is fitted to maximums, so it is a slow drive
within spec, not a measured one. A measured profile from a real ST4038
would replace it.

Pulses 3 ms or more apart are slow-step: each settles within 11 ms, and a
multi-track move follows the pulse rate. That is modelled as (n - 1) x
the rate + 11 ms. The manual says pulses 70 us to 3 ms apart may be lost;
those rates (0.5-2.5 ms) are treated as slow-step instead. Head switches
cost nothing.

### 72.2 The controller

- The low nibble of RESTORE and SEEK sets the step rate for implied seeks
  (Table 5-2): 0 is 35 us, 1-Dh are 0.5-6.5 ms, Eh 3.2 us and Fh 16 us.
  0, Eh and Fh are buffered. IBM's BIOS sends 10h and 70h, so it gets
  buffered seeks.
- SEEK holds BSY for the seek, then interrupts. Reads, verifies, writes
  and formats seek to their cylinder first, and a multi-sector transfer
  that crosses a cylinder steps one track. The sector wait starts where
  the platter is when the seek ends.
- RESTORE issues each step only after SEEK COMPLETE (5.2.1), so it takes
  11 ms a cylinder back to track 0.
- After reset or DIAGNOSE the rate is 6.5 ms, and a transfer before any
  RESTORE or SEEK recalibrates first (5.2.3). IBM's POST sends RESTORE,
  so this only shows on software that resets the controller and reads
  straight away.
- Heads start at cylinder 0 at power-on and stay put across a soft reset.

### 72.3 Checks

- `wd1003_test`: SeekHoldsBsyThenInterrupts,
  BufferedSeeksMeetTheSt4038Maximums, SlowStepRatesStepAtTheirOwnPace,
  RestoreWaitsForSeekCompleteOnEveryStep, ReadsSeekToTheirCylinderFirst,
  ATransferBeforeAnyStepRateRecalibratesFirst. The rotation tests seek to
  their cylinder first so they still measure only the platter.
- Mutation: with seeks and RESTORE instant, the first five fail.

## 73. A CPU reset keeps the cycle clock

The EGA grants CPU slots against `Cpu::cycles`. The 286 BIOS returns
from an INT 15h AH=87h block move through shutdown code 09h, so the board
resets the CPU (patch 0004). RESET doesn't stop the system clock, so
`Cpu::reset()` leaves `cycles` alone. The EGA keeps the slot it last
granted on that same timeline. Zeroing `cycles` would leave the slot ahead
of the clock, and the next video access would wait out the whole gap.
Under about 268 s of uptime the clock would jump forward that far. Past
2^31 cycles the wait would overflow `int`, `total_cycles_` would go
backwards, and the EGA raster loop would spin on a 2^64-cycle delta, which
freezes the page, as seen at the end of the Oregon Trail install.
`machine_test` ACpuResetKeepsTheClockTheEgaSlotsAgainst resets through the
8042 after two minutes of uptime and checks the next EGA write costs a few
cycles. With the counter zeroed it jumps 1.04G cycles and fails.

## 74. Floppy lever under the Subtle theme, and PrintScreen in the keyboard spec

- The Subtle theme's `button:active` nudge (`translateY(1px)`) would
  replace the closed lever's `rotate(90deg)` while the mouse is down, so
  pressing the lever's lower half would swing it open and shut before the
  click lands. The loaded lever keeps its rotation under `:active`.
  `floppy.spec.ts` "pressing the lever's far end keeps it closed until the
  click ejects" presses at 90% of its height under Subtle and checks the
  transform; it fails without the rule.
- The drawn PrintScreen key makes DOS print the screen, and LPT1 capture
  (§67) then opens its modal dialog, which would block every later key
  click in `keyboard.spec.ts`, so the spec expects that dialog and
  discards it.

## 75. Gaps only real hardware could close

Dropped from the parity backlog: nobody here will measure a real 5170,
286 or EGA, so these stay as they are.

- Old T9: fetch-bound code runs 5-7% fast against TOPBENCH's 5170-339
  (§47.4, §56.4, §62).
- Old E18: write mode 3 runs as VGA's; the EGA TR calls it not valid.
- Old S13: the ST4038 seek curve is fitted to its three spec maximums
  (§72.1), not measured.

## 76. IBM's diskette tables, and a 512KB board

### 76.1 Patch 0008: DISK_BASE and the media tables (S11)

The IBM PC/AT Technical Reference 6183355 BIOS listing has DISK_BASE at
`ORG 0EFC7H` in the ORGS module: `DF 02 25 02 0F 1B FF 54 F6 0F 08`
(SRT D, head unload F, DMA, motor-off 25h, 512-byte sectors, EOT 15, gap
1Bh, DTL FFh, format gap 54h, fill F6h, 15 ms settle, 1 s motor start).
Stock Bochs has a 1.44MB table there (AFh, 18 sectors, 6Ch) and points INT 1Eh
at a second copy. Patch 0008 puts IBM's bytes at EFC7 and points INT 1Eh
at them, as the AT does.

The DISKETTE module's MD_TBL1-6 are the 13-byte per-media tables (the 11
bytes plus max track and data rate). DISK_PARMS (AH=08h) returns the one
for the drive's CMOS type, 1.2MB-in-1.2MB for type 2 and 360KB-in-360KB
for type 1. SET_MEDIA (AH=18h) returns the one for the requested format,
so 360KB in the 1.2MB drive gets MD_TBL2 (300 kb/s, gap 2Ah, format gap
50h). Stock Bochs returns its 1.44MB table for both. Its acceptance rules for
AH=18h are unchanged; only ES:DI follows IBM. Types IBM's table lacks
get ES:DI = 0, as DISK_PARMS's no-drive path does.

SPECIFY reads the INT 1Eh table (§69.2), so floppy steps use IBM's
SRT D: 3 ms at 500 kb/s, 5 ms for 360KB media at 300 kb/s.

### 76.2 A 512KB board (P14, machine side)

`Chipset` and `Machine` take a base memory size of 512 or 640KB. At 512KB,
80000h-9FFFFh is an empty bus: reads return FFh, writes are lost, and the
pages are timed as 8-bit like any unfitted address. CMOS 15h/16h follow
the size, and POST takes base memory from them (§71). The wasm
`Machine(extKb, baseKb)` takes both. The Memory panel's Base memory
select (512 KB or 640 KB, default 640) is locked while the machine runs
and remembered in this browser, and the specs line reads "512 or 640 KB
conventional".

### 76.3 Checks

- `bios_test`: Int1EhPointsAtIbmsDiskBase,
  Int13h08hReturnsEachDrivesOwnMediaTable,
  Int13h18hPointsAtTheTableForTheFormat. All three fail with the BIOS
  built without 0008.
- `chipset_test` A512KbBoardLeavesItsTopBankEmpty, `machine_test`
  CmosReportsA512KbBoard, and `bios_test` PostTakesBaseMemoryFromCmos
  on a real 512KB board. All three fail with the size ignored.
- `memory.spec.ts`: the base memory choice is locked and remembered, an
  unknown saved value falls back to 640KB, and a 512KB board shows
  `Conventional 512K` in MEM. The last fails with the page passing 640.
- The full Playwright suite passes with the new BIOS.

## 77. Drive noise (synthesized)

The page's Drive noise checkbox plays floppy and hard disk sounds made in
an AudioWorklet from noise and filters. No recordings are used, so the
checkbox's tooltip says it's synthesized. IDEAS.md §10 wants real
drive recordings; this is the event path they would plug into. Off on
every page load, like Sound.

### 77.1 Events from the controllers

`shared/pc/drive_sound.h` is a cycle-stamped log, `DriveSoundLog`, that
pushes nothing until enabled. `Fdc765::sound` and `Wd1003::sound` write to
it only at command level, never in `tick()`, so the 486 (which never turns
it on) and a muted AT do no extra work.

- Floppy: motor on and off from each DOR motor bit edge and from a
  controller reset; one SEEK or RECALIBRATE event with the step count and
  `step_cycles()` per step, none for a seek to the same cylinder; an access
  event for READ, WRITE, FORMAT and a failed command, lasting its paced
  transfer time.
- Hard disk: a buffered seek is one actuator move lasting `seek_cycles()`
  (the ST4038 buffers the pulses and seeks once, §72.1); a slow-step seek
  is one move per pulse at the step rate; RESTORE is one move per cylinder,
  11 ms apart (§72.2). Implied seeks before a transfer log the same way.
  Every sector read, written or formatted also logs an access event that
  lasts until BSY drops, the same window the HDD light shows.
- `setDriveSound(on)` turns both logs on and replays the current motor
  state, so a drive already spinning is heard. `driveSoundEvents()` drains
  both as five doubles an event: cycle, kind, source (A:, B:, C:), steps,
  period.

### 77.2 The synth

Event times follow the frame's own cycles, as the speaker's edges do.
Step periods and access lengths stay in real 8 MHz time. The worklet
plays them 50 ms behind real time to absorb frame jitter. The floppy
motor is a low, quiet hum with a trace of rumble and a slight ripple at
the spindle rate (360 rpm A:, 300 rpm B:). Each floppy step is a tight click around 2.8 kHz
with a 1.5 ms knock, and a transfer adds a faint grind. C:'s spindle hums
at 60 Hz (3,600 rpm) and ramps up after power-on. A buffered seek is a
sharp tick near 5 kHz with a light 180 Hz knock and a brief 650 Hz ring, a
faint buzz for the move, and a tick as it lands. Each sector access is a
quieter tick plus a soft rustle while C: is busy. A real voice-coil drive
reading within one cylinder is close to silent; the access sound is there
so the noise follows the light, and DOS's FAT scan for free space is
heard. None of the timbres comes from a measurement.

Power-off suspends the audio context, and power-on resumes it, so Sound
works after a power cycle.

### 77.3 Checks

- `fdc765_test`: DriveSoundLogsNothingUntilEnabled,
  DriveSoundStepsOncePerCylinderAtTheStepRate,
  DriveSoundFollowsEachMotorBitAndReset,
  DriveSoundMarksAReadForItsTransferTime.
- `wd1003_test`: DriveSoundBufferedSeekIsOneMoveForTheSeekTime,
  DriveSoundSlowStepAndRestoreMoveOncePerTrack, DriveSoundImpliedSeekOnARead,
  DriveSoundMarksEachSectorWhileBusy, DriveSoundLogsNothingUntilEnabled.
- `drivenoise.spec.ts`: off at load and after a reload; muted, nothing is
  logged; `TYPE` of a file on cylinder 39 of a 360KB disk in B: logs one
  39-step seek, and unchecking stops the log; a plain `DIR` logs C:
  sector accesses and none for the floppies; a power-on with it checked
  logs POST's motor and C:'s buffered seeks.
- Mutation: with the floppy seek push removed, the step test and the
  cylinder-39 spec fail; with `log_seek` emptied, the three hard disk seek
  tests and the power-on spec fail; without the sector access push,
  DriveSoundMarksEachSectorWhileBusy, DriveSoundImpliedSeekOnARead and
  the `DIR` spec fail.

## 78. The floppy head stays where it was left

Source: NEC uPD765A data sheet, SEEK, RECALIBRATE and ST0.

The controller keeps a Present Cylinder Number for each drive; the drive
has a head carriage. They are separate values, so a controller reset or a
disk mount leaves the head where it is, and RECALIBRATE steps it from
there. On the real machine neither moves the head.

- `Fdc765::Drive` keeps `current_cylinder` (the physical head) and `pcn`
  apart. Controller reset clears PCN only. `mount()` and `unmount()` leave
  the head alone. Power-on builds a new machine, so the head starts on
  track 0 there.
- SEEK issues |NCN - PCN| step pulses and moves the head that far, clamped
  at track 0 and the drive's last track. PCN becomes NCN.
- RECALIBRATE steps out until track 0, at most 77 pulses. If the head is
  still off track 0 after 77 (an 80-track drive left on 78 or 79), ST0 is
  70h | drive (abnormal termination, seek end, equipment check). SENSE
  INTERRUPT STATUS returns that ST0 and PCN.

This is what makes the drive noise grind: after a disk swap or a
controller reset with the head out on the disk, the BIOS's next
recalibrate steps it all the way home at the step rate.

Checks:

- `fdc765_test`: RecalibrateAfterAResetStepsTheHeadHomeFromWhereItWasLeft,
  SeekPulsesFollowPcnNotTheHead,
  RecalibrateGivesUpAfter77PulsesWithEquipmentCheck. All three fail with
  reset homing the head again.
- `drivenoise.spec.ts`: swapping the disk in B: after reading cylinder 39
  makes the next `DIR B:` step 39 times. It fails with mount homing the
  head.
