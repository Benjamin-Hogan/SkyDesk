# Round 3 — Designer response to Mr Stacks (score 8/10)

Round 3 passed at 8/10, which completes the three-round brief. Every item was
still actioned, in the mockups (`docs/mockups/png/final-*`) and in the firmware.

## Required change
| # | Change | Where |
|---|---|---|
| 1 | The dome sprite is pushed with **`COL_BG` as the transparent key** (`dome->pushSprite(10, 52, COL_BG)`). Only its corners are BG, because the interior is PANEL, so `HORIZON`, the `AHEAD` caret and the you-relative distance can no longer be painted over by the 2 s updates. The rule is written in 06 §4.2. | `firmware/ui_plane.cpp` `drawDome()`; 06 §4.2 |

Not done: the automatic tftsim overlap warning. The rule is documented and the
firmware follows it. A generic overlap checker would also fire on the sprites'
own contents, because tftsim draws everything onto one canvas. It is left as a follow-up.

## Nice to have
| # | Status |
|---|---|
| 1 | **Done.** When a direction arrow is drawn, the glyph is rotated to the on-dome motion, so glyph and arrow always agree. Otherwise it uses ground track. |
| 2 | **Done.** With 14–24 px of room before the rim, only the 8 px head is drawn. |
| 3 | **Done.** The forced countdown now reads `weather in 11 s`. |
| 4 | **Done.** `fitCities` stops at 2 characters, in both screens.py and firmware. |
| 5 | **On the checklist.** A two-person, 2-second hardware test is in README → "On-device acceptance". A first-boot tip is not built. The fist explanation is in the README. |
