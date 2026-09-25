# Design Review — Round 2 — Mr Stacks

Reviewed: `round-1-response.md`; all 12 `png/r2-*@3x.png` (plus crops of the 1× files);
`mockups/screens.py`, `tftsim.py`, `geo.py`; the rewritten `06-ui-spec.md`;
`03-architecture.md` §Rendering/§Memory; `01-product-spec.md`; `05-sky-geometry.md` §4–5.
I also ran `screens.py` to print the per-scenario geometry. I rendered three stress
cases into my scratchpad (long operator, long type, low elevation) using the project's
own `plane()`, and checked a few things in the installed TFT_eSPI source
(`.pio/libdeps/usb/TFT_eSPI`).

## Score: 7/10

## Verdict
A big step up, and the right kind. The hierarchy now matches the spec: where, then
what, then route, then numbers. Every plane state exists. The renderer uses the real
fonts, and every number is computed rather than typed. The remaining problems are
bounded defects, not direction problems. The trail arrow is silently hidden on the
headline airliner screen. Two glyph states (overhead, stale) render as blobs.
FreeSansBold9 merges "88". The header and city lines have fit bugs. The 4-bit sprite
plan clashes with TFT_eSPI's anti-aliased primitives. It's a hard 7: clear the
required list below and this is an 8.

## Round 1 items

| # | Item | Status | Evidence / remaining gap |
|---|---|---|---|
| R1 | Elevation not readable as a bearing | **Fixed** | `42° up` on one fsb18 baseline. Azimuth degrees are removed from spec 01 and the screen. |
| R2 | Type as priority #2 | **Fixed** | `Southwest · 737-800` in fsb12, flight/reg in f2. New fit bug when strings get long: see required #5. |
| R3 | Route unverified / unknown states | **Fixed** | `r2-plane-unverified` uses the real SWA1637 PIT→TPA case, DIM codes, and a `ROUTE UNVERIFIED` text tag. `r2-plane-overhead` shows "Route unknown". Well done. |
| R4 | No second compass word | **Fixed** | HEADING cell gone, and the rule is written into 01 §priority 4 and 06 §4.5. |
| R5 | Visible trend + units | **Fixed** | 12 px triangle + `Climbing` in fs9, `ft`, `mph`. New digit-merge issue in the values: see required #4. |
| R6 | ASCII-only glyphs | **Fixed** | Primitive helpers exist, and tftsim raises on missing glyphs. |
| R7 | No bold Font 2 | **Fixed** | Hourly temps are fsb9. Dome letters are f2 regular. |
| R8 | Dome understandable by a normal person | **Partially fixed** | Done: the `UP` zenith label, the Setup → Facing screen, the `AHEAD` caret, and relative words. Missing: (a) the rim `horizon` label; (b) the relative phrase (`behind you`) is f2 MUTED, which can't be read past about 1 m, yet it's the most useful words on the card for a non-pilot; (c) no sign that the 2-second non-pilot test was run. |
| R9 | Readable trail | **Not fixed in practice** | The spec'd shape is right, but the hide rule adds up to "dome displacement ≥ 32 px" (12 start offset + 8 head + 12 shaft). By `screens.py`'s own numbers, the trail is **hidden in 4 of 7 plane mocks**: airliner-multi L=22 px, unverified L=22, departing L=9, forced L=10. With R4 done, the headline screen now shows direction of travel **nowhere**. See required #1. |
| R10 | Missing states | **Partially fixed** | All five states are mocked, and (b), (c), (d) are good. But (a) overhead and (e) stale both render as broken glyphs: see required #2 and #3. |
| R11 | Dome sprite fits budget | **Fixed** | 144×144 4-bit ≈ 10 KB, and the 03 budget is updated. There is a new clash with smooth primitives: see required #6. |
| R12 | FreeFont flicker plan | **Fixed** | Per-region persistent 4-bit sprites, pushed on string change. Same caveat as required #6. |
| R13 | Precip % | **Fixed** | Shown only at ≥ 20 %, in glcd RAIN beside the icon. |
| R14 | Moon cutout | **Fixed** | The bite uses the container colour, and the degraded strip is clean. |
| R15 | No healthy status clutter | **Fixed** | Header right side is empty when healthy, and "Gilbert" is gone. |
| R16 | Honest pixel preview | **Fixed** | The real GLCD/Font16/GFX bitmaps plus RGB565 quantisation is exactly what I asked for. One inverse caveat: `drawArc`/`drawWideLine` are simulated *aliased*, while TFT_eSPI's are anti-aliased (matters for required #6). |

