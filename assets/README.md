# landing-page assets

- **Favicons** (`favicon-16.png`, `favicon-32.png`, `favicon-48.png`,
  `favicon.ico`, `apple-touch-icon.png`, `icon-512.png`) — pixel-art beige
  IBM PC (CRT + drive bays + case front with power/HDD LEDs). Small sizes
  show `C:\` on the screen; 48px and up show `OAC`. Also used as the landing
  page `.site-icon` and each machine page's title-bar `.pb-icon`. Regenerate
  with `python3 shared/gen_favicon.py` (Pillow); that also copies the four
  files each machine stages into every `*/web/assets/`.

- **`altair-panel.jpg`** — a screenshot of *this project's* rendered Altair 8800
  front panel (`#altair .fp-case`), captured from the emulator running Kill the
  Bit: MEMR / M1 / WO status lamps lit, the rotating bit on A11, the loop
  address on A0–A3. Not a photo of real hardware.

  Used as a landing-page tile (286×128 JPEG) — the LED labels aren't meant
  to be legible at that size, just the panel's shape.

  To regenerate after a panel restyle: build the front end
  (`make -C ../altair8800/web retro8080.js roms`), serve it, load
  `/?test=1&preset=baremetal`, click `#ptr .ptr-load`, wait for
  `__test.leds().addr & 0xff00`, then screenshot the `#altair .fp-case`
  element and resize to 286 px wide (progressive JPEG q≈88).

- **`altair-startrek.jpg`**: Super Star Trek's opening short range scan on
  the VT100, from *this project's* Altair 8800 running the `?tape=startrek`
  quick play (8K BASIC off paper tape, Star Trek off the 88-ACR cassette,
  both at 50×). Load `altair8800/?test=1&tape=startrek`, answer Y at
  PRESS Y TO ACCEPT COMMAND?, then screenshot `#screen` at 2× device scale;
  the scan box and status column (x 0–1112, y 378–876 of 1556×922) resized
  to 286×128 (JPEG q90).

- **`altair-cpm.jpg`**: CP/M 2.2 (the MITS 63K build) after a `DIR` of the
  boot diskette, from *this project's* Altair 8800 running the `?disk=cpm`
  quick play (CP/M preset, BOOT with the 88-DCDD at 50×). Load
  `altair8800/?test=1&disk=cpm`, type `DIR` at A>, then screenshot
  `#screen` at 2× device scale; the listing from `A>DIR` to the next A>
  (x 0–932, y 226–643 of 1556×922) resized to 286×128 (JPEG q90).

  These eight are the shipped Ready to Play tiles. The landing page's
  Thumbnails dropdown (Original by default) can swap in libretro-thumbnails'
  DOS screenshot, title screen, or box art (`data-art`, cropped by
  `data-<style>-y`), fetched by the visitor's browser. Star Trek and CP/M
  have no `data-art` (they aren't DOS titles), so they always show their own
  tiles. None
  of it is staged.

- **`ibmpcat-panel.jpg`** — the left side of *this project's* rendered IBM
  PC/AT front panel (`#frontPanelCard .at-case`): the IBM badge, the keylock
  panel with the power light lit, the grille and the red power switch. Not a
  photo of real hardware. 286×128 like the other tiles.

  To regenerate: build the front end (`make -C ../ibmpc-at/web ibmpcat.js
  roms hdd-image`), serve it, load `/?test=1&fast=1` at a 1200×900 viewport
  and 3× device scale, wait for `#powerLed.power-on`, insert a library disk
  into drive A:, add a style tag hiding `.at-bay-ctl` (the A:/B: readout sits
  on each drive face), then screenshot `#frontPanelCard .at-case`. Crop the
  leftmost full-height 286:128 window and resize to 286×128 (Lanczos,
  progressive JPEG q88).

- **`pacman-cabinet.png`** / **`mspacman-cabinet.png`** /
  **`frogger-cabinet.png`** / **`galaga-cabinet.png`** / **`galaxian-cabinet.png`** /
  **`scramble-cabinet.png`** — original
  landing-page tiles. Spec, pinned by
  each arcade machine's `web/tests/home.spec.ts`:

  - **286×128** PNG (same box as `altair-panel.jpg`)
  - **Courier New Bold** titles from `shared/courier_new_bold.atlas`
    (a raster of that system typeface — not a cabinet logotype, character
    sprite, or manufacturer mark)
  - generated starfield (nebula + planet, tinted per game)

  Built by `shared/marquee.py` (`python3 shared/marquee.py
  assets`; atlas rebuild `--rebuild-atlas`). Also invoked from
  each machine's `gen_hwtest.py`.

  These are the only arcade tiles that ship. The hub's Thumbnails dropdown
  (Original by default) has the visitor's browser load each card's `data-art`
  image from libretro-thumbnails
  (`thumbnails.libretro.com/MAME/{Named_Snaps,Named_Titles,Named_Boxarts}/<title>.png`),
  or its `data-marquee` marquee from the Arcade Database
  (`adb.arcadeitalia.net/media/mame.current/marquees/<set>.png`).
  None of that art is committed or staged.

- **`pc486-panel.jpg`** — a screenshot of *this project's* rendered 486
  front-panel turbo cluster (`#frontPanelCard .at-case` with `.at-drives`
  hidden), captured after auto-boot with Power + Turbo LEDs lit and the
  seven-segment readout on 66 -- not a photo of real hardware. Drive bays
  are trimmed so the left island fills a standard 286×128 card without
  squashing the whole fascia the way a full-width case shot would. Same
  progressive JPEG q≈88 as the Altair/AT tiles.

  `.at-case` also contains each drive bay's page-UI controls (`.at-bay-ctl`);
  hide those together with `.at-drives` before capture so the shot is
  hardware chrome only.

  To regenerate: build/serve the front end (`make -C ../pc486/web`, plus
  `roms`/`hdd-image` if needed), load `/?test=1&fast=1`, wait for
  `#powerLed.power-on`, inject
  `.at-drives, .at-bay-ctl { display: none !important; }` and
  `.at-case { width: max-content !important; }`, screenshot
  `#frontPanelCard .at-case`, then fit into 286×128 (uniform scale, beige
  letterbox) as a progressive JPEG q≈88.
