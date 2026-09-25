# Round 1 — Designer response to Mr Stacks (score 6/10)

Round 1 scored below 7, so every required change was addressed. Round-2 mockups:
`docs/mockups/png/r2-*.png` (1×, honest device pixels) and `r2-*@3x.png`
(nearest-neighbour 3×). Source: `docs/mockups/screens.py`.

## The big change (R16): honest renders
The HTML/Chrome pipeline is gone. `docs/mockups/tftsim.py` is a small TFT_eSPI
look-alike. It parses the **real** GLCD, Font 2 and FreeFont bitmaps out of
`.pio/libdeps/usb/TFT_eSPI/Fonts`, draws non-anti-aliased primitives, and
quantises every pixel to RGB565. It **raises an error** if a string contains a
glyph that the font doesn't have, so R6 and R7 can't come back. Its method names
mirror TFT_eSPI (`fillRect`, `drawArc`, `drawWideLine`, `drawString` with
baseline datums), so `screens.py` is now an executable layout spec the firmware
ports directly. Every aircraft number (bearing, elevation, fists, distance,
trail) comes from `geo.py` (a Python copy of `05-sky-geometry.md`) using a real
position. That also covers your nice-to-have #1.

## Required changes
| # | Change | Where to check |
|---|---|---|
| R1 | Azimuth degrees are dropped from spec and screen. The LOOK block is now `NE` (fsb24) over `42° up` (fsb18: number, ring, and *up* on one baseline) | `r2-plane-airliner-multi`; spec 01 §What the Plane screen must answer; 06 §4.3 |
| R2 | Header line 1 is `Southwest · 737-800` in fsb12. Line 2 is `WN 2208 · N8563Z` in f2. GA is `Cessna 172` / `N172SP · Private`. Fallback and shortening rules are in 06 §1 and §4.1 | all plane mocks |
| R3 | New states: **route unverified** (dim codes + `ROUTE UNVERIFIED` WARN outline tag, on the real SWA1637 PIT→TPA case) and **airliner route unknown** | `r2-plane-unverified`, `r2-plane-overhead` |
| R4 | The HEADING cell is removed. Direction of travel is shown only by the dome trail. The spec forbids a second compass word | 01 §priority 4, 06 §4.5 |
| R5 | Trend is a 12 px TEXT-colored triangle/bar plus the word `Climbing`/`Descending`/`Level` in fs9. Units: `12,400 ft`, `NW 7 mph` | stats bar, weather details |
| R6 | All non-ASCII glyphs are drawn primitives (degree ring, arrow, separator dot, ✓ ✕ ○, chevron, ellipsis), each with a named helper. Font 2's native degree sign (backtick) is documented. The simulator enforces this | 06 §1 glyph table |
| R7 | No bold Font 2 anywhere. Hourly temps are `fsb9`, dome letters are `f2` regular | `r2-weather` |
| R8 | The dome has an `UP` zenith label, dashed 30°/60° rings, and a faint sector plus rim arc for "that slice of sky". There is a new **Setup → Facing** screen (VIEW_UP_DEG saved in NVS). You-relative mode shows an `AHEAD` caret and says `behind you` instead of fists | `r2-plane-ga-rotated`, `r2-setup-facing` |
| R9 | Trail is now **60 s**, a solid 2 px line with an 8 px head, starting 12 px out from the glyph. It is hidden if the shaft would be < 12 px | all plane mocks; 05 §5 |
| R10 | Mocked: (a) straight up (`UP / overhead / lean back`, with a zenith ring in place of the sector), (b) departing (dimmed accent, `LEAVING` pill, shrinking grace bar, "back to weather in 3 s"), (c) multiple planes (hollow dot + `+1 more`), (d) forced (`NOT OVERHEAD` pill, `18 s`, countdown bar), (e) stale (hollow dim glyph, `last seen 8 s ago` in WARN) | `r2-plane-*` (7 plane mocks) |
| R11 | The dome is a **4-bit palettised sprite**, 144×144 ≈ 10 KB, with an exact 16-color palette. Architecture budget updated | 03 §Rendering, §Memory |
| R12 | Flicker plan rewritten: every changing FreeFont region (LOOK block, stats values, hero temp, clock) goes into a small persistent 4-bit sprite and is pushed only when its displayed string changes. Font 2/GLCD keep bg + padding | 03 §Rendering |
| R13 | Hourly precip % in `glcd RAIN`, shown only if ≥ 20 %. When shown, the icon shifts left and the % sits to its right | `r2-weather` |
| R14 | Icons cut out with the **container** color (the moon uses PANEL in the strip) | `r2-weather-degraded` |
| R15 | The healthy header shows nothing on the right. "Gilbert" is gone. Status appears only when degraded | `r2-weather` vs `r2-weather-degraded` |
| R16 | See above | `tftsim.py` |

## Nice to have: taken
1. Mock data is computed from geometry (e.g. airliner el = 41.9°).
2. A faint filled sector was added, and the rim arc is kept.
3. Arrival cue is specified: wipe, then header bar flashes twice, then LED amber pulse ×2 (06 §6).
4. Night brightness is specified: 35 % from sunset+30 min, LDR cap, 100 % while a plane is shown (06 §6).
5. The traffic chip shows the nearest type, distance and bearing plus a drawn chevron.
6. Hero temperature goes MUTED when data is stale.
7. The boot happy path is mocked, with distinct ✓ / busy dots / ○ / ✕ markers.
9. `COL_DIM` is raised to #7886A2 (5.1:1 on BG, 4.65:1 on PANEL). `COL_MUTED` is #9AA6BE (7.7:1).

## Nice to have: deferred
8. Off-axis navy tier check. This can only be done on real hardware, so it's
   listed in the README's on-device checklist.
