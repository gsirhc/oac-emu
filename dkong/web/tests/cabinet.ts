// Per-cabinet values for the shared specs in tests/shared/ (shared/arcade-tests/).
export const ROM_DB = "retroweb-dkong";
export const HWTEST_FILES = [
  "roms/program.bin",
  "roms/char.bin",
  "roms/obj.bin",
  "roms/prom.bin",
  "roms/sound.bin",
];
// A byte inside the unused program padding (the main routine ends well
// before $0300; VRAM screen data starts at $1000): flipping it changes the
// CRC without touching the CPU's JP/flag-dependent first opcode at byte 0.
export const USER_BYTE = 0x300;
