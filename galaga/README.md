# Galaga

Midway's Galaga board from 1981. The 51XX and 54XX custom chips run from dumps you supply. Without those, that part of the board stays on a labelled fallback.

## Specs

- CPU: three Zilog Z80s at 3.072 MHz (main, sub, and sound)
- Video: Namco tilemap, 64 sprites, and the 05XX starfield, upright
- Sound: the same Namco wavetable generator as Pac-Man, at 96 kHz
- Customs: Fujitsu MB8843 (51XX) and MB8844 (54XX), when their dumps are loaded
- Controls: 2-way joystick, fire, and the coin door

Hardware notes and citations: [GALAGA_REVIEW.md](GALAGA_REVIEW.md).

The Z80 core: [Z80_REVIEW.md](../shared/cpu/Z80_REVIEW.md).

What's still off from the real boards: [Z80_ARCADE_PARITY.md](../shared/cpu/Z80_ARCADE_PARITY.md).
