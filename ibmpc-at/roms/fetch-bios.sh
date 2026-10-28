#!/usr/bin/env bash
# Fetch the source of the freely-licensed AT BIOS this machine substitutes for
# IBM's copyrighted one (Bochs Project, LGPL), the dev86 toolchain that
# bochs-bios/ builds it with, and the fonts egabios/ assembles into its EGA
# BIOS. Pinned to commits and SHA-256 verified. Files land next to this script.
#
#   ./fetch-bios.sh            # fetch what's missing / wrong
#   ./fetch-bios.sh --force    # re-fetch everything
#
# Bochs rather than SeaBIOS: SeaBIOS needs an i386 cross compiler. BIOS-bochs-legacy
# is the plain-ISA/no-PCI build of rombios.c. See IBM_PCAT_REVIEW.md §6.
#
# Both BIOSes are compatible stand-ins, not the genuine firmware.

set -euo pipefail
cd "$(dirname "$0")"

FORCE="${1:-}"

# Pinned to bochs-emu/Bochs@ff17a0c2bbabccf96d33af4e08ba8061889b079d (master, fetched 2026-09-05).
BASE="https://raw.githubusercontent.com/bochs-emu/Bochs/ff17a0c2bbabccf96d33af4e08ba8061889b079d/bochs/bios"

# dev86 0.16.21 (bcc, as86), lkundrak/dev86@c6f36cbafa979710b809f117562773dbd6825918.
DEV86="https://github.com/lkundrak/dev86/archive/c6f36cbafa979710b809f117562773dbd6825918.tar.gz"

# Yossi Gil's font collection (fntcol16.zip, Simtel msdos/screen) for egabios/. Two Simtel mirrors, same bytes.
FONTS="https://ftp.icm.edu.pl/pub/simtelnet/msdos/screen/fntcol16.zip https://ftp.sunet.se/mirror/archive/ftp.sunet.se/pub/simtelnet/msdos/screen/fntcol16.zip"

# name  |  url  |  sha256
FILES=(
  "bochs-bios/src/rombios.c|$BASE/rombios.c|1684aa54044872c8faf59dae2d6a465cb5fe1274f3c8ff0112f27e7eaea21703"
  "bochs-bios/src/rombios.h|$BASE/rombios.h|ef0eec27bfac0c88540182728220ca0df837ce9fb45a7291ca7791ef0bd17f5a"
  "bochs-bios/src/apmbios.S|$BASE/apmbios.S|af9f36c078f2709db7f65385f4eefddfae0f3c5558aff6eb8a4ffa265222d137"
  "bochs-bios/src/biossums.c|$BASE/biossums.c|751f5d72f7c3299f6c3e3fe7f3dc4734a935fa4248d67c0f7d40f311df40182d"
  "bochs-bios/src/dev86.tar.gz|$DEV86|6485bf3605b91a7f69b35e36727aac946a77d4dab84f7d83a9863b3ab47c80ff"
  "fntcol16.zip|$FONTS|ee875bb96a5be0cb70f87e1fc3ce85aa4c0c1ed78f958bb8160f9023a25d32a6"
)

sha() {
  if command -v sha256sum >/dev/null; then sha256sum "$1" | awk '{print $1}';
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}

for row in "${FILES[@]}"; do
  IFS='|' read -r name url want <<<"$row"
  if [ -z "$FORCE" ] && [ -f "$name" ] && [ "$(sha "$name")" = "$want" ]; then
    echo "ok    $name"
    continue
  fi
  echo "fetch $name"
  tmp="$(mktemp)"
  fetched=
  for u in $url; do
    if curl -sSfL --retry 3 -o "$tmp" "$u"; then fetched=1; break; fi
  done
  [ -n "$fetched" ] || { echo "  could not fetch $name" >&2; rm -f "$tmp"; exit 1; }
  got="$(sha "$tmp")"
  if [ "$got" != "$want" ]; then
    echo "  SHA-256 mismatch for $name" >&2
    echo "  want $want" >&2
    echo "  got  $got" >&2
    rm -f "$tmp"
    exit 1
  fi
  mkdir -p "$(dirname "$name")"
  mv "$tmp" "$name"
  chmod 644 "$name"
done

echo "BIOS source, dev86 + EGA BIOS fonts ready."
