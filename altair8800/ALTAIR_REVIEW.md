# Altair 8800 Fidelity Review

This file is the citation trail for the Altair 8800 emulator in `altair8800/`:
the C++ 8080 core, the peripheral boards, the WebAssembly wrapper, the browser
front end, and both test suites. Each section says what the emulator does, the
source it follows, and what it does not model. §9 lists the open items.

---

## 0. How this was verified

What is executed against the emulator:

| Check | Result |
|---|---|
| `make -C altair8800 test` | GoogleTest suites `arithmetic_test`, `cassette_test`, `disk88_test`, `interrupt_test` pass |
| `make -C altair8800 cpm` | 8080PRE, TST8080, CPUTEST and 8080EXM pass |
| 8080EXM CRC check | All 25 groups pass with matching CRCs |
| `kCycles[256]` vs. Intel 8080 datasheet | Checked entry by entry, no errors |
| `make -C altair8800 check` | `demo`, `test` and `cpm` |
| `make -C altair8800/web test` | Playwright suites drive the real front end |

The 8080EXM pass is the strongest evidence for the CPU: it exercises every
documented flag combination, including auxiliary carry and the PSW's fixed
bits 1/3/5, and it matches. The ALU and flag model is measured, not inferred.
`cpm` is a no-op when the `cpm/*.COM` diagnostics have not been fetched locally.

---

## 1. Summary

**What the emulator gets right:**

- The CPU core passes the exhaustive 8080 instruction exerciser with correct CRCs.
- The cycle table is correct in all 256 entries.
- Interrupts are wired: the 2SIO's IRQ line jams RST 7, and `EI` takes effect
  after the following instruction (§2.2, §2.3).
- The 88-DCDD models rotation, head load, step and settle timing, with a LOAD
  SPEED selector (§3.2).
- The 88-ACR cassette is linear tape with a real head position,
  record-at-head with overwrite, inter-program gaps, transport auto-stop,
  credit-based throttling, and status polarity derived from and cited against
  the actual 8K BASIC ROM. It keeps running while the CPU is stopped and
  ignores panel RESET (§3.4).
- Panel RESET clears the CPU and the bus-side board latches only. Tape position
  and disk heads stay where they are (§3.2, §3.4).
- The terminal profiles filter output the way the real terminals do (§4).
- CI boots a real, pinned MITS CP/M 2.2 diskette on the emulated 88-DCDD and
  runs `DIR`/`STAT`, and blocks the deploy on failure.
- Provenance is cited in file headers (the ROM-derived cassette polarity)
  exactly as CLAUDE.md requires.
- Architecture is clean: a host-agnostic CPU behind a `Bus` struct, peripherals
  as independent `owns()/in()/out()` units, an embind wrapper that composes them.
  The same `Disk88` runs under the native `cpm_disk` harness and in the browser.

**What is still approximated or missing, in priority order:**

1. The `M1` and `STACK` status lamps are approximated, not taken from the CPU
   core (§3.6).
2. There is no 88-SIO board at ports 0x00/0x01. Every preset uses the 88-2SIO,
   and the `baremetal` and `stock` presets are dated 1976 to match (§5.2).
3. Interrupts are unit-tested but no bundled software uses them (§2.3).
4. Test gaps: no `serial2sio` unit test and no T-state assertions (§6).
5. Smaller items: `Cpu::reset()` clears every register, the 6850 master reset
   leaves the RX FIFO alone, the 88-DCDD sector interrupt is not wired (§2.4,
   §3.1, §3.2).

---

## 2. Intel 8080 execution fidelity

### 2.1 Verdict: strong, empirically proven

`8080EXM.COM` passes with matching CRCs across all 25 groups:

```
dad <b,d,h,sp>................  PASS! crc is:14474ba6
aluop nn......................  PASS! crc is:9e922f9e
aluop <b,c,d,e,h,l,m,a>.......  PASS! crc is:cf762c86
<daa,cma,stc,cmc>.............  PASS! crc is:bb3f030c
... (all 25 groups PASS)
```

Source inspection agrees with the empirical result:

- **`sub`/`cmp` auxiliary carry.** `shared/cpu/cpu_i8080.cpp` uses
  `~(a ^ v ^ r) & 0x10`, i.e. the *inverted borrow*. This is 8080 semantics (the
  half-carry out of the internal `A + ~v + 1`), not Z80 semantics. Correct, and
  correct with borrow-in too.
- **`ana` AC quirk.** `cpu_i8080.cpp` sets AC from `(a | v) & 0x08`, the 8080
  behaviour, not the 8085's always-1. Correct.
- **`dcr` AC.** `(v & 0xF) != 0`, matching `v + 0xFF`'s half-carry. Correct.
- **`daa`.** `cpu_i8080.cpp` uses `a > 0x99` for the high-nibble correction.
  Worked through the boundary cases (`0x9A`, `0xFA`, AC-set-with-low-nibble ≤ 9,
  `0x99` with AC): exactly equivalent to the Intel manual's "if the most
  significant 4 bits are **now** greater than 9" wording, including carry-out
  from step 1. Carry is never cleared by DAA. Correct.
- **PSW masking.** `PUSH PSW` writes `(f & 0xD7) | FLAG_N1`; `POP PSW` reads it
  back the same way. Bits 3/5 read 0, bit 1 reads 1. Correct, and validated by
  8080EXM (which uses PUSH/POP PSW to accumulate its CRCs).
