# Design Review — Round 1 — Mr Stacks

Reviewed: `mockups/r1-*.html`, `mock.css`, all `png/*@3x.png`, and `r1-plane-airliner_px.png`,
`r1-plane-ga_px.png`, `r1-weather_px.png`. Checked against `01-product-spec.md`,
`06-ui-spec.md`, `05-sky-geometry.md` §6, and the RAM/font constraints in
`02-hardware.md` / `03-architecture.md`.

Reference math used below: 2.8" 320×240 means about 0.178 mm per pixel. At 2 m, a
17 px cap height (fsb12) is about 5 arcmin, which is the 20/20 limit. A 10 px cap
(Font 2) is about 3 arcmin, so **Font 2 cannot be read at 2 m** and is marginal at 1 m.
Only fsb18 and fsb24 are comfortably readable at the far end of the spec's range.

## Score: 6/10

## Verdict
The bones are good. The plane card puts "where to look" first, the dome geometry
matches §6 exactly (I checked the plane positions against `r = 72·(90−el)/90`), and
the palette and weather screen are clean and calm. It is not ship-worthy yet. The
aircraft type, which is priority #2, is set in the smallest font on the card. "NE 42°"
reads as a compass bearing. Several glyphs and the bold Font 2 don't exist in
TFT_eSPI. The mockup set also skips the headline honesty case ("route unverified",
shown on the very SWA1637 flight the spec calls out) along with most of the plane-screen
edge states.

## Required changes

1. **R1: Make the elevation number impossible to misread as a bearing.** `NE 42°`
   side by side reads as "north-east, 042°" to anyone who has used a compass app, and
   the word that disambiguates it ("up") is in small muted Font 2 on the next line.
   This is the #1 job on the screen, so it can't be ambiguous. Fix: put "up" in the
   same visual unit as the number, e.g. `42° up` in fsb18 with "up" in fsb12 on the
   same baseline, or add a drawn up-arrow glyph before the number. Keep
   "about 4 fists" as the secondary line. Also settle the spec conflict:
   `01-product-spec.md` §"What the Plane screen must answer" asks for
   "8-point + degrees" for the compass. Either drop azimuth degrees from the spec
   (my preference, since they're noise for normal people and would add a *second*
   degree number) or put them only in the dome at glcd size. Don't place them next to
   the elevation.

