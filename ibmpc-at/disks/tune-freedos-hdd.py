#!/usr/bin/env python3
"""Trim the installed FreeDOS 1.3 startup files on the shipped C: (IBM_PCAT_REVIEW.md §61)."""
import os
import subprocess
import sys
import tempfile

OFFSET = 17 * 512  # partition 1 starts at sector 17 (ST-4038 geometry)
NOTE = "Changed by oac-emu 2026-10-23: no menu countdown, no MEM, no CD-ROM or mouse driver"

CONFIG_EDITS = [
    ("MENUDEFAULT=1,5", "MENUDEFAULT=1,0"),
]
AUTO_EDITS = [
    (":Support286\nFDAPM APMDOS\nCTMOUSE\nMEM /C /N\n", ":Support286\nFDAPM APMDOS\nREM CTMOUSE\nREM MEM /C /N\n"),
    (":Only8086\nMEM /C /N\n", ":Only8086\nREM MEM /C /N\n"),
    (":FINAL\nMEM /C /N\n", ":FINAL\nREM MEM /C /N\n"),
    ("if exist %dosdir%\\bin\\cdrom.bat call %dosdir%\\bin\\cdrom.bat display\n",
     "REM if exist %dosdir%\\bin\\cdrom.bat call %dosdir%\\bin\\cdrom.bat display\n"),
]


def edit(img, name, edits, note_line, after=None):
    target = f"{img}@@{OFFSET}"
    text = subprocess.run(["mtype", "-i", target, "::/" + name], check=True, capture_output=True).stdout.decode("ascii")
    text = text.replace("\r\n", "\n")
    if NOTE in text:
        return
    for old, new in edits:
        if text.count(old) != 1:
            sys.exit(f"tune-freedos-hdd: {name}: expected one {old!r}")
        text = text.replace(old, new)
    if after:
        if not text.startswith(after):
            sys.exit(f"tune-freedos-hdd: {name}: expected to start with {after!r}")
        text = after + note_line + "\n" + text[len(after):]
    else:
        text = note_line + "\n" + text
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, name)
        with open(path, "wb") as f:
            f.write(text.replace("\n", "\r\n").encode("ascii"))
        subprocess.run(["mdel", "-i", target, "::/" + name], check=True)
        subprocess.run(["mcopy", "-i", target, path, "::/" + name], check=True)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tune-freedos-hdd.py <freedos-hdd.img>")
    img = sys.argv[1]
    edit(img, "FDCONFIG.SYS", CONFIG_EDITS, "; " + NOTE)
    edit(img, "FDAUTO.BAT", AUTO_EDITS, "REM " + NOTE, after="@ECHO OFF\n")


if __name__ == "__main__":
    main()