## Required changes

1. **R9 carry-over: the trail must show on every live plane screen.** The "hide if
   shaft < 12 px" rule, stacked on the 12 px start offset and 8 px head, hides the
   arrow whenever the plane moves less than 32 px on the dome in 60 s. That covers most
   planes at 2–5 mi, including the airliner hero mock. Direction on the dome matters;
   length doesn't. Fix: draw a **fixed-length direction arrow**, e.g. a 20 px shaft plus
   an 8 px head, starting 12 px from the glyph centre, along the unit vector
   (now → +60 s). Show it whenever the dome displacement is ≥ 3 px, and hide it only
   below that. This also stops close, fast planes drawing a 95 px line across the whole
   dome (stale L=95, ga-rotated L=85, and the arrowhead landing on the `N` label in my
   `Alaska` stress render). Update 06 §4.2 and 05 §5.

2. **Straight-up glyph is a blob** (`r2-plane-overhead`). The ~24 px glyph sits inside
   a 14 px "look here" ring, with the zenith dot showing through. At 1× the result is an
   amber knot with no readable plane shape or heading. Fix: ring radius ≥ 20 px (2 px);
   skip the zenith dot when the glyph is within 20 px of it (you already skip the `UP`
   label); draw the glyph after the ring.

3. **Stale glyph renders as fragments** (`r2-plane-stale`). "Refill the inner 55 %
   with PANEL" scales each part toward the *glyph* centre, not its own centroid. The
   leftover is a scatter of amber shards that looks like a render bug. Fix: draw the
   stale glyph solid in `PLANE_DIM` (the WARN "last seen" text already carries the
   meaning), or outline each part with `drawLine` around its own polygon. Also dim
   `29° up` to MUTED in stale and departing. Right now a stale fix is shown at
   full-confidence white next to a dimmed compass word.

4. **FreeSansBold9 digits touch.** Real font metrics: in `FreeSansBold9pt7b`, `8` is
   w=10, xAdvance=10, xOffset=0, and `2`/`0` are w=9, xo=1. So `8` has zero side-bearing,
   and `288` (airliner speed) renders as one merged lump at 1×. That's visible in
   `r2-plane-airliner-multi`. It will hit altitudes (`12,800`), hourly temps (`88°`)
   and weather details too. Fix: add a `drawNumber()` helper that draws fsb9 digits one
   at a time with +1 px tracking, and use it for every fsb9 numeral. Alternatively, move
   the stats values to fsb12, which has 1 px bearings on both sides, fits the 100 px
   cells, and helps legibility at 2 m.