2. **R2: Promote aircraft type to priority #2 as the spec requires.** Right now
   `Boeing 737-700` / `Cessna 172 Skyhawk` is Font 2 at y=186. That makes it the
   *least* readable text on the card, below the route (fsb12) and the flight number
   (fsb12 header), and unreadable at 2 m. Fix: make the header
   `Southwest · 737-700` (operator + short type, fsb12). Move the flight number
   (`1637`) or registration down into the muted secondary line, or into the stats bar.
   For GA, the header becomes `Cessna 172` with `N172SP · Private` below it. Add
   type-name shortening rules to `06-ui-spec.md` §Text-fitting (e.g.
   `Boeing 737-7H4` → `Boeing 737-700`, drop `Skyhawk` if it doesn't fit), plus a
   fallback when the type is unknown (show the ICAO type code, then "Unknown type").

3. **R3: Mock the "route unverified" state, and the airliner "route unknown" state.**
   This is the product's stated honesty rule (`01-product-spec.md` §Route honesty),
   and it has no mockup. The airliner mock even uses SWA1637, the flight the spec cites
   as a known-bad route, and shows it as fully trusted. Add `r1-plane-unverified`:
   route in `COL_DIM`, with an explicit tag such as `route?` or `unverified` in fsb9 or
   Font 2 next to it. Don't use colour alone, because dim versus muted is hard to tell
   apart at arm's length. Also add the lookup-failed variant (`route unknown`) for an
   airliner, which is different from GA "Private".

4. **R4: Remove the second compass word from the stats bar.** `HEADING NW`, set right
   under `LOOK NE`, puts two compass directions on one card. A non-pilot will
   eventually look NW. The dome trail already shows direction of travel, and it does
   so in sky terms, which is the frame the user needs. Fix: replace the HEADING cell
   with vertical trend in words (`Climbing` / `Descending` / `Level`). That also solves
   R5's problem. If you must keep direction of travel as text, phrase it as
   "moving toward NW" and keep it muted, never in amber.

5. **R5: Make climb/descend visible and add units.** `ALT ▲` is a 7 px `COL_DIM`
   glyph on `COL_PANEL` (contrast ≈ 3:1), so nobody will see it past 50 cm. Altitude
   has no unit (`12,400` of what?), and neither does weather wind (`NW 7`). Fix: write
   `12,400 ft`. Show trend as a word (see R4), or as a drawn 2 px arrow in `COL_TEXT`
   beside the value, not inside the label. Change wind to `NW 7 mph`.

6. **R6: Use only glyphs that TFT_eSPI actually has.** FreeFonts (`*7b`) and Font 2
   cover printable ASCII only (0x20–0x7E). The mockups use characters that will render
   as blanks or garbage on the device:
   - `→` in `PHX → DEN` (fsb12) and `Phoenix → Denver` (f2)
   - `·` in `up · about 4 fists`, `Weather · Radar`
   - `✕` and `○` on the boot screen
   - `°` in Font 2 (hourly temps). The ring rule in `06-ui-spec.md` only covers FreeFonts.

   Fix: specify each one as a drawn primitive in `06-ui-spec.md`, with pixel sizes:
   arrow = line plus filled triangle, dot = 2×2 fillRect, ✕ = two 2 px lines, ○ =
   drawCircle, degree ring for Font 2 = 1 px ring of radius 2. Mockups must draw them
   as SVG shapes, not text, so the mockup can't hide a missing glyph. (`▲▼` in GLCD
   Font 1 are fine, since they're CP437 0x1E/0x1F, but R5 removes them anyway.)

7. **R7: Stop using bold Font 2.** Font 2 has one weight. The hourly temperatures
   (`font-weight="700"` inside `.f2`), the dome `N`, and the boot `✕` are rendered
   bold in the mockups. On the device they will come out thinner and less prominent
   than what you're approving. Fix: hourly temps → `fsb9` (a 51 px column fits
   "100°" in FreeSansBold9pt). Dome `N` → Font 2 regular in `COL_PLANE` or
   `COL_TEXT`, or fsb9. Add a lint rule to `mock.css` so `.f2` can't take
   `font-weight`.

8. **R8: Explain the dome to a normal person.** A polar sky plot is an
   astronomer's convention. Nothing on the card says "centre = straight overhead,
   edge = horizon", and a north-up dome on a desk means the user has to know where NE
   is in their kitchen. Fix, all three:
   - (a) Label the centre dot `UP` and the outer ring `horizon`, glcd, `COL_DIM`,
     drawn once.
   - (b) Put a `you face` marker (small triangle on the rim) at `VIEW_UP_DEG`. Mock one
     variant with `VIEW_UP_DEG ≠ 0` so the rotated N label and a "top = in front of
     you" reading are designed, not left to firmware.
   - (c) Add the compass-calibration step (set `VIEW_UP_DEG`) to the setup
     instructions and the boot screen hint.

   Test the result on someone who isn't a pilot: show them the airliner card for 2 s,
   then ask "which window and how high?"

9. **R9: Beef up the 30 s trail.** It is a 1.5 px dashed line with a 5 px
   arrowhead. At device density it turns into three amber dots, and the heading can't
   be seen at 1 m. Fix: solid 2 px line in `COL_PLANE`, a filled arrowhead ≥ 7 px,
   and at least 12 px of shaft between the glyph and the arrowhead. If the projected
   +30 s point is under 6 px away (slow GA, or a plane near zenith), extend it to
   +60 s or hide the trail. Don't draw a stub.

