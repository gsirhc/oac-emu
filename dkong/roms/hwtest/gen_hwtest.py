#!/usr/bin/env python3
"""Donkey Kong board self-test ROM set, from scratch (not Nintendo's game)."""
import argparse, json, os, struct, sys, zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "shared"))
from hwtest_font import font  # noqa: E402

PROG_SIZE = 0x4000
WAIT_FRAMES = 90

# Upright tile (col, row) sits at VRAM row 29-col, column row: native V runs
# right to left across the cabinet, native H top to bottom (dkong/video.cpp).
U_COLS, U_ROWS = 28, 32

FONT = font(" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.,!?'/:")

HELP_TITLE = "DONKEY KONG ARCADE"
HELP_LINES = [
    (4,  HELP_TITLE),
    (7,  "SELF TEST OK"),
    (10, "THIS PAGE DOES NOT"),
    (12, "INCLUDE NINTENDO'S"),
    (14, "DONKEY KONG ROMS"),
    (17, "THEY ARE STILL"),
    (19, "UNDER COPYRIGHT"),
    (22, "IF YOU OWN A"),
    (24, "DONKEY KONG SET"),
    (26, "LOAD IT YOURSELF"),
    (29, "IT STAYS IN THIS"),
    (31, "BROWSER ONLY"),
]

# 2N gives each pair of upright rows its own colour code; CREF picks a
# palette bank per screen.
BANK_HATCH, BANK_BARS, BANK_HELP = 0, 1, 2
WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE = (
    (7, 7, 3), (7, 0, 0), (0, 7, 0), (0, 0, 3), (7, 7, 0), (0, 7, 3), (7, 0, 3), (7, 4, 0))
BARS = [RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA, ORANGE, WHITE]
HELP_COLOURS = {4: RED, 7: GREEN, 17: YELLOW, 19: YELLOW, 26: CYAN}

OBJ_TABLE = 0x6900
OBJ_BYTES = 0x40
TEST_SPRITES = 4


class Asm:
    def __init__(self, size):
        self.mem = bytearray(size)
        self.pc = 0

    def org(self, addr):
        self.pc = addr

    def db(self, *bs):
        for b in bs:
            self.mem[self.pc] = b & 0xFF
            self.pc += 1

    def word(self, n):
        self.db(n & 0xFF, (n >> 8) & 0xFF)

    def jp(self, addr):
        self.db(0xC3)
        self.word(addr)

    def call(self, addr):
        self.db(0xCD)
        self.word(addr)

    def ld_hl(self, n):
        self.db(0x21)
        self.word(n)

    def ld_de(self, n):
        self.db(0x11)
        self.word(n)

    def ld_bc(self, n):
        self.db(0x01)
        self.word(n)

    def ld_sp(self, n):
        self.db(0x31)
        self.word(n)

    def ld_a(self, n):
        self.db(0x3E, n)

    def ld_nn_a(self, addr):
        self.db(0x32)
        self.word(addr)

    def ld_a_nn(self, addr):
        self.db(0x3A)
        self.word(addr)

    def poke(self, addr, v):
        self.ld_a(v)
        self.ld_nn_a(addr)


