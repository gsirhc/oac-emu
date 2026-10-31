#!/usr/bin/env python3
"""Joust board self-test ROM set, from scratch (not Williams' game)."""
import argparse, json, os, struct, sys, zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "shared"))
from hwtest_font import font  # noqa: E402

CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.,!?'/:"
FONT = font(CHARS)

HELP_TITLE = "JOUST ARCADE"
# (text row of 8 lines, text, palette index); 31 rows fit in the 248 visible lines.
HELP_LINES = [
    (2,  HELP_TITLE,           2),
    (5,  "SELF TEST OK",       3),
    (9,  "THIS PAGE DOES NOT", 1),
    (11, "INCLUDE WILLIAMS'",  1),
    (13, "JOUST ROMS",         1),
    (16, "THEY ARE STILL",     4),
    (18, "UNDER COPYRIGHT",    4),
    (21, "IF YOU OWN A",       1),
    (23, "JOUST SET",          1),
    (25, "LOAD IT YOURSELF",   5),
    (28, "IT STAYS IN THIS",   1),
    (30, "BROWSER ONLY",       1),
]

# BB GGG RRR (R-8731 resistor ladder).
PALETTE = [0x00, 0xFF, 0x07, 0x38, 0x3F, 0xC7, 0xC0, 0xF8,
           0xA4, 0x05, 0x28, 0x80, 0x2F, 0x1F, 0x52, 0x80]
SCREEN_COLS = 0x98
SCREEN_ROWS = 0xF8
# CPU column c shows at pixel 2 * (c - 1) (3G slot (c + 5) mod 192, counts 3..52).
FIRST_COL = 1
TEXT_COLS = 37

ROM_PIA = 0xC80C
WIDGET_PIA = 0xC804
ROM_CTRL = 0xC900
BLIT = 0xCA00
VCOUNT = 0xCB00
WATCHDOG = 0xCBFF
GLYPH_BASE = 0x0000
WAIT_FRAMES = 90

SND_PIA = 0x0400


class Asm:
    def __init__(self, base, size):
        self.base = base
        self.mem = bytearray(size)
        self.pc = base
        self.labels = {}
        self.fix = []

    def db(self, *bs):
        for b in bs:
            self.mem[self.pc - self.base] = b & 0xFF
            self.pc += 1

    def dw(self, n):
        self.db(n >> 8, n)

    def label(self, name):
        self.labels[name] = self.pc

    def rel8(self, op, name):
        self.db(op, 0)
        self.fix.append((self.pc - 1, name))

    def resolve(self):
        for at, name in self.fix:
            off = self.labels[name] - (at + 1)
            assert -128 <= off <= 127, name
            self.mem[at - self.base] = off & 0xFF


class M6809(Asm):
    def __init__(self, base, size):
        super().__init__(base, size)
        self.fix16 = []

    def lda(self, n): self.db(0x86, n)
    def ldb(self, n): self.db(0xC6, n)
    def ldd(self, n): self.db(0xCC); self.dw(n)
    def ldx(self, n): self.db(0x8E); self.dw(n)
    def ldy(self, n): self.db(0x10, 0x8E); self.dw(n)
    def lds(self, n): self.db(0x10, 0xCE); self.dw(n)
    def sta(self, a): self.db(0xB7); self.dw(a)
    def stb(self, a): self.db(0xF7); self.dw(a)
    def std(self, a): self.db(0xFD); self.dw(a)
    def stx(self, a): self.db(0xBF); self.dw(a)
    def lda_ext(self, a): self.db(0xB6); self.dw(a)
    def clr(self, a): self.db(0x7F); self.dw(a)
    def jsr(self, a): self.db(0xBD); self.dw(a)
    def jmp(self, a): self.db(0x7E); self.dw(a)

    def poke(self, a, v):
        self.lda(v)
        self.sta(a)

    def jsr_l(self, name):
        self.db(0xBD, 0, 0)
        self.fix16.append((self.pc - 2, name))

    def resolve(self):
        super().resolve()
        for at, name in self.fix16:
            v = self.labels[name]
            self.mem[at - self.base] = v >> 8
            self.mem[at - self.base + 1] = v & 0xFF


def glyph_rom():
    """Glyph k at k * 32: 8 rows of 4 bytes, colour 1 where the font has a dot."""
    rom = bytearray(0x9000)
    for k, ch in enumerate(CHARS):
        for r, bits in enumerate(FONT[ch]):
            for b in range(4):
                hi = 1 if bits & (0x80 >> (2 * b)) else 0
                lo = 1 if bits & (0x40 >> (2 * b)) else 0
                rom[GLYPH_BASE + k * 32 + r * 4 + b] = (hi << 4) | lo
    return rom


