#!/usr/bin/env python3
"""Reassemble until as86 emits no 386 Jcc rel16 (0F 8x): each one becomes a short inverted Jcc over a JMP.
Also fails when code runs into a later .org, which as86 accepts silently."""
import re
import subprocess
import sys

OPCODE = {
    "beq": 0x74, "je": 0x74, "jz": 0x74, "bne": 0x75, "jne": 0x75, "jnz": 0x75,
    "blo": 0x72, "jb": 0x72, "jc": 0x72, "bhis": 0x73, "jae": 0x73, "jnc": 0x73,
    "blos": 0x76, "jbe": 0x76, "bhi": 0x77, "ja": 0x77, "js": 0x78, "jns": 0x79,
    "jo": 0x70, "jno": 0x71, "blt": 0x7C, "jl": 0x7C, "bge": 0x7D, "jge": 0x7D,
    "ble": 0x7E, "jle": 0x7E, "bgt": 0x7F, "jg": 0x7F,
}
LONG = re.compile(r"^(\d+) [0-9A-F]{4} +0F8[0-9A-F] ")
CODE = re.compile(r"^\d+ ([0-9A-F]{4}) ")


def check_orgs(listing):
    used = bytearray(0x10000)
    for line in open(listing, errors="replace"):
        m = CODE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        src = line[48:].split()
        if src and src[0] == ".align":
            continue
        if src and src[0] in (".blkb", ".blkw"):
            n = int(src[1].lstrip("$"), 16 if src[1].startswith("$") else 10) * (2 if src[0] == ".blkw" else 1)
        else:
            n = len("".join(line[11:48].split())) // 2
        for a in range(addr, addr + n):
            if a > 0xFFFF or used[a]:
                sys.exit(f"short_jcc: {a:04X} is assembled twice, code runs past a later .org")
            used[a] = 1


def main():
    src, out, listing = sys.argv[1], sys.argv[2], sys.argv[3]
    as86 = sys.argv[4:]
    lines = open(src).read().split("\n")
    n = 0
    for _ in range(20):
        open(src, "w").write("\n".join(lines))
        subprocess.run(as86 + [src, "-b", out, "-l", listing], check=True)
        hits = [int(m.group(1)) for m in map(LONG.match, open(listing, errors="replace")) if m]
        if not hits:
            check_orgs(listing)
            print(f"short_jcc: rewrote {n} long Jcc")
            return
        for ln in sorted(hits, reverse=True):
            op, target = lines[ln - 1].split()[:2]
            if op not in OPCODE:
                sys.exit(f"short_jcc: can't invert '{lines[ln - 1].strip()}' at line {ln}")
            n += 1
            # as86 assembles a forward branch long, so the bytes are spelled out: Jcc +3, JMP rel16.
            lines[ln - 1:ln] = [f"db {OPCODE[op] ^ 1:#04x}, 3", "db 0xE9", f"dw {target}-*-2"]
    sys.exit("short_jcc: no fixed point after 20 passes")


main()