def row_code(urow):
    return (urow // 2) & 15


def put_upright(video, ucol, urow, code):
    if 0 <= ucol < U_COLS and 0 <= urow < U_ROWS:
        video[(29 - ucol) * 32 + urow] = code & 0xFF


def center_col(text):
    return max(0, (U_COLS - len(text)) // 2)


def screen_crosshatch():
    video = bytearray(0x400)
    for ur in range(U_ROWS):
        for uc in range(U_COLS):
            put_upright(video, uc, ur, 1)
    return video


def screen_color_bars():
    video = bytearray(0x400)
    for ur in range(U_ROWS):
        for uc in range(U_COLS):
            put_upright(video, uc, ur, 2)
    return video


def screen_help():
    video = bytearray(0x400)
    for urow, text in HELP_LINES:
        for i, ch in enumerate(text.upper()):
            put_upright(video, center_col(text) + i, urow, ord(ch))
    return video


def obj_table():
    # byte 0 native V (upright x + 23), 1 code, 2 colour code, 3 native H
    # (upright y + 8); see DKONG_REVIEW.md §5.
    t = bytearray(OBJ_BYTES)
    for i in range(TEST_SPRITES):
        ux = 24 + i * 48
        uy = 120
        t[i * 4 + 0] = ux + 23
        t[i * 4 + 1] = i & 1
        t[i * 4 + 2] = 15
        t[i * 4 + 3] = uy + 8
    return t


def assemble_main(s1, s2, s3):
    a = Asm(PROG_SIZE)
    a.org(0x0000)
    a.jp(0x0200)

    # NMI: drop $7D84 (clears 8F), bump the frame counter, run the 8257
    # copy of $6900 into object RAM, re-arm $7D84.
    a.org(0x0066)
    a.db(0xF5)             # PUSH AF
    a.db(0xAF)             # XOR A
    a.ld_nn_a(0x7D84)
    a.ld_a_nn(0x6008)
    a.db(0x3C)             # INC A
    a.ld_nn_a(0x6008)
    a.poke(0x7D85, 0)
    a.poke(0x7808, 0x03)   # ch0 + ch1 enabled
    a.poke(0x7800, OBJ_TABLE & 0xFF)
    a.poke(0x7800, OBJ_TABLE >> 8)
    a.poke(0x7801, (OBJ_BYTES - 1) & 0xFF)
    a.poke(0x7801, 0x40 | ((OBJ_BYTES - 1) >> 8))
    a.poke(0x7802, 0x00)
    a.poke(0x7802, 0x70)
    a.poke(0x7803, (OBJ_BYTES - 1) & 0xFF)
    a.poke(0x7803, 0x80 | ((OBJ_BYTES - 1) >> 8))
    a.poke(0x7D85, 1)
    a.poke(0x7D85, 0)
    a.poke(0x7D84, 1)
    a.db(0xF1)             # POP AF
    a.db(0xED, 0x45)       # RETN

    # copy_screen: HL -> $7400, 0x400 bytes.
    a.org(0x0100)
    copy_screen = a.pc
    a.ld_de(0x7400)
    a.ld_bc(0x0400)
    a.db(0xED, 0xB0)       # LDIR
    a.db(0xC9)             # RET

    wait90 = a.pc
    a.ld_a_nn(0x6008)
    a.db(0xC6, WAIT_FRAMES)
    a.db(0x47)             # LD B,A
    wait_loop = a.pc
    a.ld_a_nn(0x7C00)      # IN0
    a.ld_nn_a(0x6010)
    a.ld_a_nn(0x6008)
    a.db(0xB8)             # CP B
    a.db(0x20, (wait_loop - (a.pc + 2)) & 0xFF)
    a.db(0xC9)

    set_bank = a.pc        # A = bank: $7D86 <- bit 0, $7D87 <- bit 1
    a.ld_nn_a(0x7D86)
    a.db(0x0F)             # RRCA
    a.ld_nn_a(0x7D87)
    a.db(0xC9)

    # Factory score table at the same ROM offsets the US program uses
    # ($01BA LDIR to $60B2; HI-SCORE is the last 3 bytes at $01C0 / $60B8).
    a.org(0x01BA)
    a.db(0x00, 0x37, 0x00, 0xAA, 0xAA, 0xAA, 0x50, 0x76, 0x00)

    a.org(0x0200)
    a.db(0xF3)             # DI
    a.ld_sp(0x6FF0)
    for i, ch in enumerate("TST1"):
        a.poke(0x6000 + i, ord(ch))
    a.db(0xAF)             # XOR A
    a.ld_nn_a(0x6008)
    a.ld_nn_a(0x7D83)      # 2PSL: object bank 0
    a.ld_hl(0x01BA)
    a.ld_de(0x60B2)
    a.ld_bc(0x0009)
    a.db(0xED, 0xB0)       # LDIR factory scores (incl. HI at $60B8)
    a.poke(0x7D82, 1)      # FLIP off
    a.ld_a(BANK_HATCH)
    a.call(set_bank)
    a.poke(0x7D84, 1)      # NMI on

    a.ld_hl(0x1000)
    a.call(copy_screen)
    a.call(wait90)
    a.ld_a(BANK_BARS)
    a.call(set_bank)
    a.ld_hl(0x1400)
    a.call(copy_screen)
    a.ld_hl(0x1C00)        # test sprites into the DMA source
    a.ld_de(OBJ_TABLE)
    a.ld_bc(OBJ_BYTES)
    a.db(0xED, 0xB0)
    a.call(wait90)
    a.ld_hl(OBJ_TABLE)     # hide them again
    a.ld_bc((OBJ_BYTES << 8) | 0)
    clear = a.pc
    a.db(0x71)             # LD (HL),C
    a.db(0x23)             # INC HL
    a.db(0x10, (clear - (a.pc + 2)) & 0xFF)  # DJNZ
    a.ld_a(BANK_HELP)
    a.call(set_bank)
    a.ld_hl(0x1800)
    a.call(copy_screen)

    hang = a.pc
    a.ld_a_nn(0x7C00)
    a.ld_nn_a(0x6010)
    a.jp(hang)

    a.mem[0x1000:0x1400] = s1
    a.mem[0x1400:0x1800] = s2
    a.mem[0x1800:0x1C00] = s3
    a.mem[0x1C00:0x1C00 + OBJ_BYTES] = obj_table()
    return bytes(a.mem)


def char_rom():
    # 3P (high plane) then 3N (low plane). A ROM byte is one native line, so
    # upright (x, y) in a cell is line 7-x, bit 7-y.
    rom = bytearray(0x1000)

    def plot(plane, code, x, y):
        rom[plane * 0x800 + code * 8 + (7 - x)] |= 0x80 >> y

    for i in range(8):
        for edge in (0, 7):
            plot(1, 1, i, edge)
            plot(1, 1, edge, i)
    for y in range(8):
        for x in range(8):
            plot(0, 2, x, y)
            plot(1, 2, x, y)
    for ch, rows in FONT.items():
        code = ord(ch)
        for ly, bits in enumerate(rows):
            for lx in range(8):
                if bits & (0x80 >> lx):
                    plot(1, code, lx, ly)
    return rom


def obj_rom():
    # 7C/7D low plane, 7E/7F high plane; left byte holds the first eight
    # pixels down the tube. Code 0 is a hollow box, code 1 a solid one.
    rom = bytearray(0x2000)

    def plot(plane, code, row, k):
        chip = plane * 2 + (k >> 3)
        rom[chip * 0x800 + code * 16 + row] |= 0x80 >> (k & 7)

    for r in range(16):
        for k in range(16):
            if r in (0, 15) or k in (0, 15):
                plot(0, 0, r, k)
            plot(0, 1, r, k)
            plot(1, 1, r, k)
    return rom


def prom_rom():
    # 2F: R2 R1 R0 G2, 2E: G1 G0 B1 B0, all active low (TKG4-11-CPU); then 2N.
    f = bytearray([0x0F] * 256)
    e = bytearray([0x0F] * 256)

    def put(bank, code, pens, rgb):
        r, g, b = rgb
        for pen in pens:
            ix = (bank << 6) | (code << 2) | pen
            f[ix] = ~((r << 1) | (g >> 2)) & 0x0F
            e[ix] = ~(((g & 3) << 2) | b) & 0x0F

    for code in range(16):
        put(BANK_HATCH, code, (1, 2, 3), WHITE)
        put(BANK_BARS, code, (1, 2, 3), BARS[code * len(BARS) // 16])
        put(BANK_HELP, code, (1, 2, 3), WHITE)
    for urow, colour in HELP_COLOURS.items():
        put(BANK_HELP, row_code(urow), (1, 2, 3), colour)
    n = bytearray(row_code(i & 31) for i in range(256))
    return bytes(f + e + n)


def sound_rom():
    # 3H: MOV A,#$80 / OUTL P1,A / JMP $ parks the DAC at mid-scale. 3F empty.
    rom = bytearray(0x1000)
    rom[0:5] = bytes([0x23, 0x80, 0x39, 0x04, 0x03])
    return rom


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def write_c_header(path, blobs):
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w") as f:
        f.write("// Generated by gen_hwtest.py -- do not edit.\n")
        f.write("#pragma once\n#include <cstdint>\n#include <array>\n")
        f.write("namespace dkong { namespace hwtest {\n")
        for name, data in blobs.items():
            f.write(f"inline const std::array<uint8_t, {len(data)}> {name} = {{")
            f.write(",".join(str(b) for b in data))
            f.write("};\n")
        f.write("}} // namespace\n")


def write_png(path, w, h, rgb_rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(row) for row in rgb_rows)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def marquee_png(path):
    """Landing-page tile. Shared renderer -- see shared/marquee.py."""
    shared = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared"))
    if shared not in sys.path:
        sys.path.insert(0, shared)
    from marquee import render_marquee
    render_marquee(path, "dkong")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--header")
    ap.add_argument("--png")
    args = ap.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    s1 = screen_crosshatch()
    s2 = screen_color_bars()
    s3 = screen_help()
    blobs = {
        "program": assemble_main(s1, s2, s3),
        "chars": char_rom(),
        "objs": obj_rom(),
        "proms": prom_rom(),
        "sound": sound_rom(),
    }
    names = {
        "program": "program.bin",
        "chars": "char.bin",
        "objs": "obj.bin",
        "proms": "prom.bin",
        "sound": "sound.bin",
    }
    for k, fname in names.items():
        with open(os.path.join(args.dir, fname), "wb") as f:
            f.write(blobs[k])
    if args.header:
        write_c_header(args.header, blobs)
    if args.png:
        os.makedirs(os.path.dirname(os.path.abspath(args.png)) or ".", exist_ok=True)
        marquee_png(args.png)
    crcs = {names[k]: crc32(blobs[k]) for k in names}
    with open(os.path.join(args.dir, "crc.json"), "w") as f:
        json.dump({k: f"{v:08x}" for k, v in crcs.items()}, f)
    print("hwtest CRCs:")
    for k, v in crcs.items():
        print(f"  {k}: {v:08x}")


if __name__ == "__main__":
    main()
