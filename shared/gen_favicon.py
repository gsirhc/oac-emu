"""Site favicon / title-bar icon: beige IBM PC (monitor + drives + case front).

Small sizes show C:\\ on the CRT; 48px and up show OAC. Needs Pillow.
  python3 shared/gen_favicon.py
  python3 shared/gen_favicon.py --out assets
"""
from __future__ import annotations

import argparse
import io
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

# A step lighter than page gray (#c0c0c0) so the chassis reads on the landing page.
BEZEL = (0xEC, 0xE8, 0xDE, 255)
BEZEL_HI = (0xFA, 0xF8, 0xF2, 255)
BEZEL_MID = (0xD0, 0xCA, 0xBE, 255)
BEZEL_LO = (0xA8, 0xA2, 0x94, 255)
BEZEL_DK = (0x78, 0x72, 0x64, 255)
DRIVE = (0xC4, 0xBE, 0xB0, 255)
SCREEN = (0x0B, 0x1A, 0x10, 255)
TEXT = (0x6B, 0xFF, 0xA0, 255)
LED_PWR = (0x33, 0xCC, 0x55, 255)
LED_HDD = (0xE0, 0xA0, 0x20, 255)
LED_OFF = (0x4A, 0x3A, 0x20, 255)
SLOT = (0x2A, 0x28, 0x24, 255)
OUTLINE = (0x40, 0x3C, 0x38, 255)
BTN = (0x88, 0x40, 0x38, 255)
TRANS = (0, 0, 0, 0)

