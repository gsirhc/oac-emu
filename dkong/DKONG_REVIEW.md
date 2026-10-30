# Donkey Kong (Nintendo, 1981) — review

Citation trail for the US upright set (`dkong`) on the two-board TKG4
set. Present state only. Values that are calibrated or assumed rather than
read off a sheet are tracked in [DKONG_PARITY.md](DKONG_PARITY.md).

## 1. Primary sources

- Nintendo *Operation Manual*, model TKG4-UP (1981). DIP table (p.5),
  cabinet specs, parts lists for the CPU and video PCBs.
- Nintendo schematic *TKG4-11-CPU* / *TKG4-14-CPU*: Z80A, decode, 8257,
  latches, inputs, colour PROMs and the whole sound section.
- Nintendo schematic *TKG4-14-VIDEO*: sync chain, VRAM, character and
  object ROMs, object scan and line buffers.
- Datasheets: Zilog Z80, Intel 8257-5, Intel MCS-48 (8035), TI SN74LS123,
  SN74LS259, SN74LS164, SN74LS161, NE556, DAC-08, MB3614.
- The US program ROM's own code (`$0066` NMI, `$0141` DMA set-up, `$01BA`
  score table, `$025D` rank table), disassembled by hand.

Sockets and the dump names the loader accepts:

| Socket | Part | Dump name | Contents |
|---|---|---|---|
| 5E 5C 5B 5A | 2532 | `c_5et_g` `c_5ct_g` `c_5bt_g` `c_5at_g` | program `$0000`-`$3FFF` |
| 3P 3N | 2716 | `v_3pt` `v_5h_b` | character planes (3P high, 3N low) |
| 7C 7D 7E 7F | 2716 | `l_4m_b` `l_4n_b` `l_4r_b` `l_4s_b` | object planes |
| 2F 2E | MB7052 | `c-2j` `c-2k` | palette (R2 R1 R0 G2 / G1 G0 B1 B0) |
| 2N | MB7052 | `v-5e` | character colour codes |
| 3H 3F | 2716 | `s_3i_b` `s_3j_b` | 8035 program / data |

## 2. Main CPU and clocks

- Z80A (`shared/cpu/cpu_z80`) at `kCpuHz` = 3.072 MHz = 61.44 MHz / 20.
- Two pixels per CPU clock: 384 pixels and 192 CPU cycles per line. V
  counts 248…511, 264 lines a frame, 50688 cycles, 60.606 Hz.
- VBLANK is V-low ≥ `$F0` or < `$10`. Visible lines are V `$110`-`$1EF`,
  line indices 24…247 (`kFirstVisibleLine`, `kVblankLine`).
- NMI: flip-flop 8F clocks on the /VBLK edge and is cleared and held off
  while `$7D84` is 0. The US ROM's NMI drops `$7D84` on entry and sets it
  again before RETN.
- Reset is the power-on LS123 one-shot only. The board has no watchdog;
  a hung program stays hung.

## 3. Sound

### 3.1 8035

- MB8884 (8035) at 6 MHz, 400 kHz machine cycles (6 MHz / 15), stepped
  against Z80 time in `Sound::advance`.
- Program from 3H (2 KiB, mirrored through the 4 KiB window).
- MOVX / INS read the bus: with P26 high, LS367 4E puts the inverted 3D
  latch on DB0-3 and DB4-7 float high (`0xF0 | ~cmd`). With P26 low, 3F
  answers at `(P2 & 7) << 8 | addr`.
- `$7C00` → LS175 3D (4 bits) → LS04 3E → 4E.
- `$7D80` (5H Q0) → 5J inverter → /INT.
- `$7D03` pulls P25 low; `$7D04` drives T1 and `$7D05` T0, all through
  the 6J inverters, so a latch 1 reads as 0 on the 8035.

### 3.2 DAC

- P1 → DAC-08 (P17 = MSB). P27 → Q7 → C32 10µ / R20 10K sets the DAC's
  reference current, an envelope with τ = 0.1 s.
- MB3614 transimpedance stage (R23 5.6K), then a Sallen-Key low-pass
  (R21 = R22 = 5.6K, C33 0.022µ, C34 0.01µ).
- P24 → 5J → R45 into the 556 half A control voltage.

### 3.3 Discrete circuits

All three are latch-driven from LS259 6H through 6J.

- **Walk** (`$7D00`): R36 1K / R18 4.7K / C25 3.3µ / D5 / R17 10K / R35
  1K, chopped by Q6 while 556 half A is high, then C23 4.7µ / R16 5.6K /
  C22 0.047µ ∥ R15 5.6K into R14 47K.
