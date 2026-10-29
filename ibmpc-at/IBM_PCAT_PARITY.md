# IBM PC/AT 5170-339 -- parity backlog

The open gaps between this machine and a real 1986 8 MHz 5170-339 with an
IBM EGA, plus the test gaps against the repo rules in `CLAUDE.md`. Shared
chips live in `shared/pc/` and also drive `pc486`; their tests are under
`shared/pc/tests/` and build into this machine's `make check`. Where a 486
fix ports, the item cites its `PC486_REVIEW.md` section. Checked against
the code through `IBM_PCAT_REVIEW.md` §78 (2026-10-29). When an item lands, write the current behavior and its source into
`IBM_PCAT_REVIEW.md` and delete it here.

Done so far: the real-mode CPU, C1-C9 (`IBM_PCAT_REVIEW.md` §44), the
reset vector and `F1h`, C11-C12 (§45), bus timing, T1-T5 and T8 with P5
and E11 (§46), EGA contention plus the TOPBENCH calibration, T6-T7 with
P1-P3 (§47), the EGA BIOS stand-in plus E1-E10 and E12-E14 (§48), EGA
addressing, latches and the monochrome ports, E15-E17 (§49), the full
8259A and 8237A with P4, P6 and P9 (§50), storage S1-S4 (§51) and S5, S6
and S8's rotation (§52), the keyboard and RTC, K1-K5 and R1 (§53), test
parity X1-X4 and X5 (§54, §0 / §66), CPU test coverage (§55), the last
flat instruction costs, T10-T12 (§56), and protected mode and extended
memory (§63; original C10 and P8), COM1 serial mouse plus LPT1 print
capture (§67; original P7), paced hard disk writes, S10 (§68), and the
floppy data rate, head position and step rate, S7 and S9 (§69), and a
286-only BIOS with the 8042-reset block move, a reading verify and A:
then C: boot order, D1, D4 and S12 (§70), and IBM's fixed disk interface
from CMOS drive types with no EBDA and no ATA in the WD1003, P13 and D2
(§71), hard disk seek time, S8 (§72), IBM's diskette tables, S11, and
the 512KB board, P14 (§76).
Front-end shared modules and the Performance panel are in §64-§65; they
are not tracked as hardware parity.

Rough parity today: **~94%** (mean of the table rows).

| Area | Parity | Biggest gap |
|------|--------|-------------|
| CPU (real mode) | 99% | no STOREALL; `FE /2`-`/7` unverified (C13); no 10-byte instruction limit (C15) |
| CPU (protected mode) | 85% | no LOADALL (C14); two fault codes approximated (§63.2) |
| Timing | 95% | fetch-bound code runs 5-7% fast against TOPBENCH (§62, not tracked) |
| EGA | 91% | memory-cycle length unsourced (VidMem -9% on the Enhanced Display) |
| Chipset (PIT, PIC, DMA, memory) | 95% | no game port (P10) |
| Storage (WD1003, floppy) | 99% | seek curve fitted to spec maximums (§75, not tracked) |
| Keyboard, RTC | 95% | no keylock control, BAT arrives without its self-test delay |
| Speaker | 100% | — |

Every fix lands with its native test and, where it's visible from the
page, a Playwright test, in the same commit.

# TODO

## 1. CPU

- **C13. `FE /2` to `FE /7` do nothing.** Group 4 defines only INC and DEC
  on a byte. The core decodes the ModRM byte and returns without reading
  the operand. `FF /7` raises #UD here, and the 286 raises #UD for
  undefined encodings in general, but no source found says what a 286 does
  with these six. Needs the iAPX 286 PRM opcode map. No test pins the
  current behaviour (§55.4).

- **C14. No LOADALL / STOREALL.** `0F 05` is a reported no-op. HIMEM 2.x
  and RAMDRIVE used LOADALL on 286s; FDXMS286 on the shipped image uses
  the BIOS block move instead. `F1 0F 04` (STOREALL) isn't modelled.
  Descriptor caches exist (§63.1); what is still needed is a primary-source
  layout of the 286 LOADALL table at 800h. Source: Intel iAPX 286 PRM (it
  is undocumented there, so the layout source is the open question).

- **C15. No 10-byte instruction limit.** The 286 raises #GP when an
  instruction runs past 10 bytes, which only a run of redundant prefixes
  can do. The core loops on prefixes with no limit, so code that runs
  into memory full of prefix bytes (`26`, `2E`, `36`, `3E`, `F0`-`F3`)
  hangs the page inside one `step()` instead of faulting. Source: Intel
  iAPX 286 PRM, exception 13 (verify the real-mode wording before
  coding).

## 2. EGA

