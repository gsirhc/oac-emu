#!/usr/bin/env bash
# Source for the third-party code the PCs ship, published as the
# "third-party-sources" GitHub release the PC pages' footer notices link to.
#   ./publish_sources.sh           # build third_party_sources/ only
#   ./publish_sources.sh --upload  # also create/update the release (needs gh)
set -euo pipefail
cd "$(dirname "$0")"

TAG=third-party-sources
OUT=third_party_sources
# LGPL VGABIOS 0.9d, the source of Bochs' VGABIOS-lgpl-latest.bin at the pinned Bochs commit
VGABIOS_COMMIT=12d978bd86fe88ee6b8620b0f1e91c7fb81bc2e6

BOCHS_COMMIT=$(sed -n 's|.*bochs-emu/Bochs/\([0-9a-f]\{40\}\)/.*|\1|p' pc486/roms/fetch-bios.sh | head -1)
FD_URL=$(sed -n 's|^URL="\(.*FD13-LiveCD.zip\)"|\1|p' pc486/disks/fetch-freedos-cd.sh)
FD_SHA=$(sed -n 's|^ZIP_SHA="\([0-9a-f]*\)"|\1|p' pc486/disks/fetch-freedos-cd.sh)
[ -n "$BOCHS_COMMIT" ] && [ -n "$FD_URL" ] && [ -n "$FD_SHA" ] || { echo "publish_sources: couldn't read the pins" >&2; exit 1; }
[ "$BOCHS_COMMIT" = "$(sed -n 's|.*bochs-emu/Bochs/\([0-9a-f]\{40\}\)/.*|\1|p' ibmpc-at/roms/fetch-bios.sh | head -1)" ] \
  || { echo "publish_sources: the PCs pin different Bochs commits" >&2; exit 1; }

sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | awk '{print $1}'; else shasum -a 256 "$1" | awk '{print $1}'; fi; }

mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if [ ! -f "$OUT/FD13-LiveCD.zip" ] || [ "$(sha "$OUT/FD13-LiveCD.zip")" != "$FD_SHA" ]; then
    curl -sSfL --retry 3 -o "$tmp/fd.zip" "$FD_URL" || curl -sSfL --retry 3 -o "$tmp/fd.zip" "https://web.archive.org/web/2023id_/$FD_URL"
    [ "$(sha "$tmp/fd.zip")" = "$FD_SHA" ] || { echo "publish_sources: FD13-LiveCD.zip checksum mismatch" >&2; exit 1; }
    mv "$tmp/fd.zip" "$OUT/FD13-LiveCD.zip"
fi

BOCHS_TAR="bochs-bios-${BOCHS_COMMIT:0:12}.tar.gz"
if [ ! -f "$OUT/$BOCHS_TAR" ]; then
    git -C "$tmp" init -q bochs
    git -C "$tmp/bochs" fetch -q --depth 1 --filter=blob:none https://github.com/bochs-emu/Bochs.git "$BOCHS_COMMIT"
    git -C "$tmp/bochs" archive --prefix="bochs-bios-${BOCHS_COMMIT:0:12}/" -o "$PWD/$OUT/$BOCHS_TAR" "$BOCHS_COMMIT" bochs/bios bochs/COPYING
fi

PATCH_SUM=$(cat ibmpc-at/roms/bochs-bios/Makefile ibmpc-at/roms/bochs-bios/short_jcc.py ibmpc-at/roms/bochs-bios/patches/*.patch | { sha256sum 2>/dev/null || shasum -a 256; } | cut -c1-12)
AT_PATCH_TAR="ibmpcat-bochs-bios-patches-$PATCH_SUM.tar.gz"
rm -f "$OUT"/ibmpcat-bochs-bios-patches-*.tar.gz
tar -czf "$OUT/$AT_PATCH_TAR" -C ibmpc-at/roms bochs-bios/Makefile bochs-bios/short_jcc.py bochs-bios/patches

VGA_TAR="vgabios-0.9d-${VGABIOS_COMMIT:0:12}.tar.gz"
if [ ! -f "$OUT/$VGA_TAR" ]; then
    curl -sSfL --retry 3 -o "$OUT/$VGA_TAR" "https://github.com/bochs-emu/VGABIOS/archive/$VGABIOS_COMMIT.tar.gz"
fi

cp ibmpc-at/roms/fntcol16.zip "$OUT/fntcol16.zip"

ls -l "$OUT"
[ "${1:-}" = "--upload" ] || exit 0

NOTES="Source code for the third-party software the oac-emu PCs ship:

- FD13-LiveCD.zip: FreeDOS 1.3 LiveCD, unchanged ($FD_URL, SHA-256 $FD_SHA). Every package on it carries its source under SOURCE/. It covers the FreeDOS C: images on both PCs and the Disk library floppies.
- $BOCHS_TAR: Bochs BIOS source (bochs/bios) at bochs-emu/Bochs@$BOCHS_COMMIT. The 486 ships its BIOS-bochs-legacy unchanged. GNU LGPL 2.1.
- $AT_PATCH_TAR: the patches, the build recipe (dev86 0.16.21) and its jump-shortening script that turn that source into the IBM PC/AT's modified BIOS-bochs-legacy. GNU LGPL 2.1.
- $VGA_TAR: LGPL VGABIOS 0.9d at bochs-emu/VGABIOS@$VGABIOS_COMMIT, the source of the 486's VGABIOS-lgpl-latest.bin. GNU LGPL 2.1.
- fntcol16.zip: Yossi Gil's EGA/VGA font collection. The IBM PC/AT's EGA BIOS uses its VGA-ROM.F08 and VGA-ROM.F14."

if gh release view "$TAG" >/dev/null 2>&1; then
    gh release edit "$TAG" --notes "$NOTES"
    gh release upload "$TAG" "$OUT"/* --clobber
else
    gh release create "$TAG" "$OUT"/* --title "Third-party sources" --notes "$NOTES" --target main
fi
