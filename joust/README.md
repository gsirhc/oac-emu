# Joust

Williams Electronics' Joust (1982), white/green label ROM board on the
Williams CPU board, with the D-8224 sound board.

## Specs

- CPU: Motorola MC6809E at 1 MHz (12 MHz crystal / 12, E and Q from 7K/7F)
- Sound: Motorola MC6808 at 894.886 kHz (3.579545 MHz / 4) with a 4 KiB
  program ROM, 128 bytes of 6810 RAM and a 6821 PIA into an MC1408 DAC
- Video: 304x256 bitmap in DRAM, 300x248 visible, horizontal, 60 Hz
  (260 lines of 64 µs); 16 colours from a 74189 palette, 4 bits per pixel
- Blitter: two VL2001 Special Chips (SC1) at `$CA00`, 1 byte/µs
- Memory: 48 KiB DRAM (three banks of 4116s, interleaved by the
  `decoder.4` PROM), 36 KiB banked ROM under the DRAM, 12 KiB fixed ROM at
  `$D000`
- CMOS: 5114 1Kx4 at `$CC00`, battery backed: high scores, bookkeeping
  and game adjustments. `$CC00-$CCFF` is write-protected with the coin
  door shut
- Reset: power-on, and a frame-counting watchdog fed with `$39` at `$CBFF`
- Storage: none; ROMs load from the browser's ROM socket panel
- Controls: per player a 2-way joystick and a flap button, 1P/2P start,
  three coin switches; inside the coin door AUTO-UP, ADVANCE and HIGH
  SCORE RESET. No DIP switches
- Firmware: ships a from-scratch hardware self-test ROM, not Williams'
  Joust. Load a real `joust` set yourself; it stays in the browser.

Hardware notes and citations: [JOUST_REVIEW.md](JOUST_REVIEW.md).

What's still open: [JOUST_PARITY.md](JOUST_PARITY.md).

The 6809 and 6800 cores: [shared/cpu/README.md](../shared/cpu/README.md).