- **Undocumented opcodes** alias correctly with correct cycle counts:
  `0x08/10/18/20/28/30/38` NOP (4), `0xCB` JMP (10), `0xD9` RET (10),
  `0xDD/ED/FD` CALL (17).
- **Rotates** touch only carry. `CMA` touches nothing. `STC`/`CMC` touch only
  carry. `INR`/`DCR` preserve carry. `DAD` touches only carry. All correct.

**Cycle table (`cpu_i8080.cpp`, `kCycles`), verified entry by entry against the
Intel 8080 datasheet.** Spot points worth recording: HLT = 7, XTHL = 18,
SHLD/LHLD = 16, STA/LDA = 13, MVI M / INR M / DCR M = 10, conditional RET 5
(not taken) / 11 (taken), conditional CALL 11 / 17, PCHL = 5, SPHL = 5,
XCHG = 5, EI/DI = 4, IN/OUT = 10. **No errors found.**

### 2.2 `EI` delay

`shared/cpu/cpu_i8080.cpp` (`Cpu::ei_delay_`)

A real 8080 does not recognise an interrupt until after the instruction
following `EI`. The `EI / RET` idiom in interrupt handlers depends on it.
`ei_delay_` blocks `interrupt()` for exactly one retiring instruction after
`EI`. It is counted down at the top of `step()`, and HALT's own idle cycles do
not decrement it. `tests/interrupt_test.cpp` covers the delay itself and
`EI` immediately followed by `HLT`.

### 2.3 Interrupt path

`shared/cpu/cpu_i8080.cpp` (`Cpu::interrupt`)
`altair8800/serial2sio.h` (`on_irq`)
`altair8800/serial2sio.cpp` (fires `on_irq`)

`Serial2SIO` decodes the 6850's receive- and transmit-interrupt enable bits and
fires `on_irq`. `Machine`'s constructor assigns `sio_.on_irq` to
`cpu_.interrupt(0xFF)`, RST 7, the conventional Altair 2SIO vector.

`Cpu::interrupt()` returns 0 and leaves `halted` set when interrupts are
disabled (a real 8080 in HALT with DI stays halted until RESET). When
interrupts are enabled it clears INTE, wakes a halted CPU, pushes the
post-`HLT` PC, and charges 11 T-states for an RST. The vector comes from
`opcode & 0x38`.

`tests/interrupt_test.cpp` has eight cases: ignored while DI, taken while EI,
HALT wakes, HALT stays halted while disabled, INTE cleared on accept, the
vector for all eight RST n, the `EI` delay, and `EI` immediately followed by
`HLT`.

**Gap.** Nothing in `disks/` or `roms/` uses RST-vectored interrupts, so the
wiring has not run against real period software end to end. The next step is a
small purpose-built test ROM that enables interrupts, arms the 2SIO's RX-ready
IRQ, and shows a keystroke vectoring to 0x38.

### 2.4 `Cpu::reset()` clears every register

`shared/cpu/cpu_i8080.cpp`

A real 8080 RESET forces only `PC = 0`. `A`, `F`, `BC`, `DE`, `HL`, `SP` are
indeterminate after reset, which is why Altair software always initialises `SP`
explicitly before using the stack. `Cpu::reset()` clears all of them, a cleaner
post-reset state than a real chip has, and a doc comment on the function says
so. There is no mode that leaves the registers alone.

### 2.5 T-state coverage

The `fidelity.spec.ts` test *"the 8080 runs at ~2 MHz of emulated time per
wall-clock second"* measures the `cycles` delta against wall time. That is
self-consistent: it counts what the emulator itself claims. No GoogleTest
asserts a per-opcode T-state count, so a typo in `kCycles[]` would change every
real-time behaviour in the machine (BASIC's timing loops, the cassette credit
throttle, paper-tape pacing) without turning a test red.

**Gap.** A GoogleTest that walks a table of `{opcode, expected T-states}` and
asserts `cpu.step()`'s return value, covering all 256 opcodes including the
taken and not-taken variants of every conditional CALL and RET.

---

## 3. Hardware, bus, and I/O traps

### 3.1 88-2SIO serial board (ports 0x10–0x13)

`altair8800/serial2sio.{h,cpp}`

The 6850 model:

- Status polarity: RDRF active-high on bit 0, TDRE on bit 1.
- DCD/CTS read clear. On a 6850 these status bits read 1 when the input pin is
  *high* (not asserted), so 0 = ready.
- Master reset is decoded on counter-divide field `CR1:CR0 == 0b11`.
- `tx_irq_enabled = (value & 0x60) == 0x20`. CR6:CR5 == 01 is RTS-low plus
  transmit-interrupt-enable.
- Reading the receive data register clears the overrun latch.
- Channel B is present at 0x12/0x13, the physical 2SIO layout.
- `owns()` masks `port & 0xFC`, giving exactly 0x10–0x13.

**Departure: 512-byte FIFOs** (`serial2sio.h`) in place of the 6850's single
byte, explained in the header. A program can transmit 512 characters before TDRE
drops, so the Teletype's 10 characters a second throttles the 8080 only after
512 bytes of slack are absorbed. For a `LIST` of a long program the throttle
engages. For anything shorter than 512 characters it never does.

**Gap.** Master reset does not flush the RX FIFO, so `RDRF` can survive a master
reset. A real 6850 master reset clears the receiver.

