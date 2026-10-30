# Donkey Kong -- parity backlog

The open gaps between this machine and a real TKG4 board. When an item
lands, write the current behaviour and its source into `DKONG_REVIEW.md`
and delete it here.

Rough parity today: **~90%**. The US set plays through attract, intro,
all four stages and the sound program. What's left are values fitted to
the game's own output where the scan isn't legible, plus a few sound
wiring details.

| Area | Parity | Biggest gap |
|------|--------|-------------|
| CPU core (Z80) | shared, high | — |
| Memory decode / DMA | high | 8257 per-byte interleave with the CPU (M1) |
| Video | high | Object adder constants calibrated (V1) |
| Sound | medium-high | Parts of the discrete wiring assumed (S1-S4) |
| Inputs / DIP | high | — |
| High score | high | — |

Every fix lands with its native test and, where it's visible from the
page, a Playwright test, in the same commit.

## Video

- **V1. Object adder constants.** The `0xF9` V offset and `-8` H offset
  (DKONG_REVIEW.md §5.2) are fitted to the game: Mario's feet on the
  girder and centred on a ladder. The 7R/8R/7P/8P and 7K/8K preset inputs
  aren't legible in the scan.
- **V2. Character plane order.** 3P as the high plane is chosen from the
  game's look (the bright BONUS box). The route from 3P/3N into the
  colour mux isn't legible.
- **V3. VRAM busy decode.** The LS20 inputs that free VRAM at H 256…375
  are assumed to be 64H/32H/16H/8H.

## Memory / DMA

- **M1. DMA interleave.** The 8257 copy runs all at once on the `$7D85`
  edge and the CPU stall follows it. Per-byte timing against the beam
  isn't modelled; nothing in the game depends on it so far.

## Sound

- **S1. 8L astables.** The two-inverter (R47/R48, C30) and
  three-inverter (R38, C26) topologies are read from a faint part of the
  sheet; their exact feedback paths are assumed.
- **S2. R50 node and Q3 bias.** Where R50 meets CV2, and Q3's bias
  network in the mixer, are assumed.
- **S3. VR2.** The volume pot is taken at full, so the DAC goes straight
  into R1.
- **S4. INS A,BUS address.** The bus read with P26 low uses 3F at
  `(P2 & 7) << 8`; the address the 8035 drives for INS isn't confirmed.
- **S5. 2VF phase under flip.** The noise clock ignores FLIP.
