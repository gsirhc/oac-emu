# Shared CPU cores

## Intel 8080

`cpu_i8080.{h,cpp}` is the Intel 8080 used by the Altair 8800 and Space
Invaders. All I/O goes through `Bus` callbacks. `Cpu::interrupt(opcode)`
takes the RST byte the board jams onto the bus and honours the EI delay.
The Altair's own review covers its flag and timing sources
([`ALTAIR_REVIEW.md`](../../altair8800/ALTAIR_REVIEW.md)).

```sh
make test-smoke-i8080   # I8080* GoogleTest (deploy CI i8080-test)
make i8080diag          # 8080PRE, TST8080, CPUTEST, 8080EXM through cpm/cpm_host
make coverage-i8080     # llvm-cov over the GoogleTest suite plus the diagnostics
```

## Z80

`cpu_z80.{h,cpp}` is the Zilog Z80 used by Pac-Man, Frogger, Scramble,
Galaxian, and Galaga (three CPUs).
It's the NMOS part, T-state exact: every instruction runs as its UM0080
machine cycles, so each memory and I/O strobe lands on the T-state the
real chip drives it, with the full undocumented set (MEMPTR, Q, SLL,
IXH/IXL, block-I/O flags). It is not the Altair's 8080. Flag polarity and
several opcodes differ, so the cores stay separate. Design notes and
sources are in [`Z80_REVIEW.md`](Z80_REVIEW.md).

Boards can drive the pins: `Cpu::set_int` (level) and `Cpu::set_nmi`
(edge), plus optional `Bus::wait` (/WAIT states), `Bus::tick` (T-states
as they pass) and `Bus::refresh` (/RFSH address).

```sh
make test-smoke   # Z80 GoogleTest only (deploy CI z80-test)
make zexdoc       # Frank Cringle's documented-opcode exerciser
make zexall       # Frank Cringle's full exerciser, undocumented flags included
make sst          # SingleStepTests/z80: 1,604,000 cases, bus timing per T-state
make check        # Z80 + 6502 + 8080 GoogleTest + zexdoc + zexall + sst + Dormann + 8080 diagnostics
```

## NMOS 6502

`cpu_mos6502.{h,cpp}` is the period MOS Technology 6502 used by Asteroids,
Centipede, and Missile Command (and future Atari boards). It is not the
W65C02S in `assembler6502/` — NMOS quirks (JMP ($xxFF) wrap, RMW abs,X = 7, decimal
N/Z/V from the binary path, D preserved on reset/IRQ) and the full
undocumented opcode set are intentional.

Unsourced: LXA ($AB) ORs A with $EE before the AND. "No More Secrets: NMOS
6510 Unintended Opcodes" v0.91 p.48 says the constant depends on chip and
temperature (common values $00, $FF) and names no $EE. No primary source
for $EE was found. No ROM here depends on it.

```sh
make test-smoke-mos6502   # Mos6502.* GoogleTest (deploy CI mos6502-test)
make dormann              # Klaus Dormann 6502_functional_test.bin
```

`zexdoc.com`, `zexall.com`, the SingleStepTests tarball, the Dormann
`.bin` and the 8080 diagnostic `.COM`s are fetched, pinned, and checksummed.
They are not committed.

## Intel MCS-48 / 8035

`cpu_mcs48.{h,cpp}` is the 8035 subset of the MCS-48 family (Donkey Kong's
sound CPU): no internal program ROM, so every fetch goes through
`Bus::read_program` (EA forced high on the real part). Internal data RAM is
64 bytes; R0-R7 live at 0-7 or 24-31 depending on `SEL RB0`/`RB1`, and the
8-level CALL/RETR stack lives in RAM 8-23. There is no Z flag register;
`JZ`/`JNZ` test the accumulator directly, and only CY/AC are real flags.
Sources: Grokking the MCS-48 System's instruction listing
(`dkong/docs/mcs48-isa.txt`, which restates the Intel MCS-48 Users Manual)
and the 8048/8035 datasheet.

```sh
make test-smoke-mcs48   # Mcs48.* GoogleTest (deploy CI mcs48-test)
```

## Motorola 6809

`cpu_m6809.{h,cpp}` is the MC6809E used by Joust. It runs whole
instructions and counts E cycles from the cycle columns in the MC6809
Programming Manual (M6809PM/AD), Appendix F, with the indexed-mode extras
from its Table F-2. Interrupt entry and the CWAI wait follow the MC6809E
data sheet's timing figures (8, 9 and 17). IRQ, FIRQ and NMI follow the
manual's stacking rules,
NMI is edge-triggered and armed only after the first load of S, and CWAI
and SYNC park the core until an interrupt. Undefined opcodes run as
2-cycle no-ops and are counted in `illegal_ops`. The board-level notes
and the values the manual leaves undefined are in
[`JOUST_REVIEW.md`](../../joust/JOUST_REVIEW.md).

```sh
make test-smoke-m6809   # M6809.* GoogleTest (deploy CI m6809-test)
```

## Motorola 6800 / 6808

`cpu_m6800.{h,cpp}` is the 6800 instruction set, which the MC6808 on the
Williams sound board runs unchanged (the 6808 is a 6800 without internal
RAM). Cycle counts and flags come from the M6800 Programming Reference
Manual, M68PRM(D). IRQ is level-sensitive, NMI edge-triggered, and WAI
stacks the registers up front so the wake costs only the vector fetch.

```sh
make test-smoke-m6800   # M6800.* GoogleTest (deploy CI m6800-test)
```
