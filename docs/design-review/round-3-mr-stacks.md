# Design Review — Round 3 (final) — Mr Stacks

Reviewed: `round-2-response.md`; all 14 `png/r3-*@3x.png` renders (and 1× crops);
`screens.py` (dome, arrow, header fit, `fit_cities`, `drawNumber`); `tftsim.py`; `06-ui-spec.md`
§4.1b–4.6, §7, §8; `03-architecture.md` §Rendering/§Memory; the new `07-design-review.md`.
I checked the sprite rectangles against the chrome drawn around them, and the arrow maths against
each scenario's geometry.

## Score: 8/10

## Verdict
This is ship-worthy. From 1.5 m, every plane state answers "where do I look, and how high?"
first and "what is it?" second. Worst-case strings now degrade cleanly. The data styling
is honest, and the RAM/sprite plan finally matches how TFT_eSPI actually behaves. One real
porting bug is left: static labels are drawn inside the dome sprite's rectangle, and every
2 s push will clip them. The simulator can't show it. It's a one-line fix. It isn't a 9
because the only glance test so far is an AI proxy; a real person hasn't looked at the
hardware yet.

## Round 2 items

| # | Item | Status | Evidence / note |
|---|---|---|---|
| 1 | Trail visible on live screens | **Fixed** | Fixed-length arrow computed from the true (unclamped) positions. It shows on airliner-multi, eagle, alaska, forced, overhead and rotated. It is still hidden when a plane near the rim heads outward (unverified, departing), but there the glyph's nose points the same way, so the direction still reads. Acceptable. |
| 2 | Overhead glyph blob | **Fixed** | r 20–21 ring, glyph on top, zenith dot and label suppressed. It reads as a plane now. |
| 3 | Stale glyph fragments + confidence styling | **Fixed** | Solid `PLANE_DIM` glyph. `29° up` / `27° up` are MUTED in stale and departing. |
| 4 | fsb9 `88` merge | **Fixed** | `288`, `12,400` and `15,800` separate cleanly with +1 px tracking. The tracking also applies to weather values and hourly temps. |
| 5 | Header fitted per line | **Fixed** | `American Eagle · ERJ-175` and `Alaska · 737 MAX 9` each step down to fsb9 as one line, and the type is never truncated. |
| 6 | 4-bit sprites vs anti-aliased drawing | **Fixed** | The dome is a 16-bit sprite (41 KB, allocated before WiFi). The text sprites are 4-bit, draw with palette indices, use only non-blending calls, and each has its own palette. Sprites total about 55 KB, so the ≥ 60 KB free-heap target still holds against the ~300 KB available. |
| 7 | City line overflow | **Fixed** | `Dallas → Phoenix` and `Salt Lake → Phoenix` both fit. |
| 8 | You-relative words readable, horizon label, glance test | **Fixed** | `behind you` is fsb12 TEXT and the `HORIZON` label is present. The glance test was a context-free AI proxy, which is a reasonable stand-in, and I appreciate that it was reported honestly. A human test on the device remains a pre-ship checklist item, not a design blocker. |
| 9 | Touch gesture collision | **Fixed** | Dismiss fires on release after a 1–3 s press. Settings fires while held at 3 s. Written in 06 §7 and 01. |
| NTH 1–7 | | **All done** | Glyph clamp R−19; `near horizon` below 10°; departing keeps its stats; `Private` shown once; 06 §8 explains simulator vs device; 07 log exists; facing = 200 in both mocks. |

## Required changes

1. **Labels drawn inside the dome sprite's rectangle get painted over on every push.** The dome
   sprite covers x 10–153, y 52–195 (144² centred on 82,124). Its corners are BG.
   Three things are drawn straight to the TFT *inside* that rectangle:
   - `HORIZON` (glcd at (4,199)): rows 192–195, x 10–45.
   - The you-relative distance `0.8 mi` (f2 at (4,58)): rows 52–60, x 10–42. This one is a
     **live value** redrawn with Font 2 padding, so the two draws will fight and flicker.
   - The `AHEAD` caret and label: rows 52–53.

   Each 2 s `pushSprite()` repaints those pixels with BG, so the labels lose their top or
   bottom rows. `tftsim` composites everything onto one canvas, so the mockups can't show
   this. Fix one of two ways:
   - (a) Push the dome with BG as the transparent key colour
     (`dome.pushSprite(10, 52, COL_BG)`). Safe, because the dome interior is PANEL and
     only the corners are BG.
   - (b) Move those labels fully outside x 10–153 × y 52–195.

   Write the rule in 06 §4.2. Add a check to `tftsim` that warns when a direct draw
   overlaps a declared sprite rectangle.

## Nice to have

1. **Glyph heading vs arrow direction for low planes.** The glyph is rotated to ground
   track; the arrow follows motion across the sky. In `r3-plane-forced` the glyph points
   E (flying toward you) while the arrow points NNE (sliding along the horizon), about 55°
   apart. When an arrow is drawn, consider rotating the glyph to the arrow's direction, so
   there's only one direction on the dome.
2. **Short arrowhead for outward movers.** When there's 14–24 px of room before the rim,
   draw just an 8 px head at 12–20 px instead of hiding the arrow. Unverified and departing
   would then say "heading for the horizon" explicitly.
3. **`back in 11 s` is ambiguous.** Back to what? `weather in 11 s` says it.
4. **`fit_cities` can loop forever.** When the text shrinks to `.`, `"."[:-2] + "."` stays
   `.` and the loop never ends. It only happens with an absurdly small width, but add a
   minimum-length stop before firmware copies it.
5. **Human glance test on hardware** before calling v1 done: two people, 2 s, "which
   window and how high". Also put the one-line "a fist at arm's length ≈ 10°" tip on first
   boot, as the proxy suggested.

## What works
- The plane card is now a textbook glance display. The amber compass word and `42° up` are
  the loudest things on the card, `operator · type` is second, the route is third, and the
  numbers come last. It matches the spec's priority list exactly.
- The honesty is designed in: `ROUTE UNVERIFIED` as text, confidence dimming for stale and
  departing, `NOT OVERHEAD` with a countdown, and `near horizon` at low elevations.
- The worst-case fixtures prove the fit rules instead of hoping they work.
- The toolchain (a real-font, RGB565 simulator that refuses missing glyphs, plus data
  computed from real geometry) is why three rounds converged. Keep it as the regression
  gate for any UI change.
- The dome reads for non-experts: `UP` at the centre, `HORIZON` on the rim, a sector plus a
  rim arc for "that slice of sky", and `behind you` in a readable size.
- The weather screen stays calm and correct: tracked numerals, precip only when it matters,
  quiet when healthy, and clearly stale when it isn't.
- The design review log in 07 records *why* each decision was made, so future "simplifications" won't
  undo them.
