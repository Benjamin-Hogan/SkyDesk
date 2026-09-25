# v2 map, round 2 — Designer response to Mr Stacks (score 7/10)

Round 2 passed at 7/10. The process runs three rounds, so every item was still
addressed. Renders: `docs/mockups/png/v2r3-map-*`. The spec is updated in `docs/08-plane-map.md`.

## Required changes
| # | Change | Check |
|---|---|---|
| 1a | The dim-amber prediction runs the **full v1 ENTER test at +60 s**: ≤ 3 nm, ≥ 25° up, ≥ 300 ft above ground, projecting along track, ground speed **and vertical rate**. It is the only meaning of dim amber | `map_screen.py` `mk()`/`qualifies()`; 08 §Visual rules |
| 1b | With the focus plane inside the disc, the strip's right side reads **`20° up · 2.1 mi NE`** (Font 2's native degree sign). That is the real trigger, worded like the card | `map-z0-selected` |
| 1c | **Decided: rename.** It is now the "nearby disc" (3 nm), documented as *not* the trigger, with the altitude-vs-trigger-distance table at the top of 08. The "will pop" signal is 1a | 08 §What it is |
| 1 re-mock | `z0-selected`: the selected SWA2073 is inside the disc at 20° up, so no card, and the strip says so. SWA2208 at 12,000 ft is dim amber because it *does* qualify within 60 s | `map-z0-selected` |
| 2 | Strip is now **`<operator> · <type>`**. The flight number is added only if it fits (in the mock, `Southwest 2073 · 737 MAX 8` doesn't, so it shows `Southwest · 737 MAX 8`). Too wide → the operator truncates; **the type never does** | `map-z0-selected`; 08 §Visual rules |

## Nice to have
| # | Status |
|---|---|
| 1 | **Done (spec + firmware).** When `totalInRadius > n`, a dotted coverage ring is drawn at the 40th plane's distance. No mock: the busy fixture has 30 |
| 2 | **Done.** Loading keeps the focus plane in the strip; `WIDENING...` sits by the coverage ring |
| 3 | **Done.** The focus plane never clusters or hosts a cluster. Its tag is guaranteed (right or left against the chrome only, on an `M_PANEL` backing when it overlaps glyphs). Badges flip left if they'd touch the focus ring |
| 4 | **Done.** Dismissed planes are white. Dim amber = "will pop" only |
| 5 | **Done.** `Nothing within 5 mi · nearest 7.2 mi NW` |

## Found while fixing
- **tftsim bug:** Font 2's `chr_f16_60` has `#ifdef GRAVE_IS_DEGREE … #else … #endif`
  *inside* the bitmap, and the parser read both branches. That produced a garbled
  2-byte-wide glyph wherever the Font 2 degree sign was used (the v1 Settings screen and
  the new strip). The parser now keeps only the default branch. This was a preview-only
  bug; the firmware was never affected.