- **Jump** (`$7D01`): C18 1µ → Q5 dumps C17 4.7µ (R30 100K, R29 10K).
  D7/D8 gate 556 half B through Q4 → R27 150 / C16 1µ / R26 2K / R25
  5.1K → R24 47K.
- **Boom** (`$7D02`): C21 → Q2 dumps C20 3.3µ (R8 100K, R7 10K). D2/D3
  gate the noise from 3J QC through Q1 → R5 750 / C19 1µ / R4 2K / R3
  5.1K → R2 47K.

### 3.4 NE556 8N

- Half A: R42 47K, R43 27K, C28 0.033µ. CV1 network C29 3.3µ / R44 1.2K /
  R46 12K, fed by the 8L two-inverter astable (R47/R48 43K, C30 10µ), and
  R45 10K from P24.
- Half B: R40 47K, R41 27K, C27 0.047µ. CV2 network C24 10µ / R49 1.2K /
  R51 10K, fed by the 8L three-inverter astable (R38 18K, C26 3.3µ), and
  R50 10K from the jump trigger.
- Both resets tie high through R126. `Astable::step` solves each threshold
  crossing exactly within a step.

### 3.5 Noise

LS164s 6K/5K/4K form a 24-bit shift register with feedback
`NOT(Q10 XOR Q23)`, clocked by 2VF inverted (one shift per 768 CPU
cycles). LS161 3J counts the XOR's rising edges; QC is the boom's noise.
Reset clears both.

### 3.6 Mixer and output

Four 47K resistors sum into Q3's base (about 780 Ω with C159 0.1µ, a
~2 kHz low-pass). C15 gives a ~12.6 Hz high-pass and the speaker's C8
220µ into 8 Ω a ~90 Hz one. The analog side steps every 32 Z80 cycles
(96 kHz) and `Machine` resamples to the page's audio rate.

## 4. Memory decode, latches and DMA

| Address | Read | Write |
|---|---|---|
| `$0000`-`$3FFF` | program ROM | — |
| `$6000`-`$6FFF` | work RAM | work RAM |
| `$7000`-`$73FF` | object RAM | object RAM |
| `$7400`-`$77FF` | VRAM (wait states, §4.2) | VRAM |
| `$7800`-`$780F` | — | 8257 (A3 = mode register) |
| `$7C00` | IN0 | 3D sound command |
| `$7C80` | IN1 | — |
| `$7D00` | IN2 | 6H: walk, jump, boom, P25, T1, T0 |
| `$7D80` | DIP | 5H latch |

LS259 5H at `$7D80`-`$7D87`: Q0 sound /INT, Q2 FLIP (picture flipped
while the bit is 0, which is the reset state), Q3 2PSL object RAM bank,
Q4 NMI enable, Q5 DMA request, Q6 CREF → PROM A6, Q7 CREF → PROM A7. Only
D0 reaches the latch.

### 4.1 8257-5

- One first/last flip-flop steps on every address and count write, low
  byte first; the mode write clears it.
- Ch0 reads work RAM into the 2N LS373 latch; ch1 writes the latch into
  object RAM. Both DRQs come from `$7D85`, so a rising edge copies
  `min(TC0, TC1) + 1` bytes.
- Each byte is two 4-clock DMA cycles. The CPU loses `n·8 + 2` cycles to
  BUSRQ/BUSAK, and video and sound time keep running during the stall.
- Mode bit 6 (TC stop) disables both channels after the copy.
- US ROM `$0141` (from the NMI): mode `$53`, ch0 `$6900` TC `$4180`, ch1
  `$7000` TC `$8180`, then pulses `$7D85`.

### 4.2 VRAM wait states

7F holds /WAIT on a CPU VRAM access until the video side lets go. VRAM is
free only at H 256…375 of every line, VBLANK included
(`Video::vram_wait`).

## 5. Video

### 5.1 Characters

- 32×32 tiles, `VRAM[(VF >> 3)·32 + (HF >> 3)]`. Rows 0-1 and 30-31 fall
  in VBLANK.
- Character ROM address `code·8 + (VF & 7)`, bit `7 - (HF & 7)`. Pixel =
  `3P << 1 | 3N`.
- Colour code from 2N at `((row >> 2) << 5) | col`, 4 bits.

### 5.2 Objects

- Each line scans 128 four-byte entries of the 2PSL bank.
- Byte 0 is native V. The adder chain hits when
  `(b0 + 0xF9 + VF) & 0xF0 == 0xF0`; the low nibble is the object row.
- Byte 1: code (7 bits), bit 7 flips the row order.
- Byte 2: colour code (low nibble), bit 7 reverses the dot order along H.
- Byte 3: native H, loaded into the line buffer counter at `b3 - 8`.
- 7C/7D give the low plane (16 dots, 7C first, D7 first) and 7E/7F the
  high plane.