10. **R10: Design the missing plane-screen states.** Each of these needs a mockup,
    because each changes layout or honesty:
    - (a) **Straight up** (`el ≥ 75°`): the spec says to show "straight up". What
      replaces `NE` in fsb24? The compass word is meaningless there. Suggest `UP` or
      `OVERHEAD` in the hero slot, with the plane glyph at the dome centre.
    - (b) **Departing grace** (4 s): the spec says "arrow/ghosted". Show it, e.g. the
      glyph drawn as an outline, the header in `COL_MUTED`, and a `leaving` tag.
    - (c) **Multiple planes**: `+1` alone doesn't say what it is or that it's
      tappable. Draw the other qualifying planes on the dome as small hollow
      `COL_MUTED` dots, and change the pill to `+1 more`.
    - (d) **Force-shown via chip tap** (not overhead, possibly 8° up and 5 mi out):
      the card must say it's a preview (`nearest, not overhead`) and show the 20 s
      countdown. Otherwise the user stares at the sky for a plane on the horizon.
    - (e) **Stale position on the plane screen** (ADS-B hiccup longer than one poll,
      under `LOST_TIMEOUT_S`): dim the glyph and show `last seen 8 s ago`. The weather
      screen has a stale treatment, so the plane screen needs one too.

11. **R11: Fix the dome sprite size to match the RAM budget.** The mockup dome is
    r = 72 plus a 5 px rim arc, which needs a sprite of about 150×150. As 16-bit that's
    about 44 KB, against the ≤ 124×124 / 30 KB budget in `03-architecture.md`
    §Rendering. Pick one and document it: shrink the dome to r ≤ 60 (you'd lose
    legibility, so I don't recommend it), or specify a **4-bit palettised
    `TFT_eSprite`** (≈ 11 KB, and it keeps the exact RGB565 palette, unlike 8-bit
    RGB332, which will shift `COL_PANEL` and make the dome fill mismatch the
    surroundings). Update `03-architecture.md` so the doc and the mockup agree.

12. **R12: Fix the flicker plan for FreeFont numbers.** `03-architecture.md` says
    changing numbers use `setTextColor(fg,bg)` + `setTextPadding` "without a fillRect
    flash". That's true for Fonts 2/4, but TFT_eSPI FreeFonts have no background
    rendering; the padding path clears with a rectangle and then draws. The 34 px `NE`
    / 25 px `42°` block and the fsb9 stats will blink every 2 s poll whenever the value
    changes. Fix: draw the LOOK block (≈ 146×64) and each stats value into small 4-bit
    sprites and push them. Redraw only when the *displayed string* changes, not on
    every poll. Specify this in `06-ui-spec.md`.

13. **R13: Hourly strip is missing precip %.** `01-product-spec.md` §Weather says
    "hour, icon, temp, precip %". The 8 PM rain icon has no number. Fix: add
    `precip %` in Font 2 `COL_RAIN` under the temp. Show it only when ≥ 20 %, to keep
    the strip quiet in Arizona, and document the threshold. The strip is 56 px tall and
    needs about 12 px more. Take it from the gap above the detail cells.

14. **R14: Moon icon cutout bug.** `#moon` makes its crescent by overlaying a
    `var(--bg)` circle. On the hourly strip (`COL_PANEL` background), that cutout
    shows up as a dark disc next to every moon; you can see it in
    `r1-weather-degraded@3x.png`. On device it's the same bug. Fix: draw the crescent
    with the local background colour as a parameter (`drawMoon(x,y,r,bg)`), or as a
    real crescent shape.

15. **R15: Healthy status should be invisible.** The spec says the status indicator
    is "hidden when healthy — no permanent clutter". The weather screen shows a
    permanent green dot next to `Gilbert`. Fix: remove the green dot. Show the amber or
    red dot only on problems. Consider dropping `Gilbert` too, since you know where
    you live, and freeing that slot for the stale message.

16. **R16: Make the pixel preview honest.** `render.py`'s `_px.png` renders at 1× in
    Chrome with anti-aliased Liberation Sans/Consolas, then upscales it. That is
    *more* flattering than the device, where FreeFonts and Font 2 are 1-bit and Font 2
    is a completely different pixel face. That's how R6 and R7 got through. Fix: for
    round 2, render text in the `_px` preview from the real bitmaps (parse the
    Adafruit GFX `FreeSans*7b.h` tables and TFT_eSPI `Font16`/`glcdfont` in Python), or
    at minimum set `-webkit-font-smoothing:none; text-rendering:optimizeSpeed` and label
    the preview "approximate". I will judge round 2 legibility on the `_px` files.

## Nice to have

1. **Make mock data self-consistent.** GA card: 2,800 ft MSL at 0.6 mi from a
   1,240 ft observer gives el ≈ 26°, not 31°. Airliner: 12,400 ft at 2.1 mi gives
   ≈ 45°, not 42°. Mockups become test fixtures, so make them agree with
   `05-sky-geometry.md`.
2. **Use the rim arc as a sector wedge.** The amber rim arc is the best "look this way"
   cue on the card, but it floats. A faint filled sector (2–3 flat `COL_PANEL2`-ish
   steps are fine in RGB565) from centre to rim through the plane would read as "that
   slice of sky" to anyone.
3. **Add an arrival cue.** Plane screens appear unprompted. The 150 ms wipe plus the
   RGB LED pulse from `02-hardware.md` should be mocked or specified (e.g. an amber
   header band that fades to `COL_PANEL` over 1 s in 3 steps), so the switch doesn't
   look like a glitch.
4. **Night palette / brightness.** At 9 PM, a `#EEF2F8` 47 px temperature on a dark
   desk is a flashlight. Specify LDR/PWM brightness curves and maybe a night text
   colour (`COL_MUTED` for hero numbers after sunset).
5. **Show that the traffic chip is tappable.** It isn't obvious. A tiny `›` (drawn)
   at the right end would help, or show the nearest aircraft's type
   (`closest: 737, 5.2 mi W`), which makes the tap more tempting.
6. **Dim stale weather values.** At `Updated 47m ago`, render the hero temperature in
   `COL_MUTED` so the staleness is felt, not just read.
7. **Boot happy path.** Only the error boot screen exists. Mock the normal
   "Connecting → Clock ✓ → Weather ✓ → Radar ✓" progress, and make pending, done and
   failed visibly different (hollow, filled amber, red), not three near-identical dim
   circles.
8. **Viewing angle check.** The desk sits below eye level. Verify on your CYD2USB
   unit that `COL_BG`/`COL_PANEL`/`COL_PANEL2` (RGB565 steps of only 1–3 levels per
   channel) remain distinguishable at 30–45° off-axis. If they don't, lean on hairline
   borders instead of fill contrast.
9. **Raise `COL_DIM` for glcd labels.** Glcd labels (`FEELS`, `ALT`, `LOOK`) at
   `COL_DIM` on `COL_PANEL` are ≈ 3:1. Raise `COL_DIM` to about `#6E7C99` for text use,
   or use `COL_MUTED` for 7 px labels.

## What works
- The #1 job gets the most space: dome plus a 34 px amber compass word. You can find
  "NE" from across the room.
- Dome projection is correct per §6: equidistant rings, N up, plane positions match
  their elevation.
- "about 4 fists" is excellent plain-language design for non-astronomers.
- Amber is reserved strictly for aircraft and "look here". The semantic colour
  discipline is clean.
- Weather screen hierarchy is right: temp, then clock, then details, then strip, then
  chip. It's calm and legible, and flat fills suit RGB565.
- The degraded weather screen handles staleness the right way: specific ("47m ago",
  "retrying in 30s"), in place, with no modal.
- The boot error is actionable: SSID named, retry countdown, 2.4 GHz hint.
- The GA card handles "no route" honestly instead of inventing one.