### 3.2 88-DCDD floppy controller (ports 0x08–0x0A)

`altair8800/disk88.{h,cpp}`

The controller follows MITS's and Pertec's own documentation. Sources:

- MITS, *Altair Floppy Disk Drive and Controller Hardware Documentation*
  (1977, deramp.com `Altair Floppy (88-DCDD) Manual.pdf`): "Altair Disk
  Controller I/O Information", printed pp.133–136 (ports, every status and
  control bit, timing); the one-shot timing table, p.128 and the following
  page; Disk Controller Board #1 sheets 1–2 and Board #2 sheets 1–2.
- Pertec, *FD400/FD5x0/FD5x1 Operating and Service Manual* 600500 (Mar 1977,
  bitsavers `pdf/pertec/floppy/600500_FD400-5x0-5x1_Mar77.pdf`): Table 1-1
  (360 rpm, 250 kbit/s, 10 ms adjacent-track seek, 40 ms head load, 77
  tracks) and §2.6.3 (the track 0 switch and its 40 ms hold-off).
- MITS, *Altair 8800 BASIC Reference Manual* 4.1 (Jul 1977, bitsavers),
  Appendix H: the 137-byte sector layouts and MITS's own read/write routines,
  which poll NRDA/ENWD and move two bytes per poll at 32 µs/byte.

What the model does, each point taken from those pages:

- **Ports.** `OUT 010` selects (D0–D3 drive, D7 clears the controller);
  `IN 010` status; `OUT 011` control (D0 step in, D1 step out, D2 head load,
  D3 head unload, D4/D5 interrupt enable/disable, D6 head current, D7 write
  enable); `IN 011` sector position; `OUT/IN 012` data (pp.133–136).
