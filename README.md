# oac-emu

Browser-based emulators of Old-Ass Computers built from scratch. Each machine is written in C++ compiled to WebAssembly. 

Visit [https://oac-emu.dev](https://oac-emu.dev) to use the emulators. 

## Emulators

Navigate to each machine's sub-directory for more information.  Every one has a `*_REVIEW.md` with its hardware findings and citations.  Also, some machines have a `*_PARITY.md` outlining how close it is to the real hardware.

## Frontend

The web frontend is pure JavaScript, HTML and CSS.  No 3rd party frameworks are used.  I also had a little fun with the themes, sorry.  

The monitors (CRTs / terminals) are realistic to the actual displays.  Though, I found realism effects like curved glass, scanlines and afterglow to be more annoying then cool.  So they are rendered clear and flat for readability and crispness.

## AI Statement

AI is scary.  As a professional software developer of over 25 years, its by far the biggest shift I've had to make.  So I started this project as a way to learn how to use AI properly and hopefully responsibly.  Yes, the majority of the code here was written by AI.  I know that will trigger some people, myself included.  However, it's not as easy as people make it sound.  AI tends to go off the rails and do its own thing without provication.  It took time and passion to get it to write good code where I had to constantly box it in and do heavy reviews.  How do I feel about myself using AI?  Not that great.  I understand how the code works but not intimately.  Dumb and dumber.  However, its really hard to overlook AI's benefits.  I was able to create 3 major system emulators and a bunch of arcades in a couple of months.  And they work well.  You no longer need a degree in software engineering.  You need a degree in project management.  So yeah developers are screwed, the haters will hate and you can't stop any of it.  You can only survive.  If you would like more details on how I use AI, email me at chris @ oac-emu . com.  Please keep it professional.  

"First AI came for the software engineers and we didn't speak up.  So now it comes for the scientists and mathmeticians..."

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