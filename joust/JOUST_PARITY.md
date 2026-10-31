# Joust -- parity backlog

The open gaps between this machine and a real Williams Joust board. When
an item lands, write the current behaviour and its source into
`JOUST_REVIEW.md` and delete it here.

Rough parity today: **~85%**. The white/green set runs its power-up
tests, factory restore, attract mode, coin-up and play, and the blitter
matches Sean Riddle's real-board dumps. What's left are values the scans
don't settle and timing that is modelled per instruction, not per bus
cycle.

| Area | Parity | Biggest gap |
|------|--------|-------------|
| CPU core (6809) | high | Undefined mixed-size transfers (C1) |
| Memory decode | high | — |
| Video | high | Mid-line palette and DRAM timing (V1) |
| Blitter | high | Halt granularity (B1) |
| Sound | medium-high | Watchdog reset reach (S1), amplifier (S2) |
| Inputs / coin door | high | Slam and centre coin unexposed (I1) |
| CMOS | high | Upper-nibble reads (M1) |

Every fix lands with its native test and, where it's visible from the
page, a Playwright test, in the same commit.

## CPU

- **C1. EXG/TFR between sizes.** Motorola leaves 8 ↔ 16-bit transfers
  and the unused register codes undefined; the core's choice
  (JOUST_REVIEW.md §15) is unsourced until a measurement turns up.
  Joust doesn't use them.

## Video

- **V1. Mid-line timing.** Each line is drawn from DRAM and the palette
  as they stand when the line ends. A palette or DRAM write in the middle
  of a visible line shows across that whole line, not from its pixel on.

## Blitter

- **B1. Halt granularity.** A blit runs to completion when `$CA00` is
  written and the CPU then stalls for its length. On the board the
  Special Chips own the bus byte by byte, so video and interrupts see the
  DRAM change progressively.

## Sound

- **S1. Watchdog reset.** The main board's watchdog doesn't reset the
  sound board. Whether /RESET reaches the D-8224 through the interface
  cable isn't settled from the drawing set.
- **S2. Amplifier.** The output is the DAC through the C14 coupling pole
  only; the amplifier's own response and volume pot aren't modelled.

## Inputs

- **I1. Slam and centre coin.** The board reads the slam switch (bit 6)
  and the centre coin (bit 5), but the page has no control for them.
- **I2. Cocktail.** The SCREEN CONTROL flip is decoded through the PROMs;
  a cocktail panel and table orientation aren't offered.

## CMOS

- **M1. Upper data lines.** CMOS reads return `$F0 | nibble`. What the
  floating D4-D7 actually read on a real board is unmeasured.