FONT_3x5 = {
    "C": ["011", "100", "100", "100", "011"],
    ":": ["000", "010", "000", "010", "000"],
    "\\": ["100", "100", "010", "001", "001"],
    "O": ["011", "101", "101", "101", "011"],
    "A": ["010", "101", "111", "101", "101"],
}
FONT_5x7 = {
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    ":": ["00000", "00100", "00000", "00000", "00100", "00000", "00000"],
    "\\": ["10000", "10000", "01000", "00100", "00010", "00001", "00001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "A": ["00100", "01010", "10001", "10001", "11111", "10001", "10001"],
}


def _put(px, x, y, c, w, h):
    if 0 <= x < w and 0 <= y < h:
        px[x, y] = c


def _fill(px, x0, y0, x1, y1, c, w, h):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            _put(px, x, y, c, w, h)


def _hline(px, x0, x1, y, c, w, h):
    for x in range(x0, x1 + 1):
        _put(px, x, y, c, w, h)


def _vline(px, x, y0, y1, c, w, h):
    for y in range(y0, y1 + 1):
        _put(px, x, y, c, w, h)


def _bevel(px, x0, y0, x1, y1, fill, w, h):
    _fill(px, x0, y0, x1, y1, fill, w, h)
    _hline(px, x0, x1, y0, BEZEL_HI, w, h)
    _vline(px, x0, y0, y1, BEZEL_HI, w, h)
    _hline(px, x0, x1, y1, BEZEL_LO, w, h)
    _vline(px, x1, y0, y1, BEZEL_LO, w, h)


def _outline(px, x0, y0, x1, y1, c, w, h, t=1):
    """Dark border just outside a component so beige doesn't melt into the page."""
    for i in range(t):
        _hline(px, x0 - 1 - i, x1 + 1 + i, y0 - 1 - i, c, w, h)
        _hline(px, x0 - 1 - i, x1 + 1 + i, y1 + 1 + i, c, w, h)
        _vline(px, x0 - 1 - i, y0 - i, y1 + i, c, w, h)
        _vline(px, x1 + 1 + i, y0 - i, y1 + i, c, w, h)


def _blit_text(px, x, y, text, font, color, w, h, scale=1, tracking=1):
    for ch in text:
        glyph = font[ch]
        gw = len(glyph[0])
        for row, bits in enumerate(glyph):
            for col, bit in enumerate(bits):
                if bit == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            _put(px, x + col * scale + dx, y + row * scale + dy, color, w, h)
        x += gw * scale + tracking


def draw_pc(size: int, screen_text: str, font: dict, text_scale: int = 1, tracking: int = 1) -> Image.Image:
    img = Image.new("RGBA", (size, size), TRANS)
    px = img.load()
    w = h = size
    s = size / 32.0

    def S(n):
        return int(round(n * s))

    mx0, my0, mx1, my1 = S(2), S(1), S(18), S(17)
    dx0, dy0, dx1, dy1 = S(19), S(2), S(30), S(17)
    bx0, by0, bx1, by1 = S(2), S(19), S(30), S(30)

    # Monitor
    _bevel(px, mx0, my0, mx1, my1, BEZEL, w, h)
    sx0, sy0, sx1, sy1 = S(4), S(3), S(16), S(13)
    _fill(px, sx0 - 1, sy0 - 1, sx1 + 1, sy1 + 1, OUTLINE, w, h)
    _fill(px, sx0, sy0, sx1, sy1, SCREEN, w, h)
    _put(px, S(15), S(15), LED_PWR, w, h)
    if size >= 32:
        _put(px, S(14), S(15), LED_OFF, w, h)
    _fill(px, S(8), S(17), S(12), S(18), BEZEL_MID, w, h)

    glyph_w = len(next(iter(font.values()))[0]) * text_scale
    glyph_h = len(next(iter(font.values()))) * text_scale
    tw = len(screen_text) * (glyph_w + tracking) - tracking
    sw = sx1 - sx0 + 1
    sh = sy1 - sy0 + 1
    tx = sx0 + max(1, (sw - tw) // 2)
    ty = sy0 + max(0, (sh - glyph_h) // 2)
    if tx + tw > sx1:
        tx = sx0 + 1
    _blit_text(px, tx, ty, screen_text, font, TEXT, w, h, text_scale, tracking)

    # Drive bay unit
    _bevel(px, dx0, dy0, dx1, dy1, BEZEL, w, h)
    _fill(px, S(20), S(4), S(29), S(8), DRIVE, w, h)
    _hline(px, S(20), S(29), S(4), BEZEL_DK, w, h)
    _hline(px, S(21), S(28), S(6), SLOT, w, h)
    if size >= 24:
        _put(px, S(28), S(7), BEZEL_DK, w, h)
    _fill(px, S(20), S(10), S(29), S(13), DRIVE, w, h)
    _hline(px, S(20), S(29), S(10), BEZEL_DK, w, h)
    _hline(px, S(21), S(26), S(12), SLOT, w, h)
    if size >= 32:
        _put(px, S(28), S(12), SLOT, w, h)

    # Case front (power / HDD)
    _bevel(px, bx0, by0, bx1, by1, BEZEL, w, h)
    if size >= 32:
        _fill(px, S(4), S(21), S(12), S(28), BEZEL_MID, w, h)
        step = max(1, S(1))
        for gy in range(S(22), S(28), step):
            c = BEZEL_DK if (gy // step) % 2 == 0 else BEZEL_LO
            _hline(px, S(5), S(11), gy, c, w, h)
    elif size >= 16:
        _fill(px, S(4), S(21), S(10), S(28), BEZEL_MID, w, h)

    if size >= 32:
        _fill(px, S(15), S(23), S(18), S(26), BTN, w, h)
        _put(px, S(15), S(23), BEZEL_HI, w, h)
    elif size >= 16:
        _put(px, S(14), S(24), BTN, w, h)

    if size <= 16:
        _put(px, S(22), S(24), LED_PWR, w, h)
        _put(px, S(25), S(24), LED_HDD, w, h)
    elif size < 48:
        _fill(px, S(21), S(23), S(23), S(25), OUTLINE, w, h)
        _fill(px, S(25), S(23), S(27), S(25), OUTLINE, w, h)
        _put(px, S(22), S(24), LED_PWR, w, h)
        _put(px, S(26), S(24), LED_HDD, w, h)
    else:
        _fill(px, S(20), S(22), S(23), S(26), OUTLINE, w, h)
        _fill(px, S(25), S(22), S(28), S(26), OUTLINE, w, h)
        _fill(px, S(21), S(23), S(22), S(25), LED_PWR, w, h)
        _fill(px, S(26), S(23), S(27), S(25), LED_HDD, w, h)
        _put(px, S(21), S(28), LED_PWR, w, h)
        _put(px, S(26), S(28), LED_HDD, w, h)

    # Larger sizes: near-black edge. Thickness scales with canvas so a 512
    # shown at 56px still reads as ~1px (thin absolute strokes vanish).
    if size >= 48:
        t = max(1, size // 64)
        edge = (0x20, 0x1C, 0x18, 255)
        _outline(px, mx0, my0, mx1, my1, edge, w, h, t)
        _outline(px, dx0, dy0, dx1, dy1, edge, w, h, t)
        _outline(px, bx0, by0, bx1, by1, edge, w, h, t)

    return img


def write_ico(images: list[Image.Image], path: Path) -> None:
    entries = []
    for im in images:
        if im.mode != "RGBA":
            im = im.convert("RGBA")
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        data = buf.getvalue()
        w, h = im.size
        entries.append((0 if w >= 256 else w, 0 if h >= 256 else h, data))
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, h, data in entries:
        out.write(struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for _, _, data in entries:
        out.write(data)
    path.write_bytes(out.getvalue())


def machine_asset_dirs(root: Path) -> list[Path]:
    dirs = []
    for p in sorted(root.glob("*/web/assets")):
        if p.is_dir():
            dirs.append(p)
    return dirs


def generate(out: Path, sync_machines: bool = True) -> None:
    out.mkdir(parents=True, exist_ok=True)

    v16 = draw_pc(16, "C:", FONT_3x5, text_scale=1, tracking=0)
    v32 = draw_pc(32, "C:\\", FONT_3x5, text_scale=1, tracking=1)
    v48 = draw_pc(48, "OAC", FONT_5x7, text_scale=1, tracking=1)
    v180 = draw_pc(180, "OAC", FONT_5x7, text_scale=4, tracking=3)
    v512 = draw_pc(512, "OAC", FONT_5x7, text_scale=12, tracking=8)

    v16.save(out / "favicon-16.png")
    v32.save(out / "favicon-32.png")
    v48.save(out / "favicon-48.png")
    v180.save(out / "apple-touch-icon.png")
    v512.save(out / "icon-512.png")
    write_ico([v16, v32, v48], out / "favicon.ico")

    print(f"wrote favicons to {out}/")

    if sync_machines:
        names = ("favicon-16.png", "favicon-32.png", "favicon.ico", "apple-touch-icon.png")
        for dest in machine_asset_dirs(ROOT):
            for name in names:
                src = out / name
                if not src.exists():
                    continue
                data = src.read_bytes()
                (dest / name).write_bytes(data)
            print(f"  synced {dest.relative_to(ROOT)}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "assets",
                    help="directory for site assets (default: assets/)")
    ap.add_argument("--no-sync", action="store_true",
                    help="do not copy into each machine's web/assets/")
    args = ap.parse_args(argv)
    generate(args.out.resolve(), sync_machines=not args.no_sync)
    return 0


if __name__ == "__main__":
    sys.exit(main())