- **Polarity.** Status is true = 0 (p.134). D3 and D4 are "not used, = 0"
  (on Board #1 sheet 2 their 8T97 inputs are grounded). D5 is the bus INTE,
  inverted, which the machine feeds in with `setInte()`.
- **Enable.** A drive with no diskette can't be enabled (p.133 note a), and
  every port then floats to `0xFF`, "all false" (p.134). Taking the diskette
  out of the selected drive clears the controller ("disk control also
  cleared by opening door", p.133).
- **Sector position.** Valid only while head status is true (p.134 D2,
  p.136). D0 is SR0, sector true, low for 30 µs at the start of each sector;
  D1–D5 the sector number. D6/D7 aren't driven by Board #1 (only DI0–DI5 have
  8T97 drivers on the sector strobe) so they read 1.
- **Rotation.** 360 rpm, 32 hard sectors, 5.208 ms each. Read data starts
  140 µs after SR0 and arrives every 32 µs; NRDA sets per byte and an `IN 012`
  clears it, so a reader that falls behind loses bytes. After byte 137 the
  disk holds the zero tail the write circuit lays down to the end of the
  sector (p.135 step 6), so further reads return `0x00`.
- **Writes.** Write enable after SR0; ENWD first true 280 µs after SR0, then
  every 32 µs, cleared by `OUT 012`. A byte not refreshed in time is written
  again (p.136 note a). The write stops at the end of the sector (p.135 step 7),
  and move head stays false for the 475 µs trim-erase tail.
- **Head.** Head status is true 40 ms after a head-load command, and 40 ms
  after any step with the head loaded (p.134 D2; the settle one-shot on Board
  #2 sheet 2 is retriggered by every head-load command, so Disk BASIC pays the
  40 ms on each `SECGET`). Move head is false 40 ms after head load and, after
  a step, false 10 ms, true 1 ms, false 20 ms (p.134 D1; step inhibit 1, repeat
  step OK and step inhibit 2 one-shots).
- **Track 0** comes from the FD-400's track 0 switch, so it follows the head in
  both directions, and the drive holds it false until 40 ms after the last
  step (FD-400 §2.6.3.1). The same switch makes a step out at track 0 a no-op.
- **RESET** deselects the controller and clears its latches (`selected_`,
  `flags_`, `sector_`, `bufpos_`, `write_dirty_`). It leaves the heads where
  they are, because bus RESET sends no step pulses.

**LOAD SPEED.** Realistic (192 sectors/s) is the real drive above, paced on
the CPU cycle count at every port access. 5×/25×/50× scale rotation and the
seek/settle waits only between transfers: once a sector's data starts moving
(NRDA reported, a byte read, or write enable) the rest of that sector runs at
the real 32 µs/byte, and a fast-forward never skips past a sector start or the
first read byte. Max (`?test=1`, and every native harness that never calls
`clock()`) is timeless: each `IN 011` brings the next sector under the head,
bytes come as fast as they're read, and every wait is zero.

Boot and round-trip coverage: `cpm63k`, `games` and `cpm3boot` boot to `A>`,
and CP/M `SAVE`/`DIR`/`STAT`/`ERA` round-trip at Realistic, 5×, 50× and Max.
Disk BASIC `SAVE`/`LOAD`/`RUN`/`KILL` do the same, and Altair DOS boots. Images
written at Realistic and at Max are byte-identical.

GoogleTests in `disk88_test` pin the edge behaviour: `ReadsPastByte137ReturnTheZeroTail`,
`EmptyDriveCannotBeEnabled`, `DisabledControllerFloatsAllPorts`,
`ResetDoesNotHomeTheHead`, `Track0LineFollowsHead` and
`Track0ReportsOnlyAfterTheHeadSettles`. A Playwright test boots CP/M, which
seeks off track 0, hits RESET, and checks that the track is unchanged while the
controller deselects. A fidelity test shows Realistic measurably slows a CP/M
boot.

**Boot PROM (`disk_bootrom.h`).** No MITS-published listing of the DBL was
found (the BASIC 4.1 manual only says how to run it from 177400 octal). The
image is a dumped DBL 3.3 PROM, byte for byte (Mike Douglas, deramp.com
`software/roms/orginal_roms/DBL33.HEX`, disassembly in `DBL33.PRN`): a copy
stub at `FF00h` moves `0D4h` bytes from `FF27h` to `4C00h`, and the PROM's own
cassette bootstrap sits at `FF13h`–`FF26h`.

**Not modelled:** the sector interrupt (D4/D5 latch it, but the INT/VI7
jumper output isn't wired to the CPU; no MITS software found uses it), write
protect (an FD-400 option MITS media don't use), and per-drive spindle phase
(all drives share one rotation).

### 3.3 Paper tape, not a bus device

There is no 88-PTR board and no I/O trap for the reader. `tape.tick()`
(`web/app.js`, the `tape` object) pushes bytes into the 88-2SIO channel-A RX
FIFO via `machine.sendByte()`, the same path a keystroke takes.

On a real Altair the paper tape reader most people used was the one built into
the ASR-33 Teletype, sharing the console current loop with the keyboard. Bytes
from the reader and bytes from the keys arrive on the same serial port, so the
model matches that setup. It has no reader device because there isn't one on
the bus.

Two behaviours inside it:

**(a) The feed back-pressures on the FIFO.** In `web/app.js`:

```js
if (!this.flooding && machine.rxPending && machine.rxPending() > 400) break;
```

On the AUTO-LOAD path the load never overruns, however badly the loader keeps
up. A real reader runs at a fixed mechanical rate, and the loader either keeps
up or loses bytes. The raw `START` path does model overrun (the `flooding` flag
after about 0.75 s of a jammed FIFO). The bootstrap path does not. AUTO-LOAD is
a labelled shortcut, so the back-pressure stays inside that departure.

**(b) The bootstrap is synthesised, not MITS's.** `makeBootstrap()` in
`web/app.js` builds a loader of about 40 bytes. It polls `IN 0x10 / RRC / JNC`
on RDRF (bit 0 rotated into carry), skips leader nulls with `ORA A / JZ`, stores
with `MOV M,A / INX H`, counts down `DE`, and `JMP`s to the start address. It is
not the loader MITS published. It is labelled AUTO-LOAD, and the reader's
`START` button keeps the authentic hand-keyed path. The panel guide's
hand-keyed loader follows the synthesised one. §5.3 covers the published
bootstrap.

The reader's `START` button runs the reader motor and nothing else. It touches
no memory, runs no bootstrap, and does not RUN. It warns when no loader is
running, because the bytes fall on the floor. The tape starts with a 128-frame
blank leader.

### 3.4 88-ACR cassette (ports 0x06/0x07)

`altair8800/cassette.{h,cpp}`

Status polarity is derived from the actual 8K BASIC ROM, and the derivation is
cited in the file header (`IN 06 / ANI 01 / JNZ` → wait while bit 0 is set; `IN
06 / ANI 80 / JNZ` → wait while bit 7 is set; both ready conditions active-low).
The tape is linear: a physical head position, record-at-head with overwrite, a
12-byte inter-program gap so CLOAD can resync, FF/REW winding at 30× play rate
with auto-stop at both ends, and a credit-based throttle measured against CPU
cycles so 25×/50× are really that fast rather than frame-capped. 300 baud is
30 B/s, correct for KCS with 10-bit framing. `cassette_test` covers the device.

**The transport runs while the CPU is stopped.** The deck is a separate box with
its own mechanical transport, and the CPU's RUN/STOP paddle has no effect on it.
`Machine::tickCassette(dtMs, running)` is called every rendered frame
regardless of run state. While the CPU runs, it paces off `cpu_.cycles`, so
25×/50× and turbo stay correct. While it is stopped, it advances a separate
`idle_cycles_` counter by real elapsed time at the 2 MHz equivalent and adds it
to `cpu_.cycles` before the same `cassette_.tick()` call. The two paces splice
with no jump when RUN resumes. A Playwright test shows PLAY keeps winding with
the CPU on STOP.

**Panel RESET leaves the deck alone.** RESET is a bus signal to the S-100
cards, and the cassette deck is not on the bus. `Machine::reboot()` does not
call `cassette_.reset()`, so tape position and the transport keys survive, and
a tape that was playing keeps playing. A Playwright test shows RESET touches
neither the position nor the transport state.

**Cycle counter reset.** `Cpu::reset()`, called from `Machine::reboot()`, zeroes
`cpu_.cycles`, so from `cassette_.tick()`'s point of view the counter goes
backward. `CassetteACR::tick()` guards on `cpuCycles < prev_tick_cy_`: it
resyncs the baseline and treats that tick as zero elapsed time. The GoogleTest
`TickToleratesCpuCycleCounterGoingBackwardOnReset` pins it.

`Machine::reboot()` resets the CPU and the S-100 boards' electrical state only.
Deselecting the disk controller and clearing its latches is a bus signal
reaching the 88-DCDD card (§3.2).

### 3.5 Memory / bus model

`web/wasm_machine.cpp` (`bus.read`, `bus.write`)

Unpopulated reads return `0xFF`, the Altair convention (floating high). The ROM
window is checked before the RAM ceiling, so on the 64 KB CP/M preset the
88-DCDD boot PROM at `0xFF00` shadows RAM and `bus.write` drops writes into it,
as on real hardware. A test in `disk.spec.ts` drives an `STA` through the CPU
to prove it. `writeByte()` bypasses the bus, so it is not used for that check.

Sense switches on `IN 0FFh` return the A8–A15 paddle row (`bus.in`,
`port == 0xFF`).

### 3.6 Front panel

The paddle set and layout match the real machine: OFF/ON, STOP/RUN, SINGLE STEP,
EXAMINE / EXAMINE NEXT, DEPOSIT / DEPOSIT NEXT, RESET / CLR, PROTECT /
UNPROTECT, and two AUX. EXAMINE loads the switches into PC and DEPOSIT writes
at PC, as on the real Altair. SINGLE STEP works only while stopped. The status
lamp names and the WAIT/HLDA second row match the real silkscreen.

**(a) Status lamps.** The lamps come from which `Bus` callback fired last. That
tracking lives in `wasm_machine.cpp`, since `read`, `write`, `in` and `out` are
four separate callbacks and the CPU core needs no change.

| Lamp | Real meaning | Source here |
|---|---|---|
| `MEMR` | asserted during a memory-read machine cycle | last callback was `read` |
| `WO` | active-low, lit except during a write | follows the `write` callback, active-low as on the bus |
| `INP` | asserted during an `IN` machine cycle | last callback was `in` |
| `OUT` | asserted during an `OUT` cycle | last callback was `out` |
| `INT` | interrupt acknowledged | `cpu_.interrupt()` was served since the last poll |
| `HLDA` | hold acknowledged | permanently off, there is no DMA peripheral |
| `PROT` | memory protect active | permanently off, PROTECT is a documented no-op |
| `M1`, `STACK` | instruction fetch, stack cycle | approximated |
| `INTE`, `HLTA`, `WAIT` | | from CPU state |

`M1` (opcode fetch versus a plain operand read) and `STACK` (a push or pop
versus any other memory access) need the CPU core to say what an access is for.
The four `Bus` callbacks say what kind of access it is, not why. Reporting the
purpose means tagging every opcode handler in `cpu_i8080.cpp`, which is the part
of the codebase 8080EXM proves correct. Both lamps are sub-frame pulses, a
handful of T-states inside a 10–18 T-state instruction, and the panel is polled
once per rendered frame. Aggregating counts over many cycles works for address
lamps (below), but a boolean status bit has no equivalent. `M1` and `STACK`
stay approximated.

The data lamps show the byte at the last address in memory, not the byte that
was on the data bus, so an `IN` shows memory contents rather than the port
value.

**(b) Address lamps carry brightness.** On real hardware the lamps integrate,
so an address bit driven often glows brighter than one driven rarely. Kill the
Bit depends on it. `HL` is a delay counter incremented by 14 (`DAD B` with
`BC = 0x000E`) until it overflows, about 4681 iterations at 48 T-states each,
roughly 225k cycles or 6.7 frames, and within one frame `H` sweeps about
`0x00`→`0x24`. The four consecutive `LDAX D` instructions put `D` on the address
bus four times per loop iteration, so its bit is brighter than `H`'s sweep.
That brightness weighting is the effect.

`Machine::busActivityCounts()` returns a 16-element per-bit hit-count array,
and `app.js` maps each count to lamp opacity. `panel-lights.spec.ts` asserts
that two simultaneously lit high bits render at different brightness. It
samples every rendered frame in the page, because a Playwright poll round-trip
is too slow to catch the transition window of about 16 ms reliably. The
`baremetal` preset guide promises a lit bit sweeping across the top address
lamps (A8 – A15).

---

## 4. Serial terminal options

`web/app.js` (`TERM_PROFILES`)

### 4.1 Metering and chrome

The baud to characters-per-second conversion is (`web/app.js`):

```js
baudCps = p.baud ? p.baud / (key === "tty33" ? 11 : 10) : 0;
```

The ASR-33 frames a character as 1 start + 8 data + 2 stop = 11 bits, so 110
baud is exactly 10 cps. Everything else uses 10 bits.

Scrollback is disabled on the CRT profiles and enabled only on the Teletype's
paper roll. The `bare` housing is used for Modern and Teletype, with an amber
variant. The ASR-33 has a mechanical bell and the CRTs a phosphor flash.

### 4.2 Per-profile output filters

Each profile has a capability descriptor on its `TERM_PROFILES` entry, and a
per-profile output filter sits between `outQ` and `term.write()`.

| Profile | Behaviour |
|---|---|
| Teletype ASR-33 | a stateful filter strips every escape sequence, including one split across filter calls. CAPS LOCK is forced on |
| Glass TTY | the same stripping filter |
| DEC VT52 | translates the real DEC VT52 command set (`ESC A/B/C/D` cursor, `ESC H` home, `ESC J` and `ESC K` erases, `ESC Y row col` addressing) to xterm-compatible sequences |
| Lear Siegler ADM-3A | translates its bare control-code cursor moves and `ESC =` row/column addressing; the translator is unit-tested against the Lear Siegler ADM-3A protocol |
| DEC VT100 green/amber | no filter, the xterm parser is VT100-compatible |
| Modern | no filter |

The CP/M Games disk help text tells users to switch the Terminal dropdown to
DEC VT100 first, because those programs move the cursor around and expect a
real CRT. Under the Teletype profile their escape sequences are stripped.

### 4.3 Tests

`termfilters.spec.ts` covers the filters, including a sequence split across
filter calls. Every profile has a baud-metering assertion. `fidelity.spec.ts`
covers `tty33` at 10 cps and `modern` unmetered.

**Gap.** The baud choices are not all justified against a manual. VT100 at 9600
and ADM-3A at 9600 are plausible defaults, but VT52 at 4800 and Glass TTY at 300
have no cited source.

---

## 5. Presets

`web/app.js` (`PRESETS`)

### 5.1 Per-preset notes

**`baremetal`, "Bare-Metal Toggle", 1976, 4 KB, ASR-33.** The card list uses
the real MITS part number 88-4MCS (4K static) with the 88-2SIO, which existed
by 1976. The blurb agrees with the card list. Kill the Bit was a
front-panel toggle-in program. Delivering it on paper tape is an emulator
conceit: there was no MITS paper tape of it, and a 24-byte tape with no
checksum loader is not a period artifact. The guide text points at the manual
path, but the preset's default is the tape.

**`stock`, "Stock Launch", 1976, 4 KB, ASR-33.** The card list uses 88-4MCD (4K
dynamic) with the 88-2SIO. It loads the image the ROM manifest calls "Altair 4K
BASIC 4.0 (MITS 1976)". The ROM's own banner prints `4K BASIC 4.0 / COPYRIGHT
MITS 1976` (§5.3).

**`cassette`, "Cassette Hobbyist", 1976, 32 KB, ADM-3A.**

- 88-16MCD × 2 = 32 KB is a real MITS configuration with real part numbers.
- 88-ACR is real and correctly dated, and the ADM-3A is 1976.
- 8K BASIC on paper tape is right for 1976.
- `CLOAD "S"` single-letter naming matches Altair BASIC's syntax.
- "Realistic" is 300 baud for the cassette.
- About 14 minutes for 8K at 10 B/s is arithmetically right.

The deck is labelled "RADIO SHACK CASSETTE RECORDER" in the UI. The 88-ACR is
the S-100 interface board and the recorder was any consumer deck, so the label
is the right framing.

**`cpm`, "CP/M Workstation", 1979, 64 KB, VT100.** CP/M 2.2 is 1979, and the
VT100 shipped in August 1978. The 64 KB of RAM is 4× 88-16MCD boards, told the
same way in the preset backplane and in the disk cabinet hint (`dcdd-hint`).
The 64 KB setting puts RAM up to the top of memory, and the boot PROM at
`0xFF00` shadows it (§3.5).

`presets.spec.ts` pins the part numbers, eras and RAM story.

### 5.2 88-SIO and 88-2SIO

Every preset ships an 88-2SIO at ports 0x10/0x11, because that is the only
serial board the emulator implements and the only one the bundled BASIC images
target. A genuinely period 1975 machine used the 88-SIO at ports 0x00/0x01, and
MITS shipped BASIC builds targeting it.

The 2SIO is the departure, and it is stated. `baremetal` and `stock` are dated
1976, when the 2SIO existed, rather than their software's earlier MITS release
date. `app.js`, the web README and this section say so.

**Gap.** There is no 88-SIO board. It would be a new peripheral at 0x00/0x01,
simpler than the 2SIO, plus a pinned SIO-targeted 4K/8K BASIC image that may
not exist in a form that can be fetched with a checksum. It is not scheduled,
and it is tied to the bootstrap question in §5.3.

The DBL boot PROM's error path writes to port 0x01, the 88-SIO data port (the
`3E 43 / 01 / 3E 4F ... D3 01` tail of `disk_bootrom.h`). Nothing maps port
0x01, so a boot failure prints nothing. An 88-SIO would make CP/M boot errors
visible.

### 5.3 BASIC version numbers and the 3.2 bootstrap

**Version numbers.** `web/roms/manifest.json` describes the 4K image as "Altair
4K BASIC 4.0 (MITS 1976)", and the upstream file is
`nippur72/8-bit-projects/altair-basic/4kbas40.bin`. The number is not invented.
Booting `stock` with Auto-load prints `4K BASIC 4.0 / COPYRIGHT MITS 1976` and
`716 BYTES FREE`, straight from the ROM. The 8K image is described as "8K BASIC
4.0 (MITS 1976)", and its dating has not been checked against the ROM.

**The deramp.com 3.2 bootstrap.** Mike Douglas's deramp.com Altair archive is
already a pinned source for the 8K EPROM build (`8kBas_e0/e8/f0/f8.bin`, see
`web/roms/fetch-basic.sh`). A shorter, historically real bootstrap would beat the
synthesised loader in `makeBootstrap()` for the hand-keyed panel-guide path,
with fewer switch flips and an actual artifact in place of an invention.

The `LDR4K32-octal.PRN` loader (`altairclone.com/downloads/4kbootstrap.pdf`)
runs on ports 0x00/0x01, which is the 88-SIO, not this emulator's 2SIO.
Porting the same loader to the 2SIO ports would make it a rewrite instead of an
actual artifact. It becomes worth teaching in the panel guide only with an
88-SIO (§5.2).

**Gap.** The panel guide teaches the synthesised loader. Any real bootstrap
must be transcribed from its source with the URL cited, and pinned with a
SHA-256 if it ships as a file, or inlined as a cited `constexpr` table if it
ships as a listing.

### 5.4 The `716 BYTES FREE` figure

The figure appears in three places: `web/roms/manifest.json` (the 4K BASIC
description), `web/roms/README.md`, and the `stock` preset guide in
`web/app.js`. It matches what the bundled `4kbas.bin` prints on a cold start in
a 4 KB machine, so `MEMORY SIZE?` auto-detection lands on the real number.

### 5.5 Preset test coverage

`presets.spec.ts` asserts RAM size, terminal profile and visible devices for
all four presets, plus the S-100 card part numbers, the era strings and the
64 KB RAM story.

---

## 6. Test coverage

### 6.1 What is covered

- CI runs all four 8080 diagnostics (8080PRE, TST8080, CPUTEST, 8080EXM)
  through the CP/M host, pinned by SHA-256, and exits non-zero on failure.
- CI boots a real MITS CP/M 2.2 diskette (`dhansel/Altair8800` DISK01, pinned by
  SHA-256) on the emulated 88-DCDD and runs `DIR` and `STAT`.
- The Playwright suite in `web/tests/` drives the real UI in a browser.
- `fidelity.spec.ts` encodes CLAUDE.md's realism rules as executable checks:
  2 MHz pacing, ASR-33 metering, paper tape at Realistic vs Max, and turbo not
  surviving a keypress.
- A red web-test run blocks the deploy (`build` job `needs:` it).
- Coverage targets exist for both C++ (llvm-cov, including the diagnostic COMs)
  and JS (V8/monocart).
- `make check` runs `demo`, `test` and `cpm`, so a local check includes the
  diagnostics.
- The test seam in `web/app.js` parses `location.search` with `URLSearchParams`,
  so only `?test=1` enables it. A substring match on "test" would catch an
  unrelated parameter and force Max load speeds.
- Interrupts: `interrupt_test` (§2.3).
- Disk edges: `ReadsPastByte137ReturnTheZeroTail`, `ResetDoesNotHomeTheHead`,
  `EmptyDriveCannotBeEnabled` (§3.2).
- Terminal metering for every profile, and the escape filters (§4).
- Preset card lists and eras (§5.5).
- Kill the Bit lamp brightness (§3.6b).

### 6.2 Remaining gaps

| # | Gap | Impact |
|---|---|---|
| 1 | No unit test for `serial2sio.cpp`. The GoogleTest suites are `arithmetic_test`, `cassette_test`, `disk88_test` and `interrupt_test`. The 2SIO's status bits, master reset, overrun latch, channel B and IRQ decoding have no direct test. | The console board is the most load-bearing peripheral and is only tested implicitly. |
| 2 | No T-state assertions (§2.5). | A wrong cycle count changes every real-time behaviour and no test goes red. |
| 3 | No bundled software exercises RST-vectored interrupts (§2.3). | The wiring has not run against real period software end to end. |

---

## 7. Documentation notes

### 7.1 Track-0 line

The 88-DCDD track 0 status (status D6, MITS 88-DCDD manual p.134) is the
FD-400's TRACK 0 interface line, which Pertec's manual (§2.6.3) defines as "a
function of the position of the magnetic head assembly". It is a switch on the
carriage, so it follows the head in both directions. The only hold-off is the
drive's own, which keeps the line false until 40 ms after the last step
(§2.6.3.1). A latching line (set by a step out that reaches track 0, never
cleared by a step in) is not period behaviour, and it breaks a BIOS whose
head-home routine trusts the line.

`disk88.cpp` models the switch and the hold-off (§3.2), and
`Track0LineFollowsHead` and `Track0ReportsOnlyAfterTheHeadSettles` pin it.
`CLAUDE.md` uses the 8080 `ANA` auxiliary-carry quirk as its example of a real
hardware quirk to keep. A real quirk is what the chip or board does. A
convenience is a departure from it, and the three override rules apply.

### 7.2 Overrides and the RAM story

- `printManual()` (`web/app.js`) temporarily raises `baudCps` above the
  profile's real rate so the how-to page prints in about 8 s instead of about
  2 min on a Teletype. It affects only emulator UI text, never machine output,
  and a keypress skips it. `CLAUDE.md` carves it out in its list of sanctioned
  overrides.
- The `?test=1` clause in `CLAUDE.md` names the load speeds a test may force to
  Max. The 88-DCDD has a LOAD SPEED selector (§3.2), and Max is available under
  `?test=1`.
- The CP/M RAM story is the same in the preset backplane and the disk cabinet
  hint: 4× 88-16MCD boards (§5.1).

---

## 8. Architecture

- The CPU core is host-agnostic: it talks to the world only through four
  `std::function` callbacks. The same core drives the GoogleTest fixture, the
  CP/M diagnostic host, the native disk-boot harness, and the browser.
- Peripherals are independent units with a uniform `owns() / in() / out()`
  shape, composed by `Machine::make_bus()` in a readable dispatch chain.
- The C++ core builds and tests with no emscripten dependency, which is why
  `make check` can be a fast local gate.
- Media (`*.bin`, `*.dsk`) is fetched at build time with pinned SHA-256s rather
  than committed, and the CI cache keys on the workflow file hash.
- Provenance is cited in headers wherever behaviour comes from a source.

**Open items:**

1. **`Machine::reset()` and `Machine::reboot()`** are two different operations
   with similar names. `reset()` wipes all 64 KB of RAM and re-seeds the echo
   ROM. `reboot()` resets the boards and CPU without touching memory. The front
   panel's RESET paddle calls `reboot()`. Names like `powerOnReset()` and
   `busReset()` would separate them.
2. **`web/app.js`** is more than 2,700 lines in a single `boot()` closure
   holding the front panel, three device UIs, the preset system, the loader
   state machine, the terminal metering and the test seam. The device objects
   (`paperTape`, `tape`, `cassette`, `disk`) are factored inside it and would
   split into modules cleanly. Over 200 tests are pinned to its exact
   behaviour, and the split has no user-facing benefit, so it is not planned.

---

## 9. Open items

What the emulator does not yet do, with the section that describes it.

### Hardware and bus

1. **88-SIO board** at ports 0x00/0x01 (§5.2). It would need a pinned
   SIO-targeted BASIC image. It would also make DBL boot errors visible, since
   they go to unmapped port 0x01 today.
2. **The published 4K 3.2 bootstrap** (§5.3). It runs on the 88-SIO ports, so it
   waits on item 1.
3. **`M1` and `STACK` lamps** from real machine-cycle status (§3.6a). It needs
   the CPU core to tag each access with its purpose.
4. **Data lamps** showing the byte on the data bus rather than memory at the
   last address (§3.6a).
5. **Sector interrupt** on the 88-DCDD, write protect and per-drive spindle
   phase (§3.2).
6. **6850 master reset** does not flush the RX FIFO (§3.1).
7. **`Cpu::reset()`** clears every register, where a real 8080 forces only
   `PC = 0` (§2.4).

### Tests

8. A GoogleTest for `serial2sio.cpp` (§6.2).
9. T-state assertions for all 256 opcodes plus taken and not-taken
   conditionals (§2.5).
10. A purpose-built interrupt test ROM that proves a keystroke vectors to 0x38
    (§2.3).

### Documentation and cleanup

11. Cite a source for the VT52 (4800) and Glass TTY (300) default baud rates
    (§4.3).
12. Check the 8K BASIC "4.0 (MITS 1976)" dating against the ROM (§5.3).
13. Rename `Machine::reset()` and `Machine::reboot()` (§8).

---

## Appendix A: file map

| Path | Role |
|---|---|
| `shared/cpu/cpu_i8080.{h,cpp}` | 8080 core: registers, flags, cycle table, decoder, interrupts, `Bus` callbacks |
| `altair8800/serial2sio.{h,cpp}` | 88-2SIO, ports 0x10–0x13, ring-buffered |
| `altair8800/ringbuffer.h` | fixed-capacity SPSC byte ring |
| `altair8800/cassette.{h,cpp}` | 88-ACR, ports 0x06/0x07 |
| `altair8800/disk88.{h,cpp}` | 88-DCDD, ports 0x08–0x0A, from the MITS and Pertec manuals |
| `altair8800/disk_bootrom.h` | 256-byte MITS DBL boot PROM at 0xFF00 |
| `altair8800/main.cpp` | native echo harness |
| `altair8800/cpm/cpm_host.cpp` | CP/M BDOS shim for the diagnostic `.COM`s |
| `altair8800/cpm/cpm_disk.cpp` | native 88-DCDD CP/M boot test |
| `altair8800/tests/` | GoogleTest: `arithmetic_test`, `cassette_test`, `disk88_test`, `interrupt_test` |
| `altair8800/web/wasm_machine.cpp` | embind wrapper composing CPU and boards |
| `altair8800/web/app.js` | front end: panel, devices, presets, loaders, terminal |
| `altair8800/web/index.html` | markup + CSS for the panel, disk cabinet, deck |
| `altair8800/web/tests/` | Playwright specs |

## Appendix B: key code locations

| Topic | Location |
|---|---|
| Cycle table | `shared/cpu/cpu_i8080.cpp`, `kCycles` |
| `Cpu::reset()` clears all registers | `shared/cpu/cpu_i8080.cpp` |
| `sub` AC (inverted borrow), `ana` AC quirk, `daa` | `shared/cpu/cpu_i8080.cpp` |
| `Cpu::interrupt()`, `ei_delay_` | `shared/cpu/cpu_i8080.cpp` |
| `on_irq` wiring | `Machine` constructor in `web/wasm_machine.cpp` |
| 512-byte FIFOs | `serial2sio.h` |
| `Disk88::reset()` | `disk88.cpp` |
| Track-0 sensor | `disk88.cpp` |
| `CassetteACR::tick()` cycle-counter guard | `cassette.cpp` |
| `Machine::reboot()`, `Machine::tickCassette()` | `web/wasm_machine.cpp` |
| `Machine::busActivityCounts()` | `web/wasm_machine.cpp` |
| `bus.read`, `bus.write` ROM window | `web/wasm_machine.cpp` |
| `TERM_PROFILES`, ASR-33 11-bit framing | `web/app.js` |
| `printManual` baud override | `web/app.js` |
| Status lamps, address-lamp opacity | `web/app.js` |
| RESET paddle to `reboot()` | `web/app.js` |
| `PRESETS`, `makeBootstrap()`, paper-tape FIFO back-pressure | `web/app.js` |
| Test seam guard (`URLSearchParams`) | `web/app.js` |