5. **Header line fit is per-word, not per-line.** `header()` fits the operator and the
   type separately, with only 70 px reserved for the type. My `Alaska · 737 MAX 9`
   stress render came out as "Alaska" in fsb12 next to "737 MAX 9" in fsb9: two sizes on
   one line. Fix: measure the whole line (`op · type`) against the space left of the
   pill. Step the *whole line* to fsb9 if needed. If it still doesn't fit, truncate the
   **operator** first (type is priority #2). Add worst-case fixtures to
   `screens.py`: `American Eagle · ERJ-175` with `+2 more`, and
   `Alaska · 737 MAX 9` with `NOT OVERHEAD`.

6. **4-bit sprites + TFT_eSPI smooth primitives = garbage pixels.** In
   `TFT_eSprite::drawPixel`, a 4-bpp sprite uses `color & 0x0F` as the **palette
   index** (`Sprite.cpp` ~L1649). `drawWideLine`, `drawArc` (with `smoothArc = true`
   as default) and `fillSmoothCircle` alpha-blend RGB565 values. Inside the dome sprite,
   those blended edge colours become random palette indices, and the trail, rim arc
   and sector get speckled edges. Fix, and state it in 06 §4.2 and 03 §Rendering:
   - Inside 4-bit sprites, use only non-blending primitives: `fillTriangle` quads for
     thick lines, triangle fans for the sector and rim arc (or
     `drawArc(..., smoothArc=false)`, verified on device), plus `fillCircle`/`drawCircle`.
   - Pass palette *indices*, not `COL_*` values.
   - Make `tftsim` raise if a smooth primitive is called on a sprite region, the same
     way it raises on missing glyphs.
   - Note that there are 17 `COL_*` tokens, so a single 16-entry palette can't hold them
     all; give each sprite its own palette.

7. **City line has no fit rule.** `route_block()` draws `Phoenix → <city>` with no
   width check. `Phoenix → Salt Lake City` ends at x 316 (column edge 314).
   `Dallas-Fort Worth → Phoenix` measures to x 343, so it runs off the screen. Fix: run
   it through `drawFit` (f2 → truncate with dots), or use a short-city table
   (`Dallas-FW`, `Salt Lake`). Add a fixture.

8. **R8 carry-over: make the you-relative words readable.** With `VIEW_UP_DEG` set,
   `behind you` / `ahead-left` is the answer a normal person actually uses, and it's in
   9 px muted Font 2. Fix: in you-relative mode, draw it in `fsb12 TEXT` on the
   base-149 line (`behind-right` in fsb12 fits 148 px), and move `0.8 mi` to the stats
   bar or drop it. Add the `horizon` label on the rim (glcd DIM, drawn once at the
   bottom). Run the 2-second non-pilot test and write the result in the response doc.

9. **Touch gesture conflict.** Plane screen "long-press 1 s = dismiss" and "anywhere,
   hold 3 s = settings" (01 §Touch, 06 §7) collide: holding for settings on a plane card
   dismisses the plane at 1 s. Fix: pick one and write it down. Either settings-hold
   works on the weather screen only, or dismiss fires on *release* between 1 and 3 s
   and the 3 s hold wins.

## Nice to have

1. **Glyph vs rim arc at low elevation.** With clamp `R−15`, the tail and wings of a
   low plane touch the 5 px rim arc (`r2-plane-forced` at 5°, and my SE 13° stress
   render). Clamp to `R−19`, or draw a 1 px PANEL halo (glyph at 1.15× in PANEL first).
2. **Wording below 10°.** "about 1 fist" at 5° is technically true but unhelpful. For
   `el < 10°`, say `low, near the horizon`.
3. **Departing redundancy.** The `LEAVING` pill, the shrinking bar and the
   `Leaving - back to weather in 3 s` line all say the same thing, and the altitude and
   speed are lost. Keep the pill and the bar, restore the stats row, and drop the
   sentence.
4. **"Private" twice on GA.** It appears in both header line 2 (`N172SP · Private`) and
   the route block (`Private flight`). Keep the route-block one and put the owner or
   operator type in line 2 if known, else just the registration.
5. **Sim fidelity for on-screen AA.** Outside sprites (weather icons, route arrows),
   the device *will* anti-alias `drawWideLine`. The sim draws aliased, so sun rays look
   worse in the sim than on device (the top ray reads as a `$` at 1×). Either leave it
   and note it, or add a 2-level blend.
6. **Broken doc link.** `06-ui-spec.md` points to `07-design-review.md`, which doesn't
   exist. Point it at `design-review/`.
7. **Two facing values.** Setup mock uses `VIEW_UP_DEG` 195, the rotated plane mock
   uses 200. Use one value so the pair reads as the same install.

## What works
- The hierarchy is finally right. `NE` over `42° up` is the loudest thing on the
  card, `Southwest · 737-800` is second, and you can answer "where and what" in under
  2 s at 1.5 m.
- The unverified-route treatment is exemplary: dim codes plus a text tag, on the exact
  flight the spec warns about.
- The simulator raising on missing glyphs, together with geometry-computed mock data,
  is the best process fix I've seen on a hobby project. It closes whole classes of bugs.
- The `UP / overhead / lean back` state is clear and friendly.
- The forced state is honest: `NOT OVERHEAD`, a countdown, and the plane shown low on
  the rim.
- The multi-plane hollow dot and `+1 more` read instantly.
- The weather screen is clean, the stale treatment (muted hero, amber "47 min ago") is
  right, and the healthy header is quiet.
- Boot happy path and error are clear, with distinct ✓ / busy / ○ / ✕ markers. The
  Setup → Facing screen has big touch targets, suitable for resistive touch.
- The sprite/RAM plan is sized and budgeted, and it fits.