def blit(a, ctl, src, dst, w, h, solid=None):
    # SC1 wants bit 2 of width and height inverted.
    if solid is not None:
        a.lda(solid)
        a.sta(BLIT + 1)
    a.ldd(src)
    a.std(BLIT + 2)
    a.ldd(dst)
    a.std(BLIT + 4)
    a.ldd(((w ^ 4) << 8) | (h ^ 4))
    a.std(BLIT + 6)
    a.lda(ctl)
    a.sta(BLIT)


def fill(a, col, row, cols, rows, colour):
    # SOLID to a screen-format destination: across columns, then down rows.
    blit(a, 0x12, 0, (col << 8) | row, cols, rows, colour * 0x11)


def text_line(a, row, text, colour):
    start = FIRST_COL + 3 + 4 * ((TEXT_COLS - len(text)) // 2)
    for i, ch in enumerate(text.upper()):
        if ch == " ":
            continue
        k = CHARS.index(ch)
        # FORE_ONLY | SOLID | screen destination: font dots take the colour.
        blit(a, 0x1A, GLYPH_BASE + k * 32, ((start + 4 * i) << 8) | (row * 8), 4, 8, colour * 0x11)


def main_rom():
    a = M6809(0xD000, 0x3000)
    a.label("reset")
    a.db(0x1A, 0xFF)              # ORCC #$FF
    a.lds(0xBF00)
    a.poke(WATCHDOG, 0x39)
    # ROM PIA: port A coin door in, port B sound select out, idle $3F.
    a.clr(ROM_PIA + 1)
    a.clr(ROM_PIA + 0)
    a.poke(ROM_PIA + 1, 0x3C)
    a.clr(ROM_PIA + 3)
    a.poke(ROM_PIA + 2, 0xFF)
    a.poke(ROM_PIA + 3, 0x3C)
    a.poke(ROM_PIA + 2, 0x3F)
    a.clr(WIDGET_PIA + 1)
    a.clr(WIDGET_PIA + 3)
    a.poke(WIDGET_PIA + 1, 0x34)
    a.poke(WIDGET_PIA + 3, 0x3C)
    a.poke(ROM_CTRL, 0x01)
    a.ldx(0xC000)
    for v in PALETTE:
        a.lda(v)
        a.db(0xA7, 0x80)          # STA ,X+
    a.jsr_l("clear")

    # Pattern 1: sixteen colour bars.
    bar = SCREEN_COLS // 16
    for c in range(16):
        fill(a, c * bar, 0, bar, SCREEN_ROWS, c)
    a.jsr_l("wait")

    # Pattern 2: crosshatch, one line every 16 pixels each way.
    a.jsr_l("clear")
    for col in range(FIRST_COL, SCREEN_COLS, 8):
        fill(a, col, 0, 1, SCREEN_ROWS, 1)
    for row in range(0, SCREEN_ROWS, 16):
        fill(a, 0, row, SCREEN_COLS, 1, 1)
    a.jsr_l("wait")

    # Help screen, then a beep from the sound board.
    a.jsr_l("clear")
    for row, text, colour in HELP_LINES:
        text_line(a, row, text, colour)
    a.poke(ROM_PIA + 2, 0x3E)
    a.jsr_l("frame")
    a.poke(ROM_PIA + 2, 0x3F)
    a.label("idle")
    a.jsr_l("frame")
    a.rel8(0x20, "idle")          # BRA

    a.label("clear")
    fill(a, 0, 0, SCREEN_COLS, SCREEN_ROWS, 0)
    a.db(0x39)                    # RTS

    # One frame: wait for line $F0 to come round, feeding the watchdog.
    a.label("frame")
    a.label("f_out")
    a.poke(WATCHDOG, 0x39)
    a.lda_ext(VCOUNT)
    a.db(0x81, 0xF0)              # CMPA #$F0
    a.rel8(0x27, "f_out")         # BEQ
    a.label("f_in")
    a.lda_ext(VCOUNT)
    a.db(0x81, 0xF0)
    a.rel8(0x26, "f_in")          # BNE
    a.db(0x39)

    a.label("wait")
    a.ldb(WAIT_FRAMES)
    a.label("w_loop")
    a.db(0x34, 0x04)              # PSHS B
    a.jsr_l("frame")
    a.db(0x35, 0x04)              # PULS B
    a.db(0x5A)                    # DECB
    a.rel8(0x26, "w_loop")
    a.db(0x39)

    a.label("rti")
    a.db(0x3B)
    a.resolve()
    rom = a.mem
    vec = 0xFFF0 - 0xD000
    for i, name in enumerate(["rti", "rti", "rti", "rti", "rti", "rti", "rti", "reset"]):
        v = a.labels[name]
        rom[vec + 2 * i] = v >> 8
        rom[vec + 2 * i + 1] = v & 0xFF
    return bytes(rom)


def sound_rom():
    """MC6808 program: a CB1 command plays a short square wave on the DAC."""
    a = Asm(0xF000, 0x1000)
    a.label("reset")
    a.db(0x0F)                    # SEI
    a.db(0x8E); a.dw(0x007F)      # LDS #$007F
    a.db(0xCE); a.dw(SND_PIA)     # LDX #$0400
    a.db(0x6F, 0x01)              # CLR 1,X
    a.db(0x86, 0xFF, 0xA7, 0x00)  # LDAA #$FF / STAA 0,X
    a.db(0x86, 0x04, 0xA7, 0x01)  # LDAA #$04 / STAA 1,X
    a.db(0x6F, 0x03)              # CLR 3,X
    a.db(0x6F, 0x02)              # CLR 2,X
    a.db(0x86, 0x07, 0xA7, 0x03)  # LDAA #$07 / STAA 3,X: CB1 rising edge, IRQ on
    a.db(0x86, 0x80, 0xA7, 0x00)  # DAC mid-scale
    a.db(0x0E)                    # CLI
    a.label("idle")
    a.db(0x3E)                    # WAI
    a.rel8(0x20, "idle")
    a.label("irq")
    a.db(0xB6); a.dw(SND_PIA + 2)  # LDAA $0402 clears the flag
    a.db(0xC6, 0xC0)              # LDAB #192 half-cycles
    a.label("tone")
    a.db(0x63, 0x00)              # COM 0,X flips the DAC
    a.db(0x86, 0x40)              # LDAA #64
    a.label("delay")
    a.db(0x4A)                    # DECA
    a.rel8(0x26, "delay")
    a.db(0x5A)                    # DECB
    a.rel8(0x26, "tone")
    a.db(0x86, 0x80, 0xA7, 0x00)
    a.db(0x3B)                    # RTI
    a.label("nmi")
    a.db(0x3B)
    a.resolve()
    rom = a.mem
    for i, name in enumerate(["irq", "nmi", "nmi", "reset"]):
        v = a.labels[name]
        rom[0xFF8 + 2 * i] = v >> 8
        rom[0xFF8 + 2 * i + 1] = v & 0xFF
    return bytes(rom)


def decoder_proms():
    """3G: CPU column to (bank, page); 3C: video line to DRAM row. Upper halves flip."""
    d4 = bytearray(0x200)
    for h in range(0x100):
        if h >= 0xC0:
            d4[h] = d4[0x100 + h] = 0xC0
            continue
        s = (h + 5) % 192
        d4[h] = ((s % 3) << 6) | (s // 3)
        m = 161 - s if h < SCREEN_COLS else s
        d4[0x100 + h] = ((m % 3) << 6) | (m // 3)
    d6 = bytearray(0x200)
    for line in range(0x100):
        if 0x01 <= line <= SCREEN_ROWS:
            d6[line] = line - 1
            d6[0x100 + line] = SCREEN_ROWS - line
    return bytes(d4), bytes(d6)


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def write_c_header(path, blobs):
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w") as f:
        f.write("// Generated by gen_hwtest.py -- do not edit.\n")
        f.write("#pragma once\n#include <cstdint>\n#include <array>\n")
        f.write("namespace joust { namespace hwtest {\n")
        for name, data in blobs.items():
            f.write(f"inline const std::array<uint8_t, {len(data)}> {name} = {{")
            f.write(",".join(str(b) for b in data))
            f.write("};\n")
        f.write("}} // namespace\n")


def marquee_png(path):
    shared = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared"))
    if shared not in sys.path:
        sys.path.insert(0, shared)
    from marquee import render_marquee
    render_marquee(path, "joust")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--header")
    ap.add_argument("--png")
    args = ap.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    d4, d6 = decoder_proms()
    blobs = {
        "bank": bytes(glyph_rom()),
        "fixed": main_rom(),
        "decoder4": d4,
        "decoder6": d6,
        "sound": sound_rom(),
    }
    names = {
        "bank": "bank.bin",
        "fixed": "fixed.bin",
        "decoder4": "decoder4.bin",
        "decoder6": "decoder6.bin",
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
