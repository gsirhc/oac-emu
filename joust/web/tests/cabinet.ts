// Per-cabinet values for the shared specs in tests/shared/ (shared/arcade-tests/).
export const ROM_DB = "retroweb-joust";
export const HWTEST_FILES = [
  "roms/bank.bin",
  "roms/fixed.bin",
  "roms/decoder4.bin",
  "roms/decoder6.bin",
  "roms/sound.bin",
];
// Unused tail of bank ROM 9: the glyphs end well before it and nothing reads it.
export const USER_BYTE = 0x8F00;
