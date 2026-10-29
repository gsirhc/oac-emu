# oac-emu

Browser-based emulators of Old-Ass Computers built from scratch. Each machine is written in C++ compiled to WebAssembly. 

Visit [https://oac-emu.dev](https://oac-emu.dev) to use the emulators. 

## Emulators

Navigate to each machine's sub-directory for more information.  Every one has a `*_REVIEW.md` with its hardware findings and citations.  Also, some machines have a `*_PARITY.md` outlining how close it is to the real hardware.

## Frontend

The web frontend is pure JavaScript, HTML and CSS.  No 3rd party frameworks are used.  I also had a little fun with the themes, sorry.  

The monitors (CRTs / terminals) are realistic to the actual displays.  Though, I found realism effects like curved glass, scanlines and afterglow to be more annoying then cool.  So they are rendered clear and flat for readability and crispness.

## AI Statement

AI is used to write the majority of the code.  I know that will trigger some of you.  This project started out as a way for me to learn how to properly and responsibly use AI to write software.  It has turned into a passion of reliving the old days of computing and gaming.  I will say AI is not what the Kool-Aid drinkers think it is.  It takes a considerable amount of passion and work to keep AI on the rails.  However, as you can see here, it does ultimately write code much faster.  If you would like more details on how I use AI, email me at chris @ oac-emu . com.  Please keep it professional.  

Success is not measured by tools or methods, but rather passion, heart and ultimately the end result.

Earlier development history (before moving to the oac-emu repo): [https://github.com/gsirhc/retro](https://github.com/gsirhc/retro)

## License

Copyright (C) 2023-2026 Chris Gall. Licensed under the GNU Affero General
Public License v3.0. See `[LICENSE](LICENSE)`. If you run a modified copy as a
public website, you have to offer its source to the people using it.

Some files aren't mine and keep their own terms:

- `[assembler6502/firmware/wozmon.s](assembler6502/firmware/wozmon.s)` is Steve
Wozniak's Wozmon, from Apple's 1976 Apple-1 Operation Manual.
- `[shared/vendor/](shared/vendor/)` holds xterm.js (MIT) and the
Courier Prime and VT323 fonts (SIL Open Font License), each with its license
file beside it.
- Firmware, operating systems and fonts that are fetched at build time aren't
in this repo. That covers the Bochs BIOS, the LGPL VGA BIOS, FreeDOS, Altair
BASIC and IBM's VGA ROM font, and each one stays under its own license.

Arcade ROM sets aren't included. The cabinets ship their own self-test ROMs,
and a real set is something you load yourself.