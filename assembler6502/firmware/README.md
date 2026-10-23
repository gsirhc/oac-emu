Firmware
========

6502 ROM source for the assembler emulator. Needs the cc65 toolchain
(https://github.com/cc65/cc65) on the PATH.

Memory map is in link.cfg.

To build the ROM, run make.sh. Output goes to tmp/firmware.bin, with a label
file at tmp/firmware.lbl.

compile.sh builds, then writes the image to an AT28C256 with minipro.

wozmon.s is Steve Wozniak's Wozmon from the Apple-1 Operation Manual (Apple Computer, 1976) listing.
