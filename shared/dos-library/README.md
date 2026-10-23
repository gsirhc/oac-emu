# DOS disk library (`shared/dos-library/`)

Builds the floppies behind the **Disk library** button on `ibmpc-at` and
`pc486`. Each machine lists its disks in its own `disks/library.json`; `make
-C <machine> library` builds them into `disks/library/` and the web
Makefile's `library-images` copies them, with `library.json`, into
`web/disks/library/`. Images are git-ignored and rebuilt by CI.

- `build-library.py` — FAT12 images via mtools (`mformat`, `mmd`, `mcopy`).
  Formats: 360K, 1.2MB, 1.44MB. Volume serials are derived from the label so a
  rebuild is byte-stable apart from directory timestamps.

## `library.json`

| key | meaning |
|---|---|
| `id`, `title`, `year`, `publisher`, `type` | shown in the dialog |
| `needs` | what it takes to run |
| `description`, `help`, `license` | shown in the dialog; FreeDOS disks also get `help` in `README.TXT` |
| `images` | `[{ file, label }]`, one per disk, in order |
| `format` | size in K, only for an entry with no `build` (a prebuilt image) |
| `build.format` | 360, 1200 or 1440 |
| `build.packages` | FreeDOS package names, unpacked under `\NAME\` and run from the floppy |
| `build.install_dir` | set for a set too big for one disk: files are split across the disks and every disk carries the same `INSTALL.BAT` (COMMAND.COM re-reads it after each disk swap) and a `DISKn.TAG` it checks for before copying. It sticks to DOS 3.3 batch and XCOPY options (no `/Y`), since a visitor may have put an older DOS on C: |

The builder fails if a set needs more or fewer disks than `images` lists, or a
file won't fit.

## Provenance

**FreeDOS packages** come out of the FreeDOS 1.3 LiveCD ISO that pc486 already
fetches and verifies (`pc486/disks/fetch-freedos-cd.sh`, the zip's published
SHA-256). The live 1.3 package repository has drifted from the CD, so the CD is
the pinned source. Packages are copied unchanged, minus `SOURCE/`, `APPINFO/`
and the link batch files. Licenses are GPL, BSD or public domain per program;
each disk's `README.TXT` lists them from the package's LSM file.

| package | needs |
|---|---|
| senet, hangman, eliza, zmiy, e3, uhex, doszip, freemacs | 8086/286 |
| vitetris | `TETRIS16.EXE` on a 286 with NANSI.SYS; `TETRIS.EXE` is DJGPP, 386 |
| tde, pico, sudoku86, blkdrop, dn2, wing, bolitare | 386 (TDE's `TDER.EXE` uses a `66h` prefix, so it is 386-only too) |

NetHack is left out: its `NETHACK.EXE` alone is larger than a 1.44MB disk.

## Disk swaps

Both PCs run the Bochs BIOS stand-in, which reports "drive present, does not
support change line" (rombios.c, INT 13h AH=15h/16h). DOS then can't use the
FDC's DSKCHG and falls back to its timing check, so a disk swapped and
confirmed within a second or two of the last access can be missed, and XCOPY
then reads the previous disk's cached directory. `INSTALL.BAT` checks for the
disk's `DISKn.TAG` and asks again ("Can't see disk n yet.") until DOS sees the
new disk.