- 93419 7M holds 16 hits per line; later ones are dropped. A later object
  overwrites an earlier one where its pixel is opaque (LS157 4J/3J).
- Each line is composed when the beam reaches H 256, after the CPU and DMA
  have had their go at object RAM for that line.

### 5.3 Palette

- Index `CREF << 6 | colour << 2 | pixel`; pixel 0 shows the character
  layer.
- 2F and 2E outputs are open-collector and active low: a 0 bit adds light.
  R and G use 220/470/1K ladders, B 220/470 (R69-R79). Level = sum of the
  lit conductances over the total.
- Stage palettes: barrels and elevators use bank 2, pies bank 1, rivets
  bank 3.

### 5.4 Orientation and flip

- Native frame is 256×224. The tube is rotated: native line n is upright
  column `223 - n` and native H runs down the screen (`Video::render`).
- FLIP (`$7D82` = 0) inverts VF and HF together.

## 6. DIP switches (TKG4-UP Operation Manual p.5)

CPU board SW1 A…H, read at `$7D80` through LS240 4N (ON = 1).

| Function | Switches | Values |
|---|---|---|
| Jumpmen | A,B | 3=0, 4=1, 5=2, 6=3 |
| Bonus | C,D | 7000=0, 10000=4, 15000=8, 20000=12 |
| Coinage | E,F,G | 1/1=0, 2/1=16, 1/2=32, 3/1=48, 1/3=64, 4/1=80, 1/4=96, 5/1=112 |
| Cabinet | H | OFF table, ON upright (128) |

The manual's only factory note is "keep H ON", so `Inputs::dip` defaults
to `0x80`: 3 men, 7000, 1 coin 1 play, upright.

## 7. High-score RAM

- Cold start LDIRs 9 bytes from `$01BA` to `$60B2`: 1P score, 2P score,
  then HI at `$60B8`-`$60BA` (factory `$50 $76 $00` at `$01C0`).
- `$025D` LDIRs the `$AA`-byte rank table (scores and initials) from
  `$3565` to `$6100`-`$61A9`.
- The page saves HI plus the rank table and pokes them back once both
  match the factory images in the loaded program ROM.

## 8. Inputs

All inputs pass through inverting LS240s, so a closed switch reads 1.

| Port | Bits |
|---|---|
| `$7C00` IN0 (2P) | 0 right, 1 left, 2 up, 3 down, 4 jump |
| `$7C80` IN1 | player 2, same layout |
| `$7D00` IN2 (3P) | 2 1P start, 3 2P start, 7 coin |

Coin: switch P3 → LS132 → retriggerable LS123 1N with R59 33K / C46 10µ,
`0.28·R·C·(1 + 0.7/R[kΩ])` ≈ 94 ms, onto IN2 bit 7, ORed with SERVICE. A
held switch gives one pulse. The page closes the switch for 50 ms per coin.

## 9. Sound CPU core

`shared/cpu/cpu_mcs48` implements the 8035 from the Intel MCS-48 manual:
external program fetch, 64 bytes of RAM, ports, timer/counter and
interrupts. See its tests in `shared/cpu/tests/mcs48_test.cpp`.

## 10. Hardware self-test ROM

`dkong/roms/hwtest/gen_hwtest.py` assembles a from-scratch program and
graphics (no Nintendo code or art) into the five self-test files
`program.bin`, `char.bin`, `obj.bin`, `prom.bin` and `sound.bin`.

- Writes `TST1` at `$6000` and copies a factory score table to `$60B2`
  from the same `$01BA` offset the US ROM uses.
- Its NMI acks `$7D84`, counts frames at `$6008`, programs the 8257 to
  copy `$40` bytes from `$6900` to `$7000`, pulses `$7D85`, then re-arms
  `$7D84`.
- Three screens, about 90 frames apart: a crosshatch (CREF bank 0), colour
  bars with four white 16×16 DMA'd objects (bank 1), then the help screen
  (bank 2) saying Nintendo's ROMs are still under copyright and to load
  your own set.
- Then loops echoing IN0 to `$6010`.
- The 8035 program parks P1 at `$80`.

## 11. Tests

`make -C dkong check` runs `tests/machine_test.cpp` (decode, latches,
DMA timing, VRAM waits, NMI, coin one-shot, 8035 bus, sound paths, tile
and object rendering, palette, flip, self-test screens) and
`tests/smoke_test.cpp` (`Smoke.*`). The Playwright suite in `web/tests/`
covers every control on the page. `ctest -N` lists the native cases.
