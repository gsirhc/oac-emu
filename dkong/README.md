# Donkey Kong

Nintendo's Donkey Kong (1981), TKG4 CPU board.

## Specs

- CPU: Zilog Z80A at 3.072 MHz (61.44 MHz video crystal / 20)
- Sound: Fujitsu MB8884 (8035) at 6 MHz with program and data ROMs,
  Port 1 into a DAC-08; discrete walk, jump and boom circuits on an NE556
  and an LFSR noise source
- Video: 256x224 native, upright (224x256), 60.6 Hz; 8x8 characters and
  16x16 objects (16 per line), MB7052 colour PROMs with 4 CREF banks
- Memory: 16 KiB program ROM, 4 KiB work RAM, 1 KiB object RAM at `$7000`
  (8257 DMA from `$6900`), 1 KiB VRAM at `$7400` with wait states
- Reset: power-on only, no watchdog
- Storage: none; ROMs load from the browser's ROM socket panel
- Controls: 4-way joystick, jump button, 1P/2P start, coin door
- Firmware: ships a from-scratch hardware self-test ROM, not Nintendo's
  Donkey Kong. Load a real `dkong` set yourself; it stays in the browser.

Hardware notes and citations: [DKONG_REVIEW.md](DKONG_REVIEW.md).

What's still open: [DKONG_PARITY.md](DKONG_PARITY.md).

The Z80 core: [Z80_REVIEW.md](../shared/cpu/Z80_REVIEW.md).
