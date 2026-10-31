# Joust -- hardware review

What the emulator does and where each behaviour comes from. Open gaps are
in [JOUST_PARITY.md](JOUST_PARITY.md).

## 1. Primary sources

- **Joust Drawing Set** (Williams 16P-3, basementarcade.com
  [joustsch.pdf](https://www.basementarcade.com/arcade/library/manuals/j/joustsch.pdf)).
  Pages 10-12: CPU board logic sheet 1 (clock, video counter, blanking,
  watchdog, DRAM, 3G/3C PROMs). Pages 13-14: sheet 2 (address decode,
  palette, CMOS and memory protect, Special Chips). Page 17: interface
  board (74LS257 player muxes). Pages 19-21: ROM board D-9144 (8E decode,
  sockets 4E-7E, ROM PIA). Pages 31-32: sound board D-8224.
- **Joust operator manual** (basementarcade.com
  [joustman.pdf](https://www.basementarcade.com/arcade/library/manuals/j/joustman.pdf)):
  coin door switches, switch test list (p.11), CMOS adjustments.
- **MC6809 Programming Manual**, M6809PM/AD, the HTML transcription at
  [maddes.net/m6809pm](https://www.maddes.net/m6809pm/sections.htm):
  instruction set, interrupt stacking, Appendix F cycle counts and
  Table F-2 indexed extras.
- **MC6809E data sheet** (Motorola,
  [bitsavers](https://www.bitsavers.org/components/motorola/_dataSheets/6809E.pdf)):
  Figures 8 and 9 (IRQ/NMI and FIRQ entry timing), Figure 14 (flowchart)
  and Figure 17 (cycle-by-cycle, including CWAI).
- **MC6821 data sheet**, DS9435-R3
  ([copy](https://git.applefritter.com/Apple-1-HW/RC6502-Apple-1-Replica/raw/branch/master/documentation/datasheets/MC6821.pdf)):
  control register bits, CA1/CB1 edge select, C2 output modes, IRQ.
- **M6800 Programming Reference Manual**, M68PRM(D), Nov 1976
  ([bitsavers](https://bitsavers.org/components/motorola/6800/Motorola_M6800_Programming_Reference_Manual_M68PRM%28D%29_Nov76.pdf)):
  the sound CPU's instruction set and cycles.
- **Sean Riddle's Special Chip tests on real boards**
  ([blitter.html](https://seanriddle.com/blitter.html),
  [blittest.html](https://seanriddle.com/blittest.html)): register map,
  control bits, the SC1 width/height XOR, timing, and
  [blitdump.zip](https://seanriddle.com/blitdump.zip), RAM dumps after 256
  test blits run on a real Joust board.
- The Joust ROMs themselves, disassembled by hand for input bits, coin
  switch use and the CMOS layout.

## 2. Main CPU and clocks

The CPU board's 12 MHz crystal CR1 feeds 7K/7F, which make the 6809E's E
and Q at 1 MHz (sheet 1). `joust::kCpuHz` is 1,000,000 and the page paces
the guest to wall-clock time through `arcadeLoop`. The CPU core is
`shared/cpu/cpu_m6809` (§12).

## 3. Video counter and blanking

74LS161s 5F/5E/5D/5C count VA0-VA13 on E. VA0-VA5 is the horizontal count,
64 counts (64 µs) per line; VA6-VA13 is the line (sheet 1).

- **Frame.** At the end of line $FF the counter loads 5C = %1111,
  5D = %1100, so the next line is $FC. 4D alternates the load so a frame
  is lines $FC-$FF and then $00-$FF: 260 lines, 16,640 E cycles,
  `kFrameLines` / `kFrameCycles`.
- **Horizontal blanking.** The 4B/5B latch makes counts 3-52 visible:
  50 counts × 6 pixels = 300 pixels (`kFirstVisibleCount`,
  `kVisibleCounts`, `kScreenW`).
- **Vertical blanking.** 6C latches D = (line ≥ $F8) on VA6, so lines
  $01-$F8 are visible, 248 rows (`kFirstVisibleLine`, `kScreenH`).
- **Video count read.** A read of `$CB00` returns VA8-VA13 on D2-D7
  through the 3B 8T97 (`line & $FC`).

## 4. Memory decode

### 4.1 DRAM and the 3G PROM

Three banks of 4116s, 16K each. The 3G PROM (`decoder.4`) takes CPU A8-A15
and gives bank select on D6-D7 (into 4G for WE1/WE2/WE3) and the DRAM page
on D0-D5 (sheet 1). With the real PROM, CPU column h lands at slot
(h + 5) mod 192, bank = slot mod 3, page = slot / 3; three consecutive
columns share a page, one per bank, so the video side can fetch them in
one cycle. Entries ≥ $C0 select no bank; in the real PROM that half holds
ASCII text. `Machine::dram_index` reads the PROM, not the rule; the
self-test set's PROMs are generated from the rule.

The upper half of `decoder.4` is used when the SCREEN CONTROL latch bit is
set (cocktail flip).

### 4.2 ROM, latch and I/O

| Address | Device | Source |
|---|---|---|
| `$0000-$8FFF` | DRAM; reads see ROMs 1-9 when `$C900` D0 = 1 | ROM board 8E 74154 |
| `$C000-$C00F` | palette (74189 ×2), write only | sheet 2 |
| `$C804-$C807` | widget PIA (player inputs) | sheet 2, interface board |
| `$C80C-$C80F` | ROM PIA (coin door, sound select) | ROM board |
| `$C900` | D0 ROM enable, D1 SCREEN CONTROL | sheet 2 |
| `$CA00-$CA07` | Special Chips (§6) | sheet 2 |
| `$CB00` | video count read (§3) | sheet 1 |
| `$CBFF` | watchdog (§10) | sheet 1 |
| `$CC00-$CFFF` | 5114 CMOS (§9) | sheet 2 |
| `$D000-$FFFF` | ROMs 10-12 | ROM board |

Writes to `$0000-$8FFF` always reach the DRAM, whatever the ROM enable.

## 5. Video output

Each horizontal count, the 1H/3H/4H latches take one byte from each of the
three banks at the page the counter addresses, and shift them out as six
4-bit pixels on the next count. The upper nibble is the left pixel. The
3C PROM (`decoder.6`) maps the line to the DRAM row; in the real PROM
line L reads row L - 1. Its upper half is the flipped map, picked by
SCREEN CONTROL (§4.1).

The 74189 palette holds 16 bytes, BB GGG RRR. Each bit drives a resistor
into the colour driver: red and green use 1.2K / 560 / 330, blue 560 /
330 (sheet 2, R1-R24). `Machine` sums each set bit's conductance against
the full ladder to get the level, so the steps are uneven, as on the
board.

## 6. Special Chip blitter

`shared/williams/blitter`. Two VL2001 Special Chips each handle one nibble
of the byte; the emulator models the pair as one device. Registers from
Riddle's blitter.html:

| Reg | Use |
|---|---|
| `$CA00` | control; writing it starts the blit |
| `$CA01` | solid colour |
| `$CA02/03` | source |
| `$CA04/05` | destination |
| `$CA06/07` | width / height |

Control bits: D0 source stride 256, D1 destination stride 256, D2 SLOW,
D3 FORE ONLY, D4 SOLID, D5 SHIFT, D6 suppress lower nibble, D7 suppress
upper nibble.

- **SC1 size quirk.** The SC1 parts XOR width and height with 4, and a
  size of 0 counts as 1 (Riddle). Joust compensates in its own code
  (`$4A97`).
- **Rows.** A linear (stride 1) side carries on contiguously into the next
  row; a stride-256 side starts the next row at its previous row start
  + 1.
- **Shift.** SHIFT moves the data right one pixel. The carried nibble
  persists from one row into the next and is cleared only when a blit
  starts.
- **Inhibits.** Each nibble is inhibited when its suppress bit XOR
  (FORE ONLY and the source nibble is 0) is set. The chips never read the
  destination: they drive /WINHU and /WINHL into the DRAM so an inhibited
  nibble is not written (sheet 2). An I/O destination sees a plain write.
- **Reads** go over the CPU bus, so with ROM enable set the source can be
  bank ROM.
- **Timing.** The chips halt the CPU for 1 µs per byte, 2 µs with SLOW,
  plus 5 cycles of halt overhead (Riddle's 6809E halt timing capture).

`Riddle.HardwareDumpsMatch` replays all 256 of Riddle's blits, each run
1000 times as he ran them, and compares every byte of the 48K DRAM to his
dumps from the real board. 255 are checked (one entry is marked unused)
and all match. His board's ROM sockets read back as zero, so the test
leaves the bank unpopulated.

## 7. PIAs and interrupts

`shared/williams/pia6821` follows the MC6821 data sheet: DDR/data select
on CR b2, CA1/CB1 edge on b1, the flag in b7, C2 as input (b6 flag) or
output (b5 = 1; b4 = 1 manual level from b3, b4 = 0 the read/write
strobe). IRQ out is `(CR & $81) == $81 || (CR & $68) == $48`. A strobe
that restores on E comes back high at the end of the instruction that
fired it, on both boards' PIAs.

ROM PIA (ROM board):
- CA1: COUNT 240 from 4C, high while line ≥ $F0.
- CB1: 4MS, VA11 (line bit 5), so it toggles every 32 lines.
- IRQA and IRQB both go to the 6809's /IRQ. Joust's IRQ vector is
  `$FF18`.
- Port A: coin door (§8). Port B PB0-PB5: sound select (§11).

The widget PIA's IRQs are not connected to the CPU.

## 8. Inputs

### 8.1 Player panel

The interface board's 74LS257 muxes (IC3/IC4) feed widget PIA port A.
Their select is the widget PIA's CB2: high reads player 1, low player 2.
Joust drives CB2 as a manual output ($3C high, $34 low in CRB) and reads
player 1 with it high (`$6BA6`).

| Bit | Input |
|---|---|
| 0 | left |
| 1 | right |
| 2 | flap |
| 4 | 2 player start |
| 5 | 1 player start |

The start buttons are on both mux halves. PB2-PB6 of the widget PIA are
tied low on the interface board.

### 8.2 Coin door

ROM PIA port A:

| Bit | Switch |
|---|---|
| 0 | AUTO-UP |
| 1 | ADVANCE |
| 2 | right coin |
| 3 | HIGH SCORE RESET |
| 4 | left coin |
| 5 | center coin |
| 6 | slam |

The coin bits are confirmed against the game: each one increments its own
CMOS audit counter (`$107` left, `$10D` center, `$113` right). HIGH SCORE
RESET is confirmed the same way: the routine at `$3DCB` needs bit 3 held
through 24 sleeps, then copies the default table at `$3E42` into CMOS
at `$CD3E`. ADVANCE is the bit the test loops at `$3C5D`/`$F370` wait
on. The page offers the left and right slots only.

## 9. CMOS

The 5114 is 1K × 4 at `$CC00-$CFFF`, on the CPU board's battery (sheet 2).
Only D0-D3 exist; reads return the nibble with the upper four bits high
(§15). `$CC00-$CCFF` can only be written when the memory-protect line is
high, which the coin door switch gives with the door open (6E/6G/6I).
`$CD00-$CFFF` is always writable. The CMOS survives a power cycle
(`Machine::reset` keeps it).

### 9.1 Persistence on the page

The battery keeps the whole CMOS, so the page saves the whole 1024-nibble
image per ROM-set CRC (IndexedDB `retroweb-joust`, key `hiscores`) and
writes it back before the board powers up. A blank CMOS is never saved.

With a blank CMOS and the door shut, Joust stops on ADJUSTMENT FAILURE.
Powered up with the door open, it restores factory settings
(FACTORY SETTINGS RESTORED) and halts; the next power-up with the door
shut runs the game. When a set starts with a blank CMOS, the page does
that for the visitor: it holds the door open for 1200 frames, then shuts
it and power-cycles. A note on the page says so while it runs. Reset HIGH
SCORE deletes the saved image and blanks the CMOS, like a dead battery,
and the setup runs again.

## 10. Watchdog

5I counts frames. 5G, a 13-input 74LS133, clears it on a write to `$CBFF`
with exactly `$39` on the bus (sheet 1). Besides the address lines, it
takes D0, D3, D4 and D5 directly, and D1/D2 and D6/D7 each through a 5H
7402 NOR, so D1, D2, D6 and D7 must be low: `%00111001`. The game always
writes `$39`. On the eighth frame without a clear, Q4 pulls /RESET; Sean
Riddle measured 8/60 s on a real board. Reset is held until the counter
wraps at 16, then the board restarts.

## 11. Sound board

`shared/williams/sound_board`, from the D-8224 schematic (pp.31-32).

- MC6808 at 3.579545 MHz / 4. `SoundBoard::advance` runs it in step with
  the main CPU's cycles.
- IC2 7442 decodes A12-A14: `$F000-$FFFF` is the 4K ROM; in `$0xxx`, A10
  selects the 6821 at `$0400`, otherwise the 6810 128-byte RAM at
  `$0000`.
- The ROM PIA's PB0-PB5 drive the select lines into the sound PIA's port
  B; W12/W13 hold PB6/PB7 high. IC6 4068 NANDs the six lines into CB1, so
  any value other than the idle `$3F` raises CB1.
- Port A drives the MC1408 DAC, PA7 the MSB; the output current is linear
  in the code.
- The amplifier output goes to the speaker through C14, 1000 µF into
  8 Ω, a DC-blocking high-pass (about 20 Hz). `SoundBoard::sample`
  models that pole.

## 12. CPU cores

`shared/cpu/cpu_m6809`: the whole MC6809 instruction set, cycle counts
from M6809PM Appendix F (all 170 page-1 opcodes checked against the
parsed appendix) and Table F-2 for indexed modes. IRQ and NMI stack the
entire state in 19 cycles, FIRQ only PC and CC in 10 (MC6809E data sheet
Figs. 8 and 9). NMI is edge-triggered and not armed until S has been
loaded. CWAI takes 16 cycles to stack the state and then one dead cycle
at `$FFFF` per cycle of waiting; an interrupt then costs the 2-cycle
vector fetch and a dead cycle, 3 in all (Fig. 17). With an interrupt
already pending that gives the manual's minimum of 20. SYNC parks the
core the same way.

`shared/cpu/cpu_m6800` (the sound board's 6808): the 6800 instruction set
and cycles from M68PRM(D). WAI stacks up front, so the wake costs the 4
vector cycles the manual gives.

## 13. Hardware self-test ROM

`roms/hwtest/gen_hwtest.py` assembles a from-scratch 6809 program into
the fixed ROM at `$D000`, a 6800 sound program, and decoder PROMs built
from the rules in §4.1 and §5. It sets up both PIAs and the palette,
draws 16 colour bars and then a crosshatch, and holds the help screen
(title, SELF TEST OK, then the copyright / load-your-own copy) drawn with
SC1 blits from the shared `hwtest_font.py` glyphs it places in bank ROM 1.
It feeds the watchdog from a loop on `$CB00`, and sends one sound command
so the board beeps through the DAC.

## 14. Front end

`web/app.js` runs the board at 1 MHz against the wall clock, the same
`arcadeLoop` as the other cabinets.

- Keys: P1 Arrows or A/D run, Space/Z/W/Up flap. P2 J/L run, K/I flap.
  5 left coin, 6 right coin, 1/2 start, F2 ADVANCE.
- Coin door panel: door open, AUTO-UP (shut means auto), ADVANCE, HIGH
  SCORE RESET and Power cycle. The door and AUTO-UP settings persist in
  `localStorage`, like a door left open on a real cabinet.
- Touch: a 2-way stick and a Flap button.
- ROM loader: a `joust.zip` or the loose chips. `3006-13.1b` …
  `3006-24.12b` are sockets 1-12 (bank 1-9, fixed 10-12), plus
  `decoder.4`, `decoder.6` and `joust.snd`, each checked by size.

## 15. Unsourced values

Chosen without a primary source, kept here until one turns up:

- CMOS reads return `$F0 | nibble`. The upper data lines float on the
  board.
- `$CB00` D0 and D1 read 0.
- Which edge the watchdog counter clocks on, and how long /RESET stays
  low after the counter wraps.
- 6809 EXG/TFR between an 8-bit and a 16-bit register: the programming
  manual and the MC6809E data sheet both call it undefined and invalid.
  The core moves `$FF00 | value` to a 16-bit destination and the low byte
  to an 8-bit one, and an undefined register code reads `$FFFF`. Joust
  never executes a mixed-size or undefined transfer.
- The sound board isn't reset by the main board's watchdog.

## 16. Tests

- `make -C joust test`: `machine_test`, `smoke_test` and `williams_test`
  (blitter, PIA, sound board, and the Riddle replay once
  `make -C joust riddle` has fetched the dump). `ctest -N` in
  `joust/tests/build` lists them.
- `make -C shared/cpu test-smoke-m6809` / `test-smoke-m6800`.
- `make -C joust/web test`: Playwright, including `smoke.spec.ts`
  (boot, 4:3 monitor, real 1 MHz pacing) and the shared arcade specs.
