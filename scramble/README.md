# Scramble

Konami's Scramble board from 1981.

## Specs

- CPU: two Zilog Z80s. The game CPU is 3.072 MHz. The sound CPU is 1.789772 MHz.
- Video: Galaxian-family tilemap and sprites, upright, 60.606 Hz
- Sound: two GI AY-3-8910s
- I/O: two Intel 8255s, and PAL 6J
- Controls: 2-way joystick, fire, and the coin door

Hardware notes and citations: [SCRAMBLE_REVIEW.md](SCRAMBLE_REVIEW.md). The shared video and Konami sound write-up is in that file.

The Z80 core: [Z80_REVIEW.md](../shared/cpu/Z80_REVIEW.md).

What's still off from the real boards: [Z80_ARCADE_PARITY.md](../shared/cpu/Z80_ARCADE_PARITY.md).