- **E19. No 9x14 alternate glyphs.** In mode 7, IBM's ROM patches the 8x14
  font with 9-dot versions of about a dozen characters. The only open set
  found is GPL and looks copied from IBM's ROM, so it can't go in the
  stand-in. AH=11h AL=30h BH=5 returns an empty list. Drawing new
  glyphs wouldn't make the screen any closer to IBM's, so this stays open
  until a clean public-domain set turns up.
- **E20. EGA memory-cycle length isn't sourced.** §47.3 derives 32 dots
  per five memory cycles. With the Enhanced Display, TOPBENCH's VidMem
  reads 1339 against the real 5170-339's 1470 (-9%); with a 200-line
  colour display it reads 1514 (+3%). The real run didn't record its
  monitor (§47.4). §62 confirmed the AT's 6-clock 8-bit floor is already
  applied and that the EGA TR still does not give the cycle length.
  Needs the cycle timing from the EGA's sequencer documentation.
- **E21. The palette address source bit is dropped.** Bit 5 of the 3C0h
  index is PAS: clear, the CPU owns the palette and the screen goes
  blank; set, the display reads it. `Ega::out` masks the index to 5 bits,
  so a program that leaves PAS clear still shows a picture here. Source:
  IBM EGA Technical Reference, Attribute Address Register.
- **E22. Input Status 1 bits 4-5 read 0.** On the EGA they return two of
  the six colour outputs, picked by AR12 bits 4-5 (Video Status MUX).
  IBM's EGA POST and some adapter-detection code read them. Low priority.
  Source: IBM EGA Technical Reference, Input Status Register One.

## 3. Chipset

- **P10. No game port.** The IBM Game Control Adapter at 201h was an
  option, not standard. Low priority. Port from the 486 if it lands
  there first.
- **P11. 8237 features nothing here starts.** A software request (request
  register) on a block-mode channel should run the transfer, including
  memory-to-memory on channels 0 and 1 through the temporary register.
  Command bits 3, 5, 6 and 7 (compressed timing, extended write, DREQ and
  DACK polarity) and rotating priority are stored and ignored. Low
  priority: no AT device or BIOS path uses them. A change here also
  affects `pc486` via `shared/pc/`. Source: Intel 8237A-5 data sheet
  (`IBM_PCAT_REVIEW.md` §50.3).
- **P12. Port 61h bits 2-3 read back 0, and there's no NMI.** On the AT,
  bits 0-3 of 61h read back as written: bit 2 enables RAM parity check,
  bit 3 enables I/O channel check. `Chipset::set_port61` keeps only bits
  0-1. Bits 4-5 already read refresh and OUT2. Bits 6-7 (parity error,
  channel check) and the NMI behind them, gated by port 70h bit 7
  (`CmosRtc::nmi_masked`), aren't wired. Nothing here raises them, so the
  readback of bits 2-3 is the visible part. Source: IBM PC/AT Technical
  Reference, port 061h.

## 4. Stand-in departures (labelled, not silent)

These are intentional substitutions, not missing silicon. Tracked so they
don't look like forgotten gaps:

- **D3. INT 16h AH=03h unsupported.** `MODE CON RATE=` does nothing.
  §53.2.
- **D5. Shipped FreeDOS image edits.** `MEM /C /N` and the CD-ROM probe
  removed; CTMOUSE left on for COM1. §61.
- **D7. INT 19h is Bochs's, not IBM's BOOT_STRAP.** One try a drive, a
  55AA check on floppies, "Booting from..." messages, and "No more boot
  devices" where the AT has ROM BASIC. IBM retries A: four times and C:
  three, checks a floppy's first byte and fill pattern instead, and prints
  nothing. §71.1.
- **D8. Drive noise is synthesized.** The Drive noise checkbox plays
  floppy steps, motor, C:'s spindle and seeks from the controllers' own
  events, but the sounds are made from filtered noise, not recordings of
  a 1.2MB drive or an ST4038. IDEAS.md §10. §77.
- **D6. egabios hooks INT 15h AH=4Fh + INT 1Ch for Print Screen.**
  BIOS-bochs-legacy never issues INT 5 on PrtSc (E0 37 becomes keypad `*`).
  egabios swallows the sequence on AH=4Fh and runs INT 5 from INT 1Ch.
  §48.1 / §67.

## Not ported from the 486, on purpose

VBE, the DAC and VGA's 400-line text, the cache model, FPU tags and
SoftFloat (no 80287 fitted; the ESC opcodes doing nothing is right), the
DX signature, #AC and debug registers (386 and later), the 15-byte limit
(the 286's is 10, §44.5), SB16/OPL3/CD-DA, and the PS/2 mouse (the AT has no
aux port; COM1 serial mouse is the period way, §67).

Already in line with the rules, for reference: the `?test=1&fast=1`
multiplier is gated in `app.js`, stripped from staged builds, the smoke
file boots to `C:\>`, runs a typed command and keeps a real-speed check,
deploy CI runs `ibmpcat-test` / `ibmpcat-web-test` smoke only, and the
full suites run nightly.
