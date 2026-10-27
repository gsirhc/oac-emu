#!/usr/bin/env bash
# Build the CuteMouse driver floppy for the Disk library: a 1.2MB FAT12 image
# with CTMOUSE.EXE and its two diagnostics, for this machine's serial mouse.
# Files come from the FreeDOS HDD image (disks/freedos-hdd.img).
set -euo pipefail

cd "$(dirname "$0")"

HDD=freedos-hdd.img
OUT=ctmouse.img
CTMOUSE_SHA=822cf550c9e19a22785722d2306aa08ede10ff20bfe931d5a81a15f77c5f363e
BIN_DIR="::/FREEDOS/BIN"
TOOL_DIR="::/FREEDOS/BIN/CTMOUSE"

if [ ! -f "$HDD" ]; then
    echo "build-ctmouse-floppy: $HDD not found -- run 'make hdd-image' first" >&2
    exit 1
fi
if ! command -v mformat >/dev/null 2>&1; then
    echo "build-ctmouse-floppy: mtools not installed (brew install mtools)" >&2
    exit 1
fi

export MTOOLS_SKIP_CHECK=1

PART_OFFSET=$(( $(od -An -t u4 -j 454 -N 4 "$HDD" | tr -d ' ') * 512 ))

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mtype -i "$HDD@@$PART_OFFSET" "$BIN_DIR/CTMOUSE.EXE"   > "$tmp/CTMOUSE.EXE"
mtype -i "$HDD@@$PART_OFFSET" "$TOOL_DIR/MOUSETST.COM" > "$tmp/MOUSETST.COM"
mtype -i "$HDD@@$PART_OFFSET" "$TOOL_DIR/PROTOCOL.COM" > "$tmp/PROTOCOL.COM"
mtype -i "$HDD@@$PART_OFFSET" "::/FREEDOS/DOC/KERNEL/COPYING" > "$tmp/COPYING.TXT"

got=$(shasum -a 256 "$tmp/CTMOUSE.EXE" | cut -d' ' -f1)
if [ "$got" != "$CTMOUSE_SHA" ]; then
    echo "build-ctmouse-floppy: CTMOUSE.EXE checksum mismatch" >&2
    echo "  want $CTMOUSE_SHA" >&2
    echo "  got  $got" >&2
    exit 1
fi

printf 'CuteMouse 2.1b4 -- DOS mouse driver (INT 33h) for this machine'\''s\r\nserial mouse on COM1.\r\n\r\nFDAUTO already runs CTMOUSE from C: at boot. This floppy is for\r\nMOUSETST and a spare copy of the driver.\r\n\r\n  A:\\MOUSETST        confirm the driver sees movement\r\n  A:\\PROTOCOL        report which protocol was detected\r\n  A:\\CTMOUSE         load again if you unloaded it\r\n  A:\\CTMOUSE /U      unload\r\n\r\nDo not use /P here; that forces the PS/2 path, and this AT has no AUX\r\nport.\r\n\r\nIn the emulator: tick Mouse, then CLICK the screen to take pointer\r\nlock -- without lock the page sends no movement. Your Esc key releases\r\nthe lock instead of reaching DOS, so use the Esc button on the bezel.\r\n\r\nCuteMouse is GPL software, taken from the FreeDOS distribution this\r\nmachine already ships. COPYING.TXT has the license. Its source code is\r\nin the ctmouse package on the FreeDOS 1.3 LiveCD,\r\n  https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/FD13-LiveCD.zip\r\n  mirrored at https://github.com/gsirhc/oac-emu/releases/tag/third-party-sources\r\n' > "$tmp/README.TXT"

rm -f "$OUT"
# 80 cyl x 2 heads x 15 sec = 1.2MB
dd if=/dev/zero of="$OUT" bs=512 count=2400 status=none
mformat -i "$OUT" -f 1200 -v CTMOUSE ::
for f in CTMOUSE.EXE MOUSETST.COM PROTOCOL.COM README.TXT COPYING.TXT; do
    mcopy -i "$OUT" "$tmp/$f" "::$f"
done

echo "build-ctmouse-floppy: wrote $OUT"
mdir -i "$OUT" ::
