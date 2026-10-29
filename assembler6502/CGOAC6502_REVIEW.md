# CG-OAC-6502 — hardware findings and citations

This document records what the emulator's behavior is actually based on:
netlist evidence from [`cpu6502/pcb/pcb6502/FullBoard/pcb6502full.net`](https://github.com/gsirhc/retro/blob/main/cpu6502/pcb/pcb6502/FullBoard/pcb6502full.net)
(exported from `pcb6502.kicad_sch`), cross-checked against the real firmware
in `assembler6502/firmware/`, and the decisions made where the two don't agree. Same
role as `altair8800/ALTAIR_REVIEW.md` §7.1 for that board: a schematic
discrepancy is documented with its evidence trail, not silently corrected or
silently reproduced. Sections 1-11 cover the board. Sections 12 onward
describe the firmware, the browser front end and the test suites as they
stand.

## 1. Populated parts

The netlist's `value` field is cosmetic schematic text; the real part comes
from each symbol's `libsource`. All three programmable chips are the WDC
**CMOS** parts, not their NMOS/MOS ancestors:

| Ref | Value text | Actual libsource part |
|---|---|---|
| U1 | `6502 (CPU)` | `PCM_65xx-library:W65C02SxP` |
| U4 | `6522 (VIA)` | `PCM_65xx-library:W65C22NxP` |
| U7 | `6551 (ACIA)` | `PCM_65xx-library:W65C51NxP` |
| U2 | `28C256 (ROM)` | `Memory_EEPROM:28C256`, 32K EEPROM, ZIF socket footprint |
| U3 | `62256 (RAM)` | same 28-pin symbol/footprint family, populated as a 62256 SRAM |
| U9 | `DS1813` | `Power_Protection:DS1813` reset supervisor |

This matters: `rom/bios.s` is `.setcpu "65C02"`, and the CMOS parts fix
several documented NMOS errata (65C02: `JMP ($xxFF)` page-wrap bug,
decimal-mode ADC/SBC flags; W65C51: NMOS 6551's inaccurate baud-rate
generator). The core implements the CMOS behavior throughout — see
`cpu65c02.cpp`'s file header for the full list.

## 2. Address decode (U5, a 74HC00 quad NAND)

Traced net-by-net from `pcb6502full.net`:

- Gate 4 (pins 12,13→11), both inputs tied to `/A15`: `CS_EEP = NAND(A15,A15)
  = /A15`, wired to U2 (ROM) `~CS`. ROM lives at **$8000-$FFFF**. Matches
  [`gall_oac.cfg`](https://github.com/gsirhc/retro/blob/main/cpu6502/rom/gall_oac.cfg) exactly: `BASROM` at `$8000` size `$7E00`, `WOZMON` at
  `$FE00` size `$1FA`, `RESETVEC` at `$FFFA` size `6` — sums to exactly
  `$8000` bytes.
- Gate 2 (pins 4,5→6): `CS2 = NAND(A14, CS_EEP)`, active when A15=0, A14=1
  (`$4000-$7FFF`). Feeds both U4 (VIA) `~CS2` and U7 (ACIA) `~CS2`.
- VIA `CS1` (active high, pin 24) is wired to `A13`; ACIA `CS1` (pin 2) to
  `A12`. So: **VIA at `$6000-$7FFF`** (A13=1), **ACIA at `$5000-$5FFF`**
  (A12=1) — each an 8K window, heavily mirrored (VIA only decodes 16
  registers off A0-3; ACIA decodes 4 off A0-1). Matches `rom/via.s`'s
  `PORTB = $6000` and `rom/bios.s`'s `ACIA_DATA = $5000` exactly.
- Gate 3 (pins 9,10→8): `CS_RAM = NAND(Phi2, CS_EEP)`, active whenever
  A15=0 during the valid part of the bus cycle — **no A14 term**, so as
  literally wired this selects RAM across the *entire* lower 32K, not just
  a RAM-only sub-range.
- **Gate 1 (pins 1,2→3) is completely unused** — input pin 2 floating,
  output pin 3 floating. This is the strongest evidence for what follows:
  a spare gate that most plausibly should have supplied a missing "exclude
  A14" term and never got wired in.

### 2a. RAM (U3) control-signal erratum, emulated as `~WE ← R/W`

The netlist wires U3 pin 22 (`~OE`, output enable) to `R/W`, and U3 pin 27
(`~WE`, write enable) to `A14` — not the standard `~WE ← R/W`. Taken
literally, combined with §2's un-gated `CS_RAM`: for any address below
`$4000` (A14=0), `~WE` is permanently asserted whenever the chip is
selected, regardless of whether the CPU intends a read or a write, while
`~OE` is asserted only during what the CPU considers a *write* (R/W=0) —
backwards. Zero-page and stack reads would be corrupted by phantom writes
of whatever's floating on the bus; the machine could not boot as drawn.

**Decision (confirmed with the board's owner):** treat this as a schematic
export error, not a real, working design — emulate the corrected,
functional wiring (`~WE ← R/W`, output enabled whenever selected) **and**
narrow RAM's decoded range to **`$0000-$3FFF`** (excluding the VIA/ACIA
window `$4000-$7FFF` a corrected design would need gate 1 to exclude).
Independent evidence this is the right range: `rom/bios.s`'s own boot
banner (the `ggeretsae` easter egg) advertises **"16K RAM"** verbatim, and a
boot of the original MSBASIC ROM through this emulator auto-detected
**15615 BYTES FREE**, a believable figure for 16K minus the
zero-page/stack/buffer overhead below it, and nowhere near what a wrong
decode range would produce. Implemented in `bus.cpp`.

### 2b. VIA/ACIA overlap at $7000-$7FFF, modeled as bus contention

VIA only checks A13; ACIA only checks A12; neither excludes the other's
bit. At `$7000-$7FFF` (A13=1 *and* A12=1), both chips' selects assert
simultaneously — real bus contention on a real board. Firmware never
addresses this range (`rom/via.s` uses `$6000-$600E`, `rom/bios.s` uses
`$5000-$5003` exclusively), so it's never fired in practice.

**Decision (confirmed):** keep this — it isn't excluded, it's modeled.
`bus.cpp` flags `last_access_was_contended` and resolves the read as a
wired-AND of both chips' output (a common, but explicitly *approximate*,
stand-in for two CMOS totem-pole outputs fighting on one line — not a
claim of exact electrical behavior). `bus_test.cpp` pins both that this
never fires in the ranges real firmware touches, and what happens when
something deliberately pokes `$7000-$7FFF`.

## 3. Clocking

X1 is a canned 1 MHz oscillator module (not a bare crystal — it drives
`PHI2` directly into the CPU, VIA, and ACIA, and gates `CS_RAM`). Real
1 MHz, no turbo, per `CLAUDE.md`. Y1 (1.8432 MHz) feeds the W65C51's
internal baud-rate generator directly — `acia65c51.cpp`'s `baud()` decodes
the *live* CTRL register into a real bps figure (WDC datasheet Table 4) so
the terminal can meter output against the ACIA's actual configured rate,
not a fixed per-profile guess.

## 4. Reset (DS1813, U9)

Open-drain `RST`, shared by SW1 (front button — works because it's
open-drain, just shorts the node), J4 (external reset header), and the
CPU/VIA/ACIA reset pins. Dallas/Maxim DS1813 datasheet: ~150-200ms
power-on reset hold. Modeled as a 175ms cycle-count hold in `machine.cpp`
before the CPU's first real vector read — paced across `run_cycles()`
calls (see §10), not burned instantly.

## 5. LEDs

D1, D4 and D7 are not VIA-driven. The netlist's `RNPA0/1/2` net names look
related to the VIA's own `PA0/1/2`, but `/PA0`, `/PA1`, `/PA2` and `/RNPA0`,
`/RNPA1`, `/RNPA2` are two entirely separate, unconnected nets. All three
LEDs are simple, always-passive circuits, no VIA/CPU involvement:

- **D1 "Power"**: `+5V → LED → R8 → GND`. Genuinely just a power indicator.
- **D4 "Rx" / D7 "Tx"**: sit directly across the RS232-*level* RX/TX lines
  (pre-MAX232 for RX off U8 pin 13, post-MAX232 for TX off U8 pin 14)
  through a resistor to GND — real line-activity indicators, independent
  of firmware entirely.

`machine.h`'s `on_power_led`/`on_rx_led`/`on_tx_led` model this: D1 fires
once, permanently, at construction; D4/D7 pulse per received/transmitted
byte (a real diode on the line would flicker per bit transition, not per
byte — a labelled simplification, since modeling literal line voltage
wasn't worth it for a UI indicator).

## 6. Jumper headers

- **J7 "Interrupts"**: breaks IRQ/NMI out from VIAIRQ/ACIAIRQ separately;
  R1/R2 (3.3kΩ) pull the CPU's IRQ/NMI pins high, so *no* jumper means
  neither chip's interrupt reaches the CPU at all. Confirmed from
  `rom/bios.s` itself (not inferred): `IRQ_HANDLER` reads the VIA's
  `T1CL`; `NMI_HANDLER` reads `ACIA_STATUS`/`ACIA_DATA`. Default:
  **VIA→IRQ, ACIA→NMI** — matches the firmware exactly.
- **J5 "Boot"**: PA1-4, wired up but not read anywhere in the shipped ROM
  (`BOOT`'s loop reads the serial port, not PORTA). Modeled, inert.
- **J8 "RS232 CTS"**: optional jumper routing the ACIA's own RTS output
  through the MAX232's spare channel to the connector. The ACIA's hardware
  `~CTS` input is hard-tied to GND — always clear-to-send, no incoming
  hardware flow control by default. Default: open.

## 7. J3 LCD accessory, attached by default

`rom/via.s` contains a complete HD44780 driver (E/RW/RS on VIA PA7/6/5,
data on PB0-7) — written for the sibling `pcb6502-LCD` board, but J3
exposes the identical VIA pins on FullBoard, so a real LCD plugged into J3
works identically. The schematic alone doesn't show what the stock ROM
needs from it: `rom/bios.s`'s `RESET` unconditionally calls `reset_via`,
which busy-polls the LCD's status bit on PB7 *before doing anything else*.
With nothing driving that pin, a genuinely bare FullBoard hangs forever at
boot, on real hardware as well as in emulation.

**Decision (confirmed):** the accessory (`hd44780.h`/`.cpp`) is **attached
by default** so the stock ROM actually boots, with `Machine::
set_lcd_attached(false)` reproducing the real bare-board hang on demand.

## 8. ROM image build (`rom/make.sh`)

The ROM is one linked image, not several. `bios.s` pulls in `via.s`,
`load.s` and `wozmon.s` transitively, and `editor.s` supplies the shell
and assembler (§12, §13). The whole build is a 32768-byte image laid out
by `gall_oac.cfg` (§2), assembles with `ca65` with zero assembler
errors, and it boots through the emulator end-to-end (see
`tests/machine_test.cpp`).

## 9. CPU core validation

`cpu65c02.cpp` passes both of Klaus Dormann's independent 6502/65C02
functional test suites in full (`make dormann` — ~30M and ~22M instructions
respectively), including the 65C02-extended-opcode suite's internal
decimal-mode ADC/SBC self-check. This is the same validation role
`TST8080`/`CPUTEST`/`8080EXM` play for the Altair's i8080 core.

## 10. Reset and wall-clock pacing

`Machine::step_one()` calls `cpu.reset()` to end the DS1813 hold, and
`cpu65c02::Cpu::reset()` (matching `i8080::Cpu::reset()`'s precedent)
zeroes its own `cycles` counter, since a real 65C02 doesn't remember
T-states across a reset either. `Machine::run_cycles()` therefore paces
against its own monotonic `total_cycles_`, independent of `cpu.cycles`, so
a reset landing mid-call can't make the pacing target unreachable or
overshoot it. `machine_test.cpp`'s
`ResetHoldsPcAtZeroUntilTheDs1813DelayElapses` pins this: pc stays at zero
until the hold elapses.

## 11. Open items (not blocking, flagged)

- Exact AT28C256 page size / write-cycle timing in `eeprom28c256.h` should
  be spot-checked against the specific chip's datasheet if a ROM-burning
  UI is ever added. The browser has no ROM-programmer panel;
  `m.burnRom()` seats the compiled image once at boot.
- The assembler has no `<` / `>` operators, so a `.BYTE` string can't
  hand its address to `PRINT_STR` / `LCD_PUTS` in one `JSR` (§17).
- The Examples-load wedge in the live serial path has no identified root
  cause (§16).
- LED-driving PA0-3 usage (if any exists elsewhere beyond what's been read
  so far) hasn't had a final confirming pass — §5 above is based on the
  netlist wiring, which is conclusive on its own regardless.

## 12. Wozmon and the resident assembler

Boot goes straight to a fixed Wozmon. A resident line editor and two-pass
assembler (`editor.s`) run on the emulated 65C02 itself. The ROM carries no
BASIC: the MSBASIC port
([`msbasic_galloac/`](https://github.com/gsirhc/retro/tree/main/cpu6502/rom/msbasic_galloac), cloned from `mist64/msbasic`), its numbered boot
menu, the clock utility and the BASIC-token custom commands (`commands.s`,
`format.s`) are not in the build at all, so there is no `msbasic_galloac`
license-provenance item to carry. The hardware model (`cpu65c02`,
`via65c22`, `acia65c51`, `eeprom28c256`, `hd44780`, `bus`) doesn't depend
on any of this. Only the ROM software differs.

**The `START_WOZ` entry point.** `START_WOZ` is aliased onto `ESCAPE`,
Wozmon's cold/re-sync entry point. It is the classic Apple-1 Wozmon
source's "start over" label: it prints the authentic `\` banner and
explicitly sets `LDY #$01` before its read loop, so it self-initializes
regardless of incoming A/Y. `NOTCR` is not a safe entry. It is Wozmon's
*mid-loop* "was that a CR" branch, which does an unconditional `INY` on
whatever Y already holds, and `RESET` jumps straight to Wozmon with no menu
in between, so Y is whatever `RESET`'s own init left in the register,
unrelated to any text buffer. See the comment at `wozmon.s`'s `START_WOZ`
label for the in-source version of this note.

**VIA init.** `via.s`'s `reset_via` sets up GPIO direction and the LCD, and
explicitly disables every VIA interrupt source (`IER = $7F`). It arms
nothing: there is no Timer-1 free-run (`ACR`/`T1CL`/`T1CH`) and no jiffy
counter, since the ROM has no clock utility. A user's own assembled program
can still arm CA1/Timer1/etc itself via `IER` if it wants VIA interrupts,
routed through J7 (see `IRQ_HANDLER` in `bios.s`).

**Memory map for the editor/assembler.** It respects the only two existing
claimants on RAM: `bus.cpp`'s `if (addr < 0x4000) return ram[addr]` (RAM is
$0000-$3FFF, full stop), Wozmon's own `WOZMON_BUFFER` at $0300-$03FF and
the ACIA RX ring at $0200-$02FF:

| Region | Range | Size |
|---|---|---|
| Zero page | $0000-$00FF | 256 B |
| Stack | $0100-$01FF | 256 B |
| ACIA RX ring | $0200-$02FF | 256 B |
| Wozmon buffer | $0300-$03FF | 256 B (128 used) |
| Object code | $0400-$2BFF | ~10.25 KB |
| Symbol table | $2C00-$2FFF | 1 KB |
| Source text | $3000-$3FFF | 4 KB |

With no BASIC, zero page is otherwise wide open. The editor's own ZP vars
only need to avoid Wozmon's ($24-$2B) and `load.s`'s ($4B, $4D-$4F).

**Serial LOAD/SAVE** (`load.s`, see the file's own header comment): LOAD
frames itself with an idle timeout (real hardware has no "end of transfer"
marker on the receive side, since a human or an arbitrary text file is
doing the sending). Once ~0.33 s passes with no new byte after the first
one arrives, the buffer is considered complete and NUL-terminated. SAVE is
the browser-initiated direction, so it can use exact framing instead:
ASCII STX ($02) before, ETX ($03) after, genuine "Start/End of Text"
control-code framing, not a project invention. Both are shell commands
(§13), not Wozmon entry points.

**No return path from `RUN`.** Wozmon's `R` command is `JMP (XAML)` and
never returns, so a stray `BRK` in an assembled test program needs
somewhere sane to land. Convention (also shown on the browser's Help
panel, generated from the real build's addresses so it can't go stale, see
below): end a test program with an explicit `JMP` back to `SHELL_PROMPT`
(§13). `START_WOZ`, the re-sync entry RESET uses, returns to raw Wozmon.

**Browser-side entry-point addresses.** Every ROM layout change shifts the
real address of each entry point, so nothing hand-copies them.
`web/gen_entrypoints.py` parses the real linker-generated `firmware.lbl` at
build time and emits `web/roms/entrypoints.js` (gitignored, like the other
build artifacts). The Help panel and the Playwright suite read
`window.CGOAC_ENTRYPOINTS` rather than a literal. `SRC_START`/`SYM_START`/
`OBJ_START` aren't in that file: they're plain `=` constants, not linker
labels, so they don't appear in `.lbl` output at all.

**Input pacing, browser side.** `app.js`'s `driveFrame` feeds typed and
pasted bytes to `m.typeChar()` at the ACIA's live configured baud, not in
one tight loop. That matters for LOAD/SAVE and for paste, which can hand
the terminal many bytes in one JS turn. The ACIA models a real single-byte
RX register: a second byte arriving before the NMI handler drains the first
is a genuine overrun, not a simulation nicety. Pacing is consistent with
this repo's realism rules (`CLAUDE.md`: never speed up CPU/bus timing). It
injects no cycles beyond the current animation frame's own real,
wall-clock-paced budget. It only slices that existing budget between
characters instead of running it all before or after typing them. The one
carve-out is `?test=1`, which automated tests use so a suite run isn't
paced out at 19200-baud realism on every save/load. The Save/Load popup has
no speed override (§19).

## 13. The command shell and line-numbered programs

`SHELL_ENTRY` is a single address that opens a persistent, BASIC-like
command shell with its own `>` prompt. It has real line-numbered program
storage (insert/replace/delete by number, no renumbering needed to insert
between two existing lines) plus `NEW`/`LIST`/`LIST n`/`EDIT n`/`ASM`/
`RUN`/`FREE`/`LOAD`/`SAVE`/`QUIT` as typed command words. There are no
per-function `<addr>R` entry points. This is original engineering for this
board (not a recreation of a specific historical product), but its shape
mirrors two well-documented real precedents cited below rather than being
invented from nothing.

**Storage format.** Each program line is `[num_lo][num_hi][ascii
text...][CR]` in the 4K source buffer (`SRC_START`-`SRC_END`), a 2-byte
*binary* line-number prefix, not ASCII digits. Storing the number as
decimal ASCII (matching what's displayed) would avoid a conversion step on
display, but `STORE_LINE`'s sorted-insert scan compares numbers on every
line it walks past, and a 16-bit binary compare is a couple of
instructions where re-parsing ASCII digits every scan would be many more,
repeated on every edit. Decimal is purely a *display* format, produced on
the way out by `PRDEC` (repeated subtraction against a
`10000/1000/100/10/1` power-of-ten table, the standard 6502-textbook
binary-to-decimal shape, e.g. Leventhal's *6502 Assembly Language
Programming*, the direct decimal analogue of this ROM's own
`PRBYTE`/`PRHEX` hex routines, wozmon.s) and parsed once on the way in by
`DEC_PARSE`. End-of-buffer sentinel: a stored `num_lo=0, num_hi=0`. Line
number 0 is reserved/disallowed as a real line number for exactly this
reason (a real line whose number happens to be, say, 256 has `num_lo=0`
and must not be mistaken for the sentinel; checking both bytes, not just
one, is what makes that safe).

**`STORE_LINE`, the single mutation primitive.** Every write path
(explicit numbered entry, `EDIT`'s retype, and `LOAD`'s per-line
ingestion, below) funnels through one routine: find any existing line
with this number and delete it (a real byte-shift `MEMMOVE_DOWN`, closing
the gap), then, only if new text was given, insert it at the correct
sorted position (`MEMMOVE_UP`, opening a gap). Delete-then-insert are two
separate, simpler shifts rather than one "resize in place" op, which
avoids needing separate net-growth-vs-shrink math. Each step's own bounds
check is correct on its own. Shifting the buffer to open or close a gap
is the same algorithm classic Microsoft BASIC derivatives (Applesoft,
Commodore BASIC, and MSBASIC ports such as `msbasic_galloac`) use to
insert or delete a program line in their own tokenized-line buffers, a
real, well-documented precedent for exactly this data structure on
exactly this class of machine.

**`MVDST` is a working pointer, not a return value.** After an insert,
`STORE_LINE` places the fresh 0,0 sentinel at the buffer's new end. It
takes that address from `TEMP16`, computed during the bounds check
(`ENDPTR_before_insert + NEWTOTLEN`), which `MEMMOVE_UP` never touches. It
doesn't reuse `MVDST`: `MEMMOVE_UP` *walks `MVDST` backward to zero* as
its own loop counter once there's real data to shift (inserting before or
between existing lines, not just appending), so afterward `MVDST` holds
whatever the copy loop left, not the address it started at. Only the
degenerate append case (`MVLEN` is 0, `MEMMOVE_UP` returns immediately
without touching anything) leaves it intact.
`InsertingBetweenExistingLinesNeedsNoRenumbering`
(`tests/editor_test.cpp`) pins the insert-before case: the newly-inserted
line's own terminating CR and the following line's number bytes stay
intact. See the comment at `STORE_LINE`'s `sl_donetxt` label
(`editor.s`).

**`LOAD`/`SAVE` speak real decimal text, not the internal binary
format.** A saved program is a genuinely readable text file (open it,
edit it, re-import it), so `DO_SAVE` converts each line via `PRDEC` on
the way out (`"<num> <text>\r"`, framed in real STX/ETX control bytes),
and `DO_LOAD` splits the incoming stream on CR and feeds each completed
line through `PROCESS_LINE`, the *exact same* classify-and-store path the
interactive shell prompt itself uses. This one reuse is what makes `LOAD`
accept both a real numbered save file and a plain unnumbered text file a
person hand-typed and imported: an unnumbered line just auto-numbers,
exactly as it would if typed directly at the `>` prompt (`AUTO_NUMBER`:
highest existing number + 10, the same step classic BASIC's own `AUTO`
command used).

**`LOADMODE` guards `LOAD` against command hijack.** Because `LOAD`'s
per-line ingestion reuses the shell's own command dispatcher, a plain-text
import whose content happens to read `RUN` or `QUIT` on its own line
would, without a guard, be misread as *that command* mid-transfer,
jumping into the object-code region or back to Wozmon partway through a
load and silently truncating it. `PROCESS_LINE` takes a `LOADMODE` flag
(set only while `DO_LOAD` is actively receiving) that skips command-word
matching entirely and always auto-numbers instead while it's set.
`Load.LoadDoesNotLetAnUnnumberedLineThatReadsLikeACommandHijackTheTransfer`
(`tests/load_test.cpp`) imports a file containing a bare `RUN` line and
confirms the transfer continues normally around it. `CHECK_SYNTAX`
(below) also rejects that bare `RUN` as `?SYNTAX`, a second, independent
safety net, so the rejected line never consumes a number and the next
real line auto-numbers 20, not 30.

**`ASM`'s error report names the real program line.** Every line has its
own human-chosen number, so `ASM_READLINE` captures it into `CURNUM` as a
side effect of reading the line, and `DO_ASM`'s error path prints that
(via `PRDEC`): `ERR LINE 30` means line 30, not "the second line in the
file".

**Browser-side state tracking.** `app.js` types every synthetic keystroke
itself (the human's own typing is unaffected), so it has to know whether
the terminal is currently sitting at Wozmon's `\` prompt or already inside
the shell. Blindly sending `"<hex>R"` (Wozmon's run syntax) into an
*already-open* shell prompt would not reach Wozmon at all, since Wozmon
isn't listening once the shell has taken over `READCHAR`. `PROCESS_LINE`'s
own tolerant grammar (no space required between a line number and
following text, so `PARSE_LINE` falls through to `pl_notext` rather than
erroring, see `PARSE_LINE`'s header) would accept it as line number 8000
with text `"R"` and quietly insert a bogus extra line into the program.
So an `inShell` boolean gates an `ensureShell()` helper that
`runSave`/`runLoad` call before typing `SAVE`/`LOAD`. `inShell` is
inferred from real signal, not only from `app.js`'s own sends. Entering
is detected from the shell's own banner text ("ASSEMBLY CODER") appearing
in the *real ROM output* stream (a small rolling window, `bannerTail`,
checked in `pullSerial()`, the same place that already drains
`m.readOutput()` every frame). Leaving is detected from the human's own
typed line reading `QUIT` (tracked via a `typedLine` buffer built up in
`term.onData`, cleared on Enter/backspace). Both are real signals: the
banner is a byte sequence only the actual shell prints on real entry
(whether reached by a synthetic or a hand-typed `<addr>R`), and `QUIT` is
the literal command that returns to Wozmon regardless of who typed it.
`SW1` (reset) unconditionally clears both trackers, matching the hardware
fact that a reset always drops back to raw Wozmon.

Covered by `saveload.spec.ts`'s "Save/Load work correctly even when the
terminal is already inside the shell" and "manually entering the shell (as
the Help panel instructs), then Save, doesn't pollute the program with a
bogus re-entry line". The second enters the shell exactly the way the Help
panel documents (not via any `app.js` helper), Saves, then `LIST`s and
confirms exactly two lines exist, no bogus third one.

**Ctrl-C breaks a hung program, a real serial-break convention rather than
a cooperative check.** `RUN` transfers control to the user's own assembled
object code with no supervision. The break lives entirely in `bios.s`'s
`NMI_HANDLER`, the ACIA's receive-interrupt handler: it recognizes byte
`$03` (ASCII ETX, the real Ctrl-C wire value) as it comes off the wire
and, instead of buffering it into `SERIAL_BUFFER` and `RTI`-ing back to
whatever was interrupted, resets the stack pointer and jumps straight to
`SHELL_PROMPT`. This is the standard 6502 SBC-monitor break pattern: the
receive-ISR itself recognizes the break character and redirects, rather
than relying on the interrupted code to notice anything. It's why NMI
(not a polled check) is the only mechanism that actually works here. NMI
fires the instant the byte arrives regardless of what the CPU is doing,
including a tight `JMP $`-to-itself loop with no `READCHAR` poll of its
own, unlike, say, classic Microsoft BASIC's STOP key, which only works
because the interpreter polls the keyboard between statements. A
resident assembler's own compiled programs have no such interpreter loop
to lean on, so a genuine interrupt is the only option, matching real
serial-break convention on 6502 monitors of the era.

Two details. The handler never `CHROUT`s anything before jumping: the
interrupted code could itself be mid-`CHROUT` (inside `CHLL`'s real
per-character ACIA delay), and writing a fresh byte to `ACIA_DATA` before
that finishes would corrupt whatever transmission was already in flight,
so the shell's own `>` reprompt is the only feedback Ctrl-C gives, and
it's sufficient. And there's no attempt to preserve or resume the
interrupted context. Ctrl-C is a one-way break, not a suspend/resume. The
stack is reset to a known-good empty state (`LDX #$FF; TXS`) rather than
trying to unwind whatever depth of subroutine calls the running program
had reached, since there's nothing sane to return to once the user has
asked to abandon it. This only works while the ACIA is jumpered to NMI
(J7's default, matching the shipped ROM). Routing it to IRQ instead
disables all serial reception (`IRQ_HANDLER` is a bare stub).

Covered by `Editor.CtrlCBreaksAGenuinelyInfiniteLoopBackToTheShell`
(`tests/editor_test.cpp`), which types a real `JMP START`-to-itself
program, confirms via `cpu.pc` that it's genuinely spinning at the loop
after a large cycle budget, then confirms Ctrl-C returns to a working
shell prompt (a follow-up `LIST` still works, proving the stack/state
wasn't left corrupted), and by
`Editor.CtrlCAtTheShellPromptDiscardsAnyPartialLineAndReprompts`, which
sends Ctrl-C mid-typing (no program running at all, just idle in
`READLINE_ECHO`'s own poll loop) and confirms the abandoned partial line
never gets stored. `editor.spec.ts`'s "Ctrl-C breaks a genuinely hung
(infinite-loop) program back to the shell" drives the same scenario
through the real browser terminal. xterm.js's default keybinding sends the
raw `$03` byte via `term.onData` on Ctrl+C with nothing selected, flowing
through the existing input-pacing pipeline unmodified.

**`SHELL_ENTRY` is pinned at `$8000`.** Every other address in this ROM is
looked up dynamically at build time (`gen_entrypoints.py` reads the real
value out of `tmp/firmware.lbl`, and the browser/tests read it back from
that generated file), so a source edit anywhere in `editor.s` can shift
every label after it without anything going stale. `SHELL_ENTRY` is the
one address a human is expected to type from memory, over and over, at
Wozmon's raw `\` prompt, so it can't float. `gall_oac.cfg`'s `BASROM`
region starts at `$8000`, and the BASIC-era segments (`HEADER`,
`VECTORS`, `KEYWORDS`, …) are still declared in the linker config but
empty (nothing left in the build populates them), so the `BIOS` segment is
the first thing placed in ROM. A one-instruction `jmp SHELL_START`
trampoline, literally the first bytes of `bios.s`'s own `.segment "BIOS"`,
sits at `$8000` by construction. The real shell implementation
(`SHELL_START`; the label `SHELL_ENTRY` names the trampoline) moves freely
as `editor.s` changes. Same shape as a fixed low-memory cold-start vector
on period hardware, e.g. the Commodore 64's BASIC cold-start entry sitting
at a fixed `$A000` despite KERNAL/BASIC ROM internals shifting between
revisions. `gen_entrypoints.py`'s `WANTED` list just says `"SHELL_ENTRY"`,
which resolves to whatever address that name has in the freshly-built
`.lbl` file, so `app.js` and the Playwright specs (all read
`E.SHELL_ENTRY` live) carry no literal. Only the three GoogleTest files'
own hardcoded `kShellEntry` constant (documented in each file as "verified
against `tmp/firmware.lbl` after each build," the convention for this one
intentionally hand-typed address) holds `0x8000`.

**A fixed OS-call jump table, same trick, for terminal/LCD I/O from a user
program.** A real program has legitimate reason to print something. It's
the only way to *prove* it ran, short of dropping into Wozmon and
examining memory by hand. Every candidate routine (`CHROUT`, `STROUT`,
`print_char_lcd`, `clear_lcd`, `cursorLine1_lcd`/`cursorLine2_lcd`) lives
at a real but unpinned address, so a `JSR` to one in a saved program
would silently break on the next ROM rebuild. `bios.s`'s trampoline is a
small table of 3-byte `jmp`s to the real (free-floating) implementations:
`SHELL_ENTRY`, `PRINT_CHAR`/`PRINT_STR` (terminal),
`LCD_PUTC`/`LCD_PUTS`/`LCD_CLEAR`/`LCD_LINE1`/`LCD_LINE2`, so
`$8000`-`$8017` is a small, permanent public API surface. `LCD_PUTS`
needs `PRINT_STR_LCD` (`via.s`), a loop over the LCD's per-character
primitive, same shape as `STROUT`'s own loop and sharing `STROUT`'s
`ADDR_PTR` zero-page pointer (safe: the two never run concurrently).
Because this table occupies the first bytes of ROM, the boot code and
everything else sit after `$8017`. `SHELL_PROMPT`, the resume-`JMP` target
documented on the Help panel, is not pinned. It was `$80EE` in the build
this section was last checked against, and the browser and tests read the
live value.

`gen_entrypoints.py`'s `WANTED` list names the table's entries, not their
addresses, so the browser and Playwright specs hold no address literal.
Tests holding a raw address for something *not* on the pinned list read it
from the build: `RunResumeLandsBackInTheShellNotRawWozmon`'s `JMP
$SHELL_PROMPT` construction reads the real address at test time straight
out of `tmp/firmware.lbl` (a small `labelAddr()` helper, mirroring what
`gen_entrypoints.py` does for the browser), so it can't go stale on a
rebuild. `board.spec.ts`'s PC-range assertion against `via.s`'s `lcdbusy`
loop proves the bare-board LCD hang genuinely spins forever, not crashes.

Covered by `Assembler.JsrPrintCharReachesTheTerminal`,
`Assembler.JsrPrintStrReachesTheTerminal` (pokes a string into RAM byte by
byte first) and `Assembler.JsrLcdPutcAndPutsReachTheLcd`
(`tests/assembler_test.cpp`). Each types a real program that `JSR`s the
fixed address, runs it, and checks the real side effect (`on_serial_out`
capture, or `m.lcd.text[][]` directly). `saveload.spec.ts`'s "shows this
build's real OS-call addresses" mirrors the `SHELL_ENTRY`/`SHELL_PROMPT`
Help-panel test for the seven OS-call IDs. A program can embed its own
string data with `.BYTE` (§17).

**`PRINT_ENTRY` reformats stored source into aligned columns for
display, a display-only pass, not a stricter grammar.** Listings use
classic assembly-listing formatting (label / mnemonic / operand /
comment, each in its own column) without column-sensitive typing. Real
teletype-era assemblers split on whitespace, not fixed columns (this
ROM's own toolchain, ca65, works the same way), and imposing strict
columns on entry would fight the shell's free-form
`STORE_LINE`/`AUTO_NUMBER` design for no real benefit. So the *stored*
text stays exactly what was typed (however many spaces, wherever). Only
`PRINT_ENTRY`, shared by `LIST` and `EDIT`'s "show the line" step,
re-derives field boundaries for display, using the same label-scan shape
`PARSE_LINE` uses (scan for a leading identifier terminated by `:`)
copied onto a display-only buffer copy so it can't disturb `ASM`'s own
use of `LINE_BUF`/`CURSOR`/etc. It skips all of `PARSE_LINE`'s real
validation (`MNEM_LOOKUP`, addressing-mode resolution), so an
unassembled, even syntactically broken, line still lists sensibly.
`LABEL_FIELD`/`OPERAND_FIELD` (8/10 columns) are the only two knobs. A
label or operand longer than its field gets one guaranteed separator
space instead of breaking alignment further right, rather than being
truncated.

Comments are handled in `ASM_READLINE`: its "in a comment" flag (`TEMP16`,
reused) strips everything from a `;` to end-of-line before `PARSE_LINE`
sees it, and the file's own header documents `LABEL: MNEMONIC OPERAND ;
comment` as the grammar. `PRINT_ENTRY` is the only place that *keeps* a
comment (it's a display pass, not the real parse). It pads the operand
field out to `OPERAND_FIELD`, then prints `"; "` plus the comment text. A
`;` with nothing real before it (no mnemonic at all, just `"; comment"`
after a possible number) prints as blank mnemonic/operand placeholder
fields followed by the aligned comment, the same "a comment-only line
assembles as a no-op" behavior `ASM_READLINE`'s stripping gives, and it
displays sensibly instead of looking like an empty line.

Covered by `Assembler.InlineCommentsAreIgnoredByTheAssembler` and
`Assembler.AFullLineCommentAssemblesAsANoOp` (checking real emitted object
bytes) and `Editor.ListFormatsLabelMnemonicOperandAndCommentIntoAlignedColumns`
(the display formatting, including the comment-only-line case) in the
respective GoogleTest files. Every `LIST`/`EDIT` output assertion in both
the GoogleTest and Playwright suites expects the real column padding. The
*typed input* in any test is whatever a human would naturally type.

**`LOAD` echoes each incoming line on its own row.** `READCHAR`
(`bios.s`) echoes every raw byte it reads, unconditionally, for every
caller, including `DO_LOAD`'s own ingestion loop, which reads the wire
format's bare-CR line separators (`load.s`'s own header explains why the
wire format is bare-CR, not CRLF: it matches what `SAVE` emits and what
`PROCESS_LINE` expects). A lone CR on a real terminal returns the cursor
to column 0 *without* advancing to the next row, so `dload_gotcr` emits an
explicit `LF` right after the CR `READCHAR` already echoed, mirroring what
`READLINE_ECHO` applies after its own CR. Without it each newly-echoed
line would land on top of the previous one.

Covered by `Load.LoadEchoesEachIncomingLineOnItsOwnTerminalRowNotOverwritingThePrevious`
(`tests/load_test.cpp`), which asserts every CR in a real `LOAD`
transfer's echoed output is immediately followed by an LF, checked against
the raw `on_serial_out` byte capture, not rendered text.
`saveload.spec.ts`'s "Load's own echo lands each incoming line on its own
row, not overlapping the previous one" drives the same scenario through
the real browser terminal, checking a raw output spy rather than rendered
DOM text (see the note on `innerText()` reliability below).

**`innerText()` can miss content a screenshot correctly shows.**
`page.locator("#screen").innerText()` can return stale content
immediately after an action that a `page.locator("#screen").screenshot()`
taken at the same moment shows already rendered. This is distinct from
the xterm.js "WWWWWWW..." glyph-measurement span (§14.b), though the two
can compound in the same test run. The reliable pattern already exists
(`terminal.spec.ts`'s CAPS LOCK tests spy on `m.typeChar` rather than
trusting rendered text). The `LOAD`/`SAVE` tests above spy on
`m.readOutput` the same way (`installOutputSpy`/`getRawOut`,
`saveload.spec.ts`), both the pass/fail assertion *and* the
synchronization wait (`expect.poll` against the raw spy, not
`toContainText` against the DOM), so neither test depends on xterm.js
having painted anything by the time Playwright looks.

**Prompt and echo behavior** (all in the shell's own I/O path in
`editor.s`, not in any command's logic):

- The prompt character is `>` (`shell_loop`'s `lda #'>'`), a single
  literal, so every place a test looks for "did the shell reprompt" uses it
  (`out.find(">")` in GoogleTest, the `/>/g` prompt-count regex in
  `editor.spec.ts`'s Ctrl-C test).
- **A blank Enter opens a fresh row.** `READCHAR` echoes the bare CR
  unconditionally regardless of line length, and a lone CR returns the
  cursor to column 0 without advancing a row. So `READLINE_ECHO`'s
  `rl_gotcr` always emits the LF, including when nothing was typed before
  Enter (Y=0), matching `dload_gotcr`'s unconditional version.
- **`SHELL_PROMPT` prints its own CR/LF before reprompting. The ordinary
  per-command loop doesn't.** `SHELL_PROMPT` is the one address a
  resumed/interrupted program's `JMP` and `bios.s`'s `NMI_HANDLER` (Ctrl-C)
  land on (see the fixed-`$8000`-and-jump-table note above). A test
  program's `PRINT_CHAR`'d output has no reason to end in a newline of its
  own, so the reprompt needs a leading CR/LF or it lands glued onto that
  output (`X>`). The ordinary command loop doesn't need one, since the
  prior command's own output already ends in its own CR/LF. So there are
  two labels: `SHELL_PROMPT` (prints CR/LF, then falls into `shell_loop`)
  and `shell_loop` (the bare reprompt, which `SHELL_START`'s startup
  fallthrough and the loop's own `jmp` target directly). Covered by
  `Editor.RunResumeLandsBackInTheShellNotRawWozmon` and both Ctrl-C tests
  against the `>`-prompt assertions above (a regression would show as the
  reprompt glued to the preceding output).
- **`NEW`, `SAVE` and a clean `ASM` print a shared `Ok`.** `MSG_OK` is
  `"Ok"`, referenced by `DO_NEW`, `DO_SAVE` and `ASM`'s clean-assemble
  confirmation, one consistent success string across all three. The
  `NEW` GoogleTest case checks for `"Ok"`, not `"NEW"`, since
  `out.find("NEW")` would match `READLINE_ECHO`'s own echo of the typed
  command word rather than the completion banner. One `saveload.spec.ts`
  test types `SAVE` before `LOAD`, and since both print the identical `Ok`,
  a bare `raw.includes("Ok")` synchronization wait would resolve
  immediately against `SAVE`'s own `Ok` rather than waiting for the
  `LOAD`. It checkpoints the raw output spy's length right before
  triggering `LOAD` and polls only the slice after it, the same "don't
  reuse a signal a prior step in the same test already produced" pattern
  `saveload.spec.ts` uses for "STA $50" (see the Help-panel and pollution
  tests above).

**Backspace erases the character, not just the cursor.** `READLINE_ECHO`
treats both `$08` (BS, what a genuine ASR-33/Teletype-style keyboard's
Backspace key sends) and `$7F` (DEL, what the physical Backspace key sends
through this board's browser terminal, `app.js`/xterm.js, confirmed by
spying on `Machine.typeChar`) as the same "erase the previous character"
trigger. Both are consumed by the erase path, so neither is ever written
into `LINE_BUF`, where an invisible control byte would ride along through
`STORE_LINE` into the program buffer and make the stored line differ
silently from what the screen shows.

The erase itself is destructive (`rl_erase`, `editor.s`): overwrite the
vacated column with a space, then back up over the space too, landing the
cursor where the bare erase leaves it but with the glyph gone.
`READCHAR` (bios.s) echoes every raw byte it reads, `$08` included, and a
bare BS only moves a terminal's cursor left one column without erasing the
glyph there. `wozmon.s`'s own `BACKSPACE` (`DEY`/`BMI GETLINE`) is exactly
that non-destructive convention, period-accurate for the real Apple 1
monitor it recreates. This shell is original software for this board, not
a recreation of a specific historical monitor, so it doesn't inherit that
limitation.

The two bytes need different handling in that shared `rl_erase` step.
`$08`'s own automatic echo already moved the cursor left by the time
`READLINE_ECHO` classifies it, but `$7F` in the *output* direction is the
classic teletype "rubout, ignore me" byte. A real terminal (xterm.js
included) treats it as a non-printing no-op, so it never moves the cursor
on its own. The `$7F` path supplies that missing leading BS itself before
calling `rl_erase`, or the erase would land one column too far right.

Covered by `Editor.BackspaceErasesTheCharacterNotJustTheCursor` (`$08`),
`Editor.DelAlsoErasesDestructivelyAndDoesNotCorruptTheStoredLine` (`$7F`,
asserting both the exact echoed byte sequence *and* that the corrected
line stores and lists with no stray `$7F` leaked into it) and
`Editor.BackspaceOnAnEmptyLineIsANoOp` (`tests/editor_test.cpp`, both
bytes, Y=0 guard). `editor.spec.ts`'s "backspace erases the character,
not just the cursor" drives the same scenario through the real browser
terminal using `page.keyboard.press("Backspace")`, the actual key rather
than a hand-picked byte, spying on the raw output stream the same way
`saveload.spec.ts` does.

**`CHECK_SYNTAX`: bad opcode/operand syntax is rejected the instant a
line is typed, not only at `ASM`/`RUN` time.** It catches "garbage entered
by the user", a typo'd mnemonic or malformed operand, without needing to
check whether any label it mentions exists yet. `STORE_LINE` (the single
mutation primitive every write path funnels through: numbered entry,
`EDIT`'s retype, `LOAD`'s per-line ingestion) calls `CHECK_SYNTAX` first.
It reuses `PARSE_LINE` itself, the *exact* routine `ASM_PASS1`/`ASM_PASS2`
use for real assembly, not a second, parallel implementation that could
drift out of sync with what `ASM` accepts, against the about-to-be-stored
text. If `PARSE_LINE` sets `ERRFLAG` (unrecognized mnemonic, malformed
`$xx`/`#$xx` operand, an unsupported addressing mode for that mnemonic,
…), `STORE_LINE` prints `?SYNTAX` and bails out before touching the buffer
at all. Nothing is deleted or inserted, so a bad retype can't clobber a
good existing line, and a bad auto-numbered line doesn't consume a line
number. The precedent is real: classic Microsoft BASIC (and its
derivatives, `msbasic_galloac` among them) tokenized and syntax-checked
each line as it was typed, printing `?SYNTAX ERROR` immediately rather
than waiting for `RUN`. `?SYNTAX` (`MSG_SYNTAX`) has the same terse
`?`-prefixed shape as this ROM's `?LINE` (`MSG_BADLINE`).

**It is label-agnostic: a syntax check, not an early resolve.**
`PARSE_LINE`'s mnemonic lookup (`MNEM_LOOKUP`) and addressing-mode
resolution (`RESOLVE_MODE`/`RESOLVE_SIZE`) never consult the symbol
table. Only `SYM_DEFINE` (pass 1) and `EMIT_INSTRUCTION`'s operand
resolution (pass 2) do, and neither runs here. A labelled operand
(`PARSE_OPERAND`'s `@label` path) is accepted purely structurally (looks
like an identifier), its value never looked up. So `20 JMP FORWARD` typed
before line 30 defines `FORWARD` passes cleanly, exactly like a real
two-pass assembler accepts a forward reference. `ASM`'s own passes are the
only place an *undefined* label is caught (`LOOKUP_LABEL`, pass 2).
Checking labels at entry would reject every forward reference a normal,
sensibly-ordered program makes routine use of.

**Comments are excluded from the checked range.** The text `STORE_LINE`
receives is whatever the user typed, comments and all (`PRINT_ENTRY`
needs that same raw text for `LIST`/`EDIT`, see above), but
`PARSE_OPERAND`'s label path rejects a bare `;` outright (`IS_ALPHA` fails
on it → `@badoperand`), so `NOP ; nothing to do` would read as `?SYNTAX`
without the exclusion. `CHECK_SYNTAX` finds the first `;` in the checked
range (the same trigger `ASM_READLINE`'s own comment strip uses) and
temporarily shortens `LINELEN` to stop right there before calling
`PARSE_LINE`, restoring the real length immediately after. `PARSE_LINE`
only ever *reads* `LINE_BUF`, never writes it, so this is safe and needs
no second scratch buffer. `PARSE_LINE` expects the caller to set `CURSOR`
first: `ASM_PASS1`/`2` each `stz CURSOR` (their `ASM_READLINE`-sourced
lines always start at offset 0), and `CHECK_SYNTAX` sets it to `RESTOFF`
(nonzero for an explicitly-numbered line, past the `"10 "` prefix).

Covered by `Editor.RejectsBadSyntaxImmediatelyAtEntryWithoutCheckingLabels`
(`tests/editor_test.cpp`): a bad mnemonic is rejected and never stored,
and a forward-referencing `JMP` to a not-yet-defined label is accepted
and stored correctly. Test fixtures store only text that is valid 6502
source or a comment: the filler used to exhaust the source buffer is a
comment-only line (`; AAAA…`), which `CHECK_SYNTAX` skips exactly like
`ASM` itself, and operands are hex (`$xx`), since this assembler takes hex
operands only and a decimal `JMP 10` is always invalid.
`Assembler.RejectsAnUnknownMnemonicImmediatelyAtEntryNotJustAtAsm` pins
that an unrecognized mnemonic is caught at entry and never reaches `ASM`'s
`"ERR LINE n"`.

### 12.x `SHELL_START` banner starts on its own row

Typing `8000R` at Wozmon's real prompt to enter the shell hits its
genuine "AAAA R" **examine-then-run** syntax. Real Wozmon echoes
`"8000: <byte>"` (its own EXAMINE-mode echo, `wozmon.s`'s `NOTCR`/`ECHO`)
*before* the `JMP`, with no trailing CR/LF of its own, authentic 1976
Apple-1 behavior (the `4C` is just the actual opcode byte stored at
$8000). `SHELL_START` prints CR/LF before `MSG_SHELL`, so the banner
lands on its own line regardless of how the shell was entered
(`"8000: 4C"` then `"6502 ASSEMBLY CODER"` on the next row). This is
pure terminal-row placement with no branch or state to assert beyond what
the boot-into-shell tests already exercise.

### 12.z SAVE's on-screen dump advances a row per line

`SAVE`'s wire format is deliberately bare-CR-separated between lines (no
LF, see load.s's own header, matching the same "<num> <text>\r" shape
`LOAD` parses back), which is correct for the actual transfer and for
anything genuinely listening on the wire. Displayed raw, a bare CR just
returns a real terminal's cursor to column 0 without advancing a row, so
each line would overwrite the previous one and the trailing `Ok` would
land on top of whatever is left of the last line. This is the same
overwrite artifact `DO_LOAD`'s own incoming-echo works around on the ROM
side (its header comment), and the SAVE direction is handled client-side
instead (`app.js`'s `pullSerial()`), not in the ROM. The actual bytes
transmitted stay byte-exact (so the file format, LOAD's round-trip, and
anything really listening on the ACIA see the real, plain wire content).
Only `outQ` (this page's own on-screen draw queue) gets a synthetic LF
appended whenever a CR isn't immediately followed by a real one, tracked
via a `pendingLf` flag that persists across animation frames (so it's
correct regardless of where a chunk boundary falls relative to a CR/LF
pair). `saveCapture` and the banner-detection logic both see the
untouched original byte.

Covered by `tests/saveload.spec.ts`'s "typed SAVE prints each line on its
own row..." test, which reads xterm's own line buffer directly
(`window.__term.buffer.active`, exposed alongside `window.__machine`)
rather than scraping `#screen`'s flattened text, since row placement is
exactly what's under test.

### 12.aa Board graphic, Power toggle, and the landing-page thumbnail

The board SVG carries function labels, not reference designators
(`U1`/`U2`/.../`J1`/.../`SW1`). It is hand-maintained inline in
`index.html` (see `gen_pcb_svg3.py`'s own header comment; the script
itself wasn't preserved from whatever session produced the original
output). Labels: `6502 CPU`, `ROM` and `RAM` each with a `32K` line (both
are genuinely 32K parts, `62256` SRAM and `28C256` EEPROM, confirmed
against [`cpu6502/pcb/pcb6502/FullBoard/pcb6502.kicad_pcb`](https://github.com/gsirhc/retro/blob/main/cpu6502/pcb/pcb6502/FullBoard/pcb6502.kicad_pcb)'s own `Value` properties, not guessed), and crystal
labels `1 MHz` / `RS232 1.8432 MHz`. The three LEDs
(`D1`/`D4`/`D7`) read `Pwr`/`Rx`/`Tx`, their actual function, matching
`pcbLedD1`/`D4`/`D7`'s own `led-power`/`led-rx`/`led-tx` classes.
`.lbl-chip-sub` (the small sub-label text style) is used only for the
`32K` sub-labels.

**RST**: the pushbutton (`SW1`) is the real click target (`app.js`'s
`[data-ref="SW1"]` listener) and its text is `RST`, right by the actual
clickable part. There is no label for U9, the DS1813 reset-supervisor
chip. A padded invisible `.pcb-hit` rect inside SW1's group, well beyond
the visible button housing, makes the click target more forgiving than
the tightly-drawn silkscreen footprint alone.

**Power (J1)**: the barrel jack graphic is clickable, toggling a
`poweredOn` flag. `frame()`'s main loop skips CPU/serial/LCD work entirely
while off (CPU genuinely frozen, not just visually paused; verified by
`cycleCount()` staying flat across a wait in the Playwright test),
keystrokes are ignored, and D1 dims (`led-power` class removed). This has
**no real-hardware equivalent**: the actual board has no power switch,
J1's just a jack, always live when plugged in. So it's flagged in both
`app.js` and the Help/board-panel prose as a labelled UI convenience, not
a hardware claim. It pauses in place rather than resetting, so a program
mid-edit survives a "power cycle", closer to "the monitor's unplugged"
than "the machine lost its memory."

**J2 (DB9)** is labelled `RS232`.

**J7 (interrupt routing) and J5 (BOOT select)** have no row or controls
below the board and no `app.js` listeners (`setViaIrqRoute`/
`setAciaIrqRoute`/`setBootSelect` aren't called). `bus.h`'s own
`JumperState` struct defaults (`via_irq_route = Irq`, `acia_irq_route =
Nmi`, `boot_select = 0`) already match the shipped ROM's wiring with zero
JS needed, confirmed by reading those defaults directly rather than
assuming. J8 is covered in 12.ab.

**Landing-page thumbnail**: `assets/cg-oac-6502-board.svg`, extracted from
this same board markup with its own self-contained `<style>`. Inline SVG
embedded in an HTML document tolerates things strict XML doesn't, e.g.
`--` inside an HTML comment, and `<img src=*.svg>` parses strict XML, so
the extraction strips any HTML comment. It has a widened, letterboxed
`viewBox` (light-gray backdrop) matching the landing page's other two card
thumbnails' 286×128 aspect ratio, so the row of three stays visually
consistent. Checked by rendering it standalone (`qlmanage -t`) and
inspecting the output, real image content and not just "no parse error",
since a class name typo or a mispositioned label wouldn't show up any
other way.

### 12.ab Board graphic details

Checked by re-rendering and zooming into the board graphic (same
`qlmanage -t` technique as 12.aa), since several of these are only
visible at pixel level.

**U2 (ROM)'s pins sit inside its body.** The body rect is 17.74 units
tall, but its 14-pins-a-side rows (real 28C256 spacing) would span
±16.51, overshooting both top and bottom. Enlarging the body to match
would collide with RAM (U3), which sits at an equally real, KiCad-traced
position directly below with no slack, so there's no room in this
diagram's layout for a literal 28-pin-DIP-sized ROM body. The graphic
keeps the real pin count (14/side) and the original body size, and
compresses the pin spacing (~0.44×) to fit inside it: same visual "this
is a 28-pin chip" impression, no overhang, no layout collision. Every
other chip's pins sit inside its body with the same ~1.6-unit margin
(checked by diffing every chip's body bounds against its own pin range).

**LED labels** (`Rx`/`Tx`/`Pwr`) sit above their LEDs.

**`REG`** keeps its chip shape with no text, matching how the small
jumper headers go label-less (12.aa).

**J2 is labelled `RS232`**, and **Y1's crystal** carries a purpose label
alongside its frequency, `RS232 1.8432 MHz`, since it's the ACIA's
baud-rate crystal.

**J8 (RTS→CTS)** has no overlay checkbox either. Like J7/J5 in 12.aa, it
defaults to the shipped ROM's own wiring (`rts_to_rs232_cts = false` in
`bus.h`, "no trace populates this, it's jumper wire only", never asserted
by anything in this design). The `.pcb-overlay` CSS block, which only ever
styled J8's div, is gone with it.

**RST's red button cap is clickable.** `.sw-button` (the circle) and
`.sw-body` (the housing rect underneath) both carry `cursor: pointer`, so
hovering the part that visually reads as "the button" gives a click
affordance. The click handler is bound to the parent `<g>`, covering the
whole group via event bubbling.

## 14. Test-suite structure: `Machine` moves, the terminal locator, timeouts and focus

`assembler6502-test` and `assembler6502-web-test` run in CI. The native
GoogleTest suite (`tests/`) and the Playwright suite (`web/tests/`) rely on
the four points below.

### 14.a `Machine` is safely movable

Every GoogleTest helper across `assembler_test.cpp`/`editor_test.cpp`/`load_test.cpp` boots the real firmware via a pattern like:

```cpp
Machine bootIntoShell(std::string& out) {
    ...
    Machine m;
    m.on_serial_out = [&](uint8_t c) { out += char(c); };
    ...
    return m;
}
```

`Machine` (machine.h) wires several `this`-capturing lambdas into itself at construction: `cpu`'s bus read/write callbacks and the VIA `on_pa_change`/`read_pb` LCD-strobe hooks. A named local returned by value only reliably elides the copy/move when the compiler can apply NRVO, which the standard never guarantees and compilers don't apply uniformly. `bootIntoShell`'s early-return branch (`if (!f) { ADD_FAILURE(); return Machine(); }`) gives the function two distinct return expressions, which is the shape that tends to defeat NRVO. Clang elides the copy there where GCC 13 does not, so correctness can't depend on elision: a moved-into `Machine` whose lambdas still captured the old object's address would dereference a dangling `this`.

`Machine` is therefore properly, safely movable. `Cpu::rebind_bus()` re-points just the callback, leaving every register/cycle-count field alone (the `cpu = Cpu(newBus)` pattern the constructor uses would reset those). The wiring lives in a private `wire()` method, and `Machine` has an explicit move constructor/assignment that moves every member then calls `wire()` again so the callbacks re-point at the new object. Copy is deleted outright, since nothing legitimately needs two `Machine`s sharing one `on_serial_out`. Building with `-fno-elide-constructors` (forces every by-value return to actually move, the worst case regardless of compiler/optimization level) passes the whole suite.

### 14.b Text assertions use `#screen .xterm-rows`

xterm.js appends a hidden `<span class="xterm-char-measure-element" aria-hidden="true">` (textContent `"W".repeat(32)`, used purely to measure monospace character-cell width) as a sibling of the real rendered rows. Playwright's `toContainText`/`textContent`-based matching does not respect `aria-hidden` the way `.innerText()` does, so a locator on the whole `#screen` container picks up that hidden span's text too. That is normally harmless (the real content is *also* present, so `toContainText` still finds its expected substring somewhere in the concatenation) until the real content genuinely isn't there yet, at which point the assertion's failure message shows only the hidden span's `"WWWW..."` filler, which reads exactly like corrupted terminal output.

Every `#screen` locator used for text assertions is therefore scoped to `#screen .xterm-rows` (the real rendered-rows container; excludes the hidden measurement span) across all six spec files. `.xterm-rows`'s `textContent`/`innerText` match exactly (no leak), while the bare container's `textContent` also picks up an entire embedded `<style>` block's text on top of the hidden span. Where a test needs raw terminal output it spies on `Machine.readOutput` instead of the DOM (§13).

### 14.c Boot timeouts allow for 2-worker CPU contention

The suite's `workers: 2` setting runs two real `requestAnimationFrame`-paced 1MHz CPU cores in two separate Chromium processes sharing one host's CPU (the config's own comment names this tradeoff). Under real contention, in CI and on an otherwise-idle dev machine busy with unrelated background processes, a from-cold boot occasionally takes longer than a 25s/5s/10s assertion would allow. That is expected, correct behavior for a wall-clock-paced emulator under resource contention (`CLAUDE.md`'s "never speed up the CPU" rule means the answer is a more generous wait, not a faster boot). The boot-banner waits are 45s, the shorter follow-up assertions 20s (matching the global `expect.timeout` default), and the overall per-test timeout 90s, leaving room for the rest of a test after a slow boot.

### 14.d `beforeEach` returns focus to the terminal

`test.describe("Save / Load")`'s `beforeEach` opens the floating Save/Load popup with `page.click("#saveBtn")` as its last step, preparing every test's fixtures for `#pgmName`/`#pgmSave`/`#pgmFile`/`.chip-lib` interactions. Clicking a button moves `document.activeElement` onto that button (ordinary browser behavior), and xterm.js's helper `<textarea>` needs actual DOM focus to receive `page.keyboard` input at all. Without a refocus, a test's very first `enterProgram()`/`typeLine()` call, typing the shell-entry address and a program, would type into the Save button and reach the emulator not at all.

That is invisible in tests that only check Save's own UI feedback (a "Saved" status label, a download link, a chip-lib entry), none of which requires the typed *content* to have landed. It matters for every test that verifies the saved/loaded program's actual text: typed SAVE's line-by-line row check, Load-from-shelf's round-trip, the anti-re-entry-pollution test, Load's own-echo overlap check, and the plain-text-import test. `beforeEach` therefore ends with one more `page.click("#screen")`, refocusing the terminal before any test body runs. Individual tests that interact with the popup again mid-test keep their own later refocus clicks, which are still needed for typing that happens after a *second* popup round-trip.

CI runs with `retries: 2`, so a test that only passes on retry is reported flaky.

## 15. `FREE`, reporting source/object memory usage

`FREE` reports bytes used/free in the source buffer ("PROGRAM") and the
assembled object-code region ("EXEC", how much the last `ASM` took up),
e.g.:

```
>FREE
PROGRAM: 357 USED, 3737 FREE
EXEC:    78 USED, 10162 FREE
```

**PROGRAM** is computed live by calling `SCAN_BUFFER` with `LINENUM=$FFFF`
(the same "don't care about a match, just want `ENDPTR`" trick
`AUTO_NUMBER` uses) and comparing the returned sentinel address against
`STORE_LINE`'s own real capacity ceiling, `SRC_END-2`. The "-2" matters:
it's the same two bytes `STORE_LINE`'s `FULL` check reserves for the
buffer's own `0,0` end sentinel, so `FREE` reports exactly the room a real
`STORE_LINE` call would agree is available, not an off-by-two optimistic
number.

**EXEC** reads `ASMPC`, which holds the address right after the last byte
`ASM_PASS2` emitted, so there's no second variable to drift out of sync
with it. Before any `ASM` has run, `ASMPC` is `OBJ_START` (the "no program
assembled yet" address): `SHELL_START` seeds it on shell entry unless a
program is already assembled (`HASOBJ`, §21), and `NEW` reseeds it (§20).
`NEW` therefore resets `EXEC` along with `PROGRAM`, and `FREE` reports
`EXEC: 0 USED` until the next real `ASM`.
`Editor.FreeReportsSourceAndObjectUsageSeparately`
(`tests/editor_test.cpp`) covers fresh, after-store, after-`ASM` and
after-`NEW`.

## 16. Examples load and the serial-injection wedge (open)

Examples no longer go through the live serial path (§18, §19). That path
has a known failure that has no identified root cause. Feeding an Example's
source through it from the browser (the fetch-from-server Examples chips,
added alongside `FREE`) wedged the emulated CPU: the terminal stopped
producing any output at all, and the CPU ended up spinning forever inside
`bios.s`'s `IRQ_HANDLER` stub (a bare `pha`/`pla`/`rti` that exists only
so a *user's own* program enabling a VIA interrupt via J7 has somewhere
safe to land; nothing in this ROM's own boot path should ever reach it,
and `reset_via` explicitly disables every VIA interrupt source).

**What it isn't**, ruled out one at a time against the real browser, each
independently reproduced/un-reproduced:

- Not `fetch()` itself. Moving the example fetches out of the click
  handler entirely (prefetched up front, awaited before the machine's own
  `frame()` loop even starts) made no difference.
- Not bulk queue size on its own. A same-sized synthetic program with no
  comment lines, queued the exact same way, never reproduced it.
- Not reproducible in a native (non-WASM) build at all. A C++ port of
  `driveFrame()`'s exact per-frame/backpressure algorithm, fed the exact
  same file bytes, completes cleanly every time. Whatever this is, it's
  specific to the real browser/WASM runtime, not the ROM logic or the
  pacing algorithm in the abstract.
- Not a simple "ring overflow" in the sense the original 200-byte
  backpressure threshold (see the comment in `app.js`) was designed to
  prevent. The ring never reported itself full.

**What's confirmed**: lowering that backpressure threshold from 200 down
to single digits, keeping the `SERIAL_BUFFER` ring close to empty at all
times instead of letting it ride near capacity, reliably fixes it for
`hello.asm`, `primes.asm` and `lcd_demo.asm`. It does **not** fix
`rps.asm` (~3.2K, roughly 2.5x `primes.asm`), which still wedges partway
through even at the most conservative threshold tried (breaking on any
unconsumed byte at all). Making injection *more* conservative moved
`rps.asm`'s failure point *earlier* in the byte stream in one comparison,
not later, which suggests the wedge may correlate with elapsed real time
or frame count rather than bytes transferred or ring pressure. That isn't
confirmed either.

**Status**: the shipped threshold is the lowered one. Hand-typed programs,
Save, Load-from-file and the saved-programs shelf still go through the
paced ACIA/NMI path (§19). A retry loop or a smaller/chunked transfer for
the largest files is the likely next thing to try if the root cause is
picked back up.

## 17. `.BYTE`, a data directive

`.BYTE "text"[,$xx,...]` stores raw data instead of an instruction, one
byte per quoted character and/or one byte per `$xx` hex literal,
comma-separated (e.g. `MSG: .BYTE "HI",$0D,$0A,$00`). A program that calls
`PRINT_STR`/`LCD_PUTS` can carry its string as data instead of building it
with an `LDA #$xx`/`STA` pair per character.

**Where it plugs into the two-pass design.** `PARSE_LINE` separates
"resolve this line's shape" (both passes) from "actually write bytes"
(`EMIT_INSTRUCTION`, pass 2 only), driven by `AMODE`/`ASIZE`, and `.BYTE`
reuses that seam rather than a parallel path. `MATCH_BYTE_KEYWORD` checks
for the literal token right where the mnemonic field would otherwise be.
It runs before the 3-char `MNEM_LOOKUP` a real 6502 mnemonic always uses,
since `.BYTE` is 5 characters and has to be intercepted *before* that
fixed-width assumption. On a match it sets `AMODE = AM_BYTES` (9, one past
`AM_REL`, never a real `OPTAB` column) instead of running
`MNEM_LOOKUP`/`RESOLVE_MODE` at all. `ASIZE` is the item list's total byte
count, computed by `SCAN_BYTE_LIST` in "count only" mode (`BYTEMIT=0`).
Because `ADVANCE_PC` just does `ASMPC += ASIZE` regardless of what produced
that number, a `.BYTE` line participates in label placement with no
special case, both for a label defined on a `.BYTE` line and for a later
label/branch that needs to land after one (see
`ByteDirectiveAdvancesThePcSoALaterLabelResolvesPastIt`,
`assembler_test.cpp`).

**Pass 2 re-scans rather than caches.** A normal instruction's resolved
`OPVAL` is one 16-bit value, trivial to carry from `PARSE_LINE` into
`EMIT_INSTRUCTION` in a couple of zero-page bytes. A `.BYTE` line's data
can be dozens of bytes (the assembler already checked `LINE_BUF`'s cap
during scanning, so it fits, but not in a fixed 2-byte scratch var).
`EMIT_INSTRUCTION`'s `AM_BYTES` case re-derives `CURSOR` from `MNEMOFF`
(stashed by `PARSE_LINE`, pointing at the start of `.BYTE` in `LINE_BUF`,
which pass 2 hasn't touched since re-reading this same line via
`ASM_READLINE`) and calls `SCAN_BYTE_LIST` a second time, this time with
`BYTEMIT=1` so it also writes each byte to `(ASMPC),y`. Recomputing
instead of caching matches how this file treats every other
pass-1-then-pass-2 line, and the second routine (`SCAN_BYTE_LIST`) is
shared by both call sites instead of two near-duplicate scanners.

**No `<`/`>` operators, so `.BYTE` can't feed `PRINT_STR`/`LCD_PUTS`
directly.** Those OS calls need the string's 16-bit address split across
A/Y, and this assembler has no way to compute "the low/high byte of a
label" as an operand (`PARSE_OPERAND`'s header). A `.BYTE` string is
walked with an indexed loop instead (`LDA MSG,X`, already-supported
absolute,X addressing, `/ BEQ done / JSR PRINT_CHAR / INX`): one small
loop instead of one `LDA`+`JSR` pair per character. Adding `<`/`>` to make
`PRINT_STR` reachable in one `JSR` is an open item (§11).

**Comment-stripping runs before `.BYTE` ever sees the line, so a string
can't contain `;`.** `ASM_READLINE`/`CHECK_SYNTAX` both strip everything
from the first `;` onward before `PARSE_LINE` is called at all (the same
mechanism that makes a bare `;` in a normal operand fail, per
`PARSE_LINE`'s own header). Making that stripper quote-aware would touch
two call sites for a corner case (a semicolon inside displayed text) real
period assemblers generally don't special-case either, so it's documented
as a limitation (the file header and the Help panel). A `"` can't appear
in a string either, for the same "no escaping" reason `HEX_PARSE`-based
literals elsewhere in this assembler don't support escapes.

Covered by `assembler_test.cpp`'s `ByteDirective*` tests: a plain quoted
string, a string mixed with `$xx` literals, PC advancement past a `.BYTE`
line, an end-to-end assemble-and-`RUN` that prints a `.BYTE` string out
the real ACIA via an indexed loop, and the three rejected-input cases
(empty operand, unterminated string, oversized hex literal). GoogleTest
only: this is assembler/codegen behavior with no UI surface, and the
existing Help-panel and syntax-error Playwright specs exercise the
surrounding chrome.

## 18. Examples load through a direct RAM poke

An Example bypasses the per-character serial simulation in `app.js`
entirely, which sidesteps §16. The Examples buttons fetch a pre-built
`.bin` of the program's source and poke it straight into emulated RAM.

**Build.** A build-time-only native tool, `web/gen_example_bin.cpp` (built
with the host's own C++ compiler by `make examples-bin`, never shipped),
boots the real ROM headlessly and types each `examples/*.asm` file the way
a human would: `NEW`, each line, then a `JMP $<SHELL_PROMPT>` resume line
as the program's last line. It reads the real, current `SHELL_PROMPT` out
of `firmware.lbl` (the same technique `gen_entrypoints.py` uses), since
`SHELL_PROMPT`, not `SHELL_START`, is the documented resume target (§13).
Without that line a finished program would fall off its own end into
zero-filled RAM (a BRK/IRQ cascade) instead of returning to a clean
reprompt. It then runs `ASM` as a fail-loud correctness check, so a broken
Example never reaches a visitor, and discards the assembled output. The
`.bin` holds one part: `[2] srcLen [srcLen source bytes]`, the source
buffer from `SRC_START` to its `0,0` sentinel.

**Load.** A WASM binding, `Machine::pokeRam(addr, bytes)`
(`wasm_machine.cpp`), writes bytes directly into the emulated RAM array.
`app.js`'s Example click handler fetches the `.bin` and `pokeExample()`
pokes the source at `SRC_START`. No `typeChar()`, no ACIA, no NMI, no
`driveFrame()` backpressure loop is involved. `LIST`/`EDIT` show the
genuine source and `FREE` reports real `PROGRAM` usage. The visitor still
types `ASM` and watches the board's own two-pass assembler compile it,
exactly like a hand-typed program. Only the *typing of the source* is
skipped, not the compiling. This is the one place the source appears
without streaming in through the ACIA, and it touches only the four
bundled Example programs.

## 19. Example load feedback on the terminal, no transfer-speed override

`pokeExample()` brackets its `pokeRam()` call with
`injectOutput("LOAD\r\nOk\r\n")`, faking the terminal echo a real typed
`LOAD` produces. A real `LOAD` command's own `DO_LOAD` dispatch can't be
used: `bios.s`'s `SERIAL_BUFFER` RX ring is 256 bytes, far smaller than
any real Example's source (577 bytes for the smallest, `hello.asm`), and
`DO_LOAD` blocks on serial bytes that never arrive via a raw RAM poke.
`Machine::injectOutput(bytes)` (`wasm_machine.cpp`) appends bytes directly
to the same output queue `readOutput()` drains every frame, the exact
pipeline genuine ACIA output flows through, so nothing downstream
(including the Playwright raw-output-spy test helper) needs a special
case. After the faked `Ok` it also appends a bare `>` (§21).

**There is no "instant transfer" control.** The Save/Load popup
(`index.html`) has no speed checkbox, and `driveFrame()`'s pacing checks
only `TEST_MODE`, the `?test=1` carve-out. Hand-typed programs, Save,
Load-from-file and the saved-programs shelf all go through the real paced
ACIA/NMI path. Only the Examples buttons skip the transfer, and they skip
it by poking source, not by speeding the ACIA up.

`web/tests/saveload.spec.ts`'s "Load (an Example)" test confirms the
LOAD/Ok terminal text appears (via the raw-output spy), that `LIST` shows
the real poked source, and that `ASM` then `RUN` produces the program's
real output. `RUN` without a prior `ASM` lands on the no-program trap
(§21).

## 20. `NEW` resets the object region too, not just source

`NEW` clears the assembled program as well as the source, so `NEW` then
`RUN`, with no `ASM` in between (a plausible workflow: start over, forget
you haven't compiled yet), doesn't re-execute the *previous* program from
bytes still sitting at `OBJ_START`. `DO_NEW` zeroes the source buffer's
2-byte end sentinel, plants the no-program trap at `OBJ_START` (§21) and
reseeds `ASMPC` to `OBJ_START`, mirroring `SHELL_START`'s own cold-boot
seed, so `FREE` reports `EXEC: 0 USED` again until the next real `ASM`.
`ASM` itself always reseeds `ASMPC` to `OBJ_START` before re-emitting. The
header comment on `SHELL_START`'s `ASMPC` seed describes the same rule.

Covered by `NewClearsAssembledObjectCodeTooSoAStaleRunCantExecute`
(`tests/editor_test.cpp`): it assembles and `RUN`s a small program (with
the documented `JMP SHELL_PROMPT` resume line, so the shell is genuinely
alive again afterward), confirms it really executed, `NEW`s, asserts the
object region now holds the trap, then `RUN`s again with no intervening
`ASM` and confirms the old program's effect (a value written to a fixed
zero page address) does *not* recur. `FreeReportsSourceAndObjectUsageSeparately`
expects the post-`NEW` `EXEC` figure above.

## 21. `RUN` with nothing assembled prints `?NO PROGRAM`

**The empty state is a trap, not zeroes.** Zero bytes at `OBJ_START` are
a `BRK` opcode, and `RUN` (`jmp OBJ_START`, no guard) would land there and
spin forever in `bios.s`'s bare `IRQ_HANDLER` stub (`pha`/`pla`/`rti`;
`BRK` pushes and re-enters at the same place, over and over), which looks
exactly like a hang. `PLANT_NOPROG_TRAP` instead writes a real 3-byte
`JMP ERR_NOPROG` at `OBJ_START`. `RUN` needs no guard of its own: it lands
on real code that prints `?NO PROGRAM` (matching the `?LINE`/`?SYNTAX`
short-error convention) and `JMP`s back to `SHELL_PROMPT`. The trap costs
3 bytes regardless of how big the last assembly was, so there's no
byte-range clear.

The trap is planted from three places, each documented at its own call
site:

- **`DO_NEW`**, unconditionally. Starting fresh always means this.
- **`DO_ASM`**, at the very start of every attempt (so a `RUN` typed
  mid-edit, before the *next* `ASM`, can't hit a stale success from
  before), and again in `da_err` (so a *failed* re-`ASM` can't leave
  `RUN` pointed at a half-written object region from an `ASM_PASS2` that
  errored partway through, nor at an *older*, now-out-of-sync-with-the-
  visible-source successful assembly). A clean pass 2 overwrites the trap
  with the real program's own first bytes, since it emits from
  `OBJ_START` too.
- **`SHELL_START`**, conditionally, gated on the `HASOBJ` zero-page flag
  (set by a clean `DO_ASM`, cleared by `PLANT_NOPROG_TRAP` itself). Unlike
  the other two call sites, entering the shell must NOT always replant the
  trap: real RAM persists across a `QUIT` then a fresh `8000R` re-entry,
  exactly like real hardware, so a program already cleanly `ASM`'d before
  `QUIT` is still genuinely `RUN`nable and must survive. `HASOBJ` is what
  makes that distinction possible. `SHELL_START` leaves `ASMPC` alone
  whenever `HASOBJ` says a real program is still there, so `FREE` reports
  correctly after `QUIT` and re-entry.

Covered by four GoogleTest cases: nothing ever assembled (fresh shell
entry, straight to `RUN`), `NEW` then `RUN` (checks for the trap's `$4C`
opcode and the `?NO PROGRAM`/reprompt text), a failed re-`ASM` after a
good one (proving the *old* program doesn't leak through), and
`QUIT` then re-entering the shell (proving a real, untouched assembly
*does* survive and still runs, `?NO PROGRAM` absent).

**The Examples' faked LOAD/Ok reprompts.** A raw RAM poke doesn't touch the
program counter or call `shell_loop`, so after `pokeExample()` prints its
faked `"LOAD\r\nOk\r\n"` (§19) the real CPU is still sitting wherever it
was, mid-`READLINE_ECHO`, having already printed whatever prompt was
there. The real `shell_loop` prints a bare `>` (no CR/LF) before every
`READLINE_ECHO`, so `pokeExample()` appends that same bare `>` after its
faked `"Ok\r\n"`. The terminal's appearance then matches the real,
idle-and-ready CPU state, and the prompt is visible without a blank-Enter
to force a reprompt. This is cosmetic: the shell accepts input
immediately either way. `saveload.spec.ts`'s "Load (an Example)" test
asserts the raw output ends `Ok\r\n>` after the fake confirmation.
