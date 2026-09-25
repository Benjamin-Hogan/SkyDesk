# 06 — UI Spec

> Audience: agents implementing `firmware/ui_*.cpp`.
> **The executable source of truth is `docs/mockups/screens.py`.** It draws every
> screen with `tftsim.py`, which uses TFT_eSPI's real bitmap fonts and RGB565
> color and mirrors TFT_eSPI call names. Port coordinates from there. This doc
> explains the rules behind them. Canvas: **320 × 240 landscape**. All text `y`
> values are **baselines** (use `setTextDatum(L_BASELINE | C_BASELINE | R_BASELINE)`).
>
> Render: `python docs/mockups/screens.py final` → `docs/mockups/png/final-*.png`
> (1× honest device pixels + `@3x` nearest-neighbour). Design history and review
> scores: `07-design-review.md`.

## 1. Typography (TFT_eSPI fonts only)
| Token | Font | Cap px | Use |
|---|---|---|---|
| `glcd` | Font 1 (GLCD 5×7) | 7 | Micro labels, uppercase only: `LOOK`, `ALTITUDE`, pills |
| `f2` | Font 2 | 9 | Secondary lines. **One weight. Never bold.** |
| `fs9` / `fsb9` | FreeSans / FreeSansBold 9pt | 12 | Values, words (`Climbing`, `Route unknown`) |
| `fsb12` | FreeSansBold12pt | 17 | Header line 1, route codes |
| `fsb18` | FreeSansBold18pt | 25 | Clock, `42° up` |
| `fsb24` | FreeSansBold24pt | 33 | Hero: temperature, compass word |

**TFT_eSPI quirk (found on hardware):** with the GLCD font, `L/C/R_BASELINE`
datums treat y as the text's *top*, not its baseline. Free fonts and Font 2 do
use the baseline. `drawText()` subtracts 7 px for GLCD, so every layout can keep
y = baseline. Never call `drawString` with GLCD directly.

Legibility floor. Font 2 is ~3 arcmin at 2 m, so it is **supporting info only**.
Anything the user needs at 2 m (where to look, what it is) must be `fsb12` or larger.

### Glyphs that do NOT exist: draw them as primitives (helpers in `ui_common.cpp`)
All fonts are ASCII 0x20–0x7E. Never put these in a string:
| Glyph | Primitive | Helper |
|---|---|---|
| `°` (GFX fonts) | ring: `drawCircle` × thickness, top at cap height (r 5/4/3/2 for 24/18/12/9 pt) | `drawDegree()` |
| `°` (Font 2) | Font 2 maps **backtick** `` ` `` to a degree sign | use `` "`" `` |
| `→` | `drawWideLine` shaft + `fillTriangle` head | `drawArrowRight()` |
| `·` separator | `fillCircle(r=1..2)` | `drawSep()` |
| `▲ ▼` | `fillTriangle` 12 px | `drawTrend()` |
| `✓ ✕ ○` | two wide lines / two wide lines / `drawCircle` | `drawCheck()` etc. |
| `›` chevron | two 2 px wide lines | `drawChevron()` |
| `…` truncation | three 2×2 `fillRect` dots | in `drawFit()` |

### Text fitting (`drawFit(text, x, y, maxW, fonts[])`)
1. Measure with `textWidth()` in the preferred font; if it fits, draw.
2. Else step down once (`fsb12 → fsb9`); else truncate and draw `…` dots.
3. Airline names: strip `Airlines`, `Air Lines`, `Airways`, `Air Line`, `Inc`,
   `Co`, `Corp`, `LLC` (`Southwest Airlines` → `Southwest`).

## 2. Palette (`COL_*` in `config.h`, RGB565 via `RGB565()`)
| Token | Hex | Role |
|---|---|---|
| `COL_BG` | #0A1120 | Background |
| `COL_PANEL` | #131C2E | Header, stats bar, dome fill, hourly panel |
| `COL_PANEL2` | #1C2740 | Chips, buttons |
| `COL_HAIR` | #2A3752 | Dividers, rings |
| `COL_TEXT` | #EEF2F8 | Primary |
| `COL_MUTED` | #9AA6BE | Secondary (≥ 7:1 on BG) |
| `COL_DIM` | #7886A2 | Micro labels (≥ 4.5:1 on BG and PANEL; raised from R1) |
| `COL_PLANE` | #FFB02E | **Reserved** for the aircraft and "look here" |
| `COL_PLANE_DIM` | #8A6424 | Aircraft when departing or stale |
| `COL_PLANE_FAINT` | #3A3326 | Dome "slice of sky" sector |
| `COL_OK` / `COL_WARN` / `COL_ERR` | #3DDC84 / #FF8A3D / #FF5A5F | Status |
| `COL_SUN` / `COL_CLOUD` / `COL_RAIN` / `COL_MOON` | #FFD24A / #C9D3E3 / #4AA3FF / #E8E3C8 | Icons |

Gradients are banned (RGB565 banding). Icons cut out with the **container's**
color (e.g. the moon's bite uses `COL_PANEL` inside the hourly strip).

## 3. Weather screen (`weather()` in screens.py)
| Region | y | Content |
|---|---|---|
| Header | 0–22 | Date `f2 MUTED` left. **Nothing on the right when healthy.** Degraded: `Updated 47 min ago` `f2 WARN` + 3 px dot |
| Hero | 30–106 | Icon (1.5×) at (40,62), temp `fsb24` at x 80 base 82 (MUTED when stale), condition `fs9` base 104 |
| Clock | 30–86 | `fsb18` right-aligned to 306−(PM width), `PM` f2, next sun event `f2` base 84 |
| Details | 118–146 | 4 cells at x 10/84/170/238: label `glcd DIM` base 126, value `fsb9` base 145, units `f2` (`mph`) |
| Hourly | 154–210 | Panel; 6 columns, 51 px pitch from x 31: hour `f2`, icon 0.55×, precip `glcd RAIN` **only if ≥ 20 %** (icon shifts left 9 px), temp `fsb9` |
| Traffic chip | 215–238 | `PANEL2` pill. Plane glyph, `N nearby`, right: nearest type + distance + bearing + chevron (tappable). Offline: `Radar offline` WARN + retry countdown |

## 4. Plane screen (`plane()` in screens.py)
Answers, in order: **where to look**, then **what it is**, then **where it's going**, then numbers.

### 4.1 Header (0–43, `PANEL`, 4 px `PLANE` left bar)
- Line 1, base 24: `<Operator>` · `<short type>` in `fsb12` (e.g. `Southwest · 737-800`).
  No operator (GA): `<Manufacturer> <model>` (`Cessna 172`). Unknown type: `Unknown type`.
  The type is priority #2, so it gets the big font. The flight number does not.
- Line 2, base 39, `f2 MUTED`: `WN 2208 · N8563Z` (IATA flight, registration). GA: `N172SP · Private`.
- Right pill (glcd or f2 in a 20 px round-rect): `+1 more` (PLANE), `LEAVING` (MUTED),
  `NOT OVERHEAD` (MUTED, with the countdown `18 s` on line 2 right).

### 4.2 Sky dome (center 82,124, r 70), a 16-bit sprite
- **Pushed with `COL_BG` as the transparent key** (`pushSprite(10, 52, COL_BG)`).
  The sprite rectangle (x 10–153, y 52–195) overlaps `HORIZON`, the `AHEAD` caret,
  and the you-relative distance, all of which are drawn directly on the TFT. Only the
  sprite's corners are BG (the interior is PANEL), so the transparent key keeps those
  labels intact. **Rule:** anything drawn directly inside a sprite's rectangle
  must sit on a pixel that the sprite leaves transparent (R3-1).
- The sprite is 144 × 144 × 16-bit (≈ 41 KB), allocated **once at boot**
  before WiFi starts, so it doesn't fragment the heap. It is 16-bit rather than 4-bit because
  TFT_eSPI's anti-aliased primitives (`drawWideLine`, smooth `drawArc`,
  `fillSmoothCircle`) blend RGB565 values, and on a 4-bit sprite those values
  would be misread as palette indices. See 03 §Rendering.
- Fill `PANEL`. Rim `HAIR`. Dashed rings at 30° and 60° elevation (r × 2/3, r × 1/3).
- Projection: `r_el = R·(90−el)/90`, angle `az − VIEW_UP_DEG` (05-sky-geometry §6).
  The glyph position is clamped to **`R−19`** so its tail never touches the rim arc.
- **Look-here**: a faint `PLANE_FAINT` sector ±14° from center to rim, plus a 5 px
  `PLANE` rim arc at the same bearing. TFT_eSPI `drawArc` angles run
  clockwise from **6 o'clock**, so `arcAngle = az − VIEW_UP_DEG + 180`.
- Compass letters are `f2` inside the rim at r−11: `N` in TEXT, the others DIM. A letter is
  skipped if it falls within 16 px of the glyph.
- Zenith: a 2 px dot plus a `UP` glcd label. **Both are hidden** when the glyph is within 20 px.
- `HORIZON` glcd label in DIM at (4, 199), just outside the rim at the lower left.
- **Straight up (el ≥ 75°)**: no sector and no rim arc, because azimuth is meaningless near
  the zenith. Instead draw a 2 px `PLANE` ring of **r 20–21** at the center, then the glyph
  on top of it.
- Aircraft glyph: 6 convex parts (`PLANE_PARTS`), each drawn with `fillTriangle`s,
  rotated to `track − VIEW_UP_DEG`, about 24 px across. **Stale and departing
  planes draw the solid glyph in `PLANE_DIM`**, never a hollow one.
- **Direction arrow**: fixed length, because direction matters and length doesn't.
  - Direction comes from the **true (unclamped)** dome positions, now → +60 s
    (05-sky-geometry §5).
  - Geometry: a 12 px gap from the glyph center, a 2 px `drawWideLine` shaft, and an
    8 px `fillTriangle` head. The tip sits at 40 px, or at the rim − 3 px if that is closer.
  - With 14–24 px of room before the rim, draw only the 8 px head. With < 14 px, or if the plane
    moves < 3 px on the dome in 60 s, draw no arrow. Hidden when stale.
  - Whenever an arrow is drawn, the **glyph is rotated to the same on-dome direction**, so
    they never disagree. Otherwise the glyph uses `track − VIEW_UP_DEG`.
- Other qualifying aircraft: a 2 px hollow circle of r 4, `MUTED`.
- You-relative mode (`VIEW_UP_DEG ≠ 0`): a white caret at the rim top plus an
  `AHEAD` glcd label, with the letters rotated. Distance moves to the top-left corner, (4, 58) in `f2`.

### 4.3 LOOK block (x 166–314)
| y (base) | Content |
|---|---|
| 60 | `LOOK` glcd DIM |
| 97 | 8-point compass word in `fsb24 PLANE` (e.g. `NE`). **No azimuth degrees anywhere.** |
| 130 | `42° up` in `fsb18`: the number, ring and word *up* share one baseline, so it can't be read as a bearing |
| 149 | `about 4 fists · 2.4 mi` in `f2 MUTED`. Below 10°: `near horizon · 6.7 mi` |
| 157 | 1 px `HAIR` divider |

- **You-relative** (`VIEW_UP_DEG ≠ 0`): the lines tighten to 58 / 93 / 123, and line 4
  (base 150) is the relative phrase (`behind you`, `ahead-left`…) in **`fsb12` TEXT**.
  This is the answer a normal person actually uses, so it gets a readable size.
- Straight up: `UP` (fsb24) / `overhead` (fsb12) / `lean back · 0.4 mi`.
- **Confidence styling**: in the stale and departing states the compass word is
  `PLANE_DIM` and `42° up` is `MUTED`, never full-confidence white.
- Stale: line 149 becomes `last seen 8 s ago` in WARN.

### 4.4 Route block (x 166, base 181 / 197)
| Case | Line 1 (`fsb12` unless noted) | Line 2 |
|---|---|---|
| Plausible | `PHX → DEN` TEXT (drawn arrow) | `Phoenix → Denver` f2 MUTED, fitted |
| **Unverified** | `PIT → TPA` in **DIM** | `ROUTE UNVERIFIED` glcd in a WARN outline tag (text and color, not color alone) |
| Airline, no route | `Route unknown` fs9 MUTED | `no schedule found` f2 DIM |
| GA / private | `Private flight` fs9 MUTED | `no route filed` f2 DIM |
| Lookup pending | `Looking up route` fs9 MUTED | — |

City-line fit (`fitCities`, 129 px available):
1. If both names fit, draw them as-is.
2. Otherwise look them up in the short-city table (`Dallas-Fort Worth→Dallas`,
   `Salt Lake City→Salt Lake`, `San Francisco→San Fran`, `Minneapolis→Mpls`…).
3. If they still don't fit, trim the longer name and end it with `.`.

### 4.5 Stats bar (202–239, `PANEL`)
- `ALTITUDE` (x 10): value in `fsb9` + `ft`. `SPEED` (x 112): value + `mph`.
- **All `fsb9` numbers go through `drawNumber()`**, which adds +1 px between glyphs.
  FreeSansBold9's `8` has zero side-bearing, so `288` would otherwise merge into
  one blob. This also applies to weather values and hourly temps.
- Trend (x 196): a 12 px ▲/▼ triangle or a level bar, plus `Climbing` / `Descending` /
  `Level` in `fs9` (±300 fpm threshold). **There is no heading cell.** Only one compass
  word appears on the screen.
- Departing: a `PLANE_DIM` bar along the top edge shrinks over the grace period.
  **The stats stay visible.** The `LEAVING` pill and the bar together say it; there is no third "leaving".
- Forced (not overhead): a MUTED 3 px countdown bar along the top edge, plus
  `weather in 18 s` right-aligned on header line 2.

### 4.1b Header fitting (supersedes the per-word rule)
Fit `<op> · <type>` as **one line**:
1. Try the whole line in fsb12.
2. If that doesn't fit, try the whole line in fsb9.
3. If it still doesn't fit, truncate **the operator** (never the type) with `…`.

Worst cases are mocked: `plane-worst-eagle` (`American Eagle · ERJ-175` + `+2 more`)
and `plane-worst-alaska` (`Alaska · 737 MAX 9` + `NOT OVERHEAD`).
GA line 2 shows only the registration. `Private` appears once, in the route block.

### 4.6 State matrix (every state has a mockup)
| State | Mockup |
|---|---|
| Live, multiple planes | `plane-airliner-multi` |
| Route unverified | `plane-unverified` |
| Straight up + route unknown | `plane-overhead` |
| Departing grace | `plane-departing` |
| Force-shown (chip tap) | `plane-forced` |
| Stale position | `plane-stale` |
| You-relative dome, GA | `plane-ga-rotated` |
| Long operator + 3 planes + long city | `plane-worst-eagle` |
| Low plane, forced, long type | `plane-worst-alaska` |

## 5. Boot, error, setup
- Boot: wordmark + checklist panel. Rows are ✓ (OK green), animated 3-dot (busy,
  PLANE), ○ (todo, DIM), ✕ (fail, ERR) + an actionable retry line. Hints below.
- Setup → Facing: the owner rotates the dial with −15/+15 buttons until `YOU FACE`
  matches reality (a phone compass helps), then presses Done. The value is saved to
  NVS as `VIEW_UP_DEG`. 0 means north-up (the default).

## 6. Motion, light and arrival cue
- Weather → Plane: 8-band top-down wipe (≈150 ms). Then the header's left bar
  flashes PLANE/PANEL twice (300 ms), and the RGB LED pulses amber twice.
- Plane → Weather: the same wipe bottom-up, with no flash.
- The dome sprite redraws on each ADS-B update (2 s). There is no tweening in
  v1: a 24 px glyph moving ≤ 6 px per update reads as smooth enough.
- Backlight (LEDC PWM on GPIO 21): 100 % in daytime, and 35 % from sunset+30 min
  to sunrise−30 min (from weather data). If `LDR_AUTO`, the LDR caps it further.
  A plane arrival always forces 100 % for the duration of the card.

## 7. Touch
| Where | Gesture | Action |
|---|---|---|
| Plane | tap (< 0.6 s) | Next qualifying plane (only if `+N more`) |
| Plane | press 1–3 s, **then release** | Dismiss this plane (until it exits) |
| Weather | tap traffic chip | Force-show the nearest plane for 20 s (`NOT OVERHEAD`) |
| Anywhere | hold ≥ 3 s | Settings: Facing direction · Dim at night · Calibrate touch |
| Settings | hold ≥ 3 s again | Straight into touch calibration (works even if touch mapping is off) |

**Precedence rule.** Dismiss fires on **release**, and only if the press lasted
1–3 s. Settings fires **while held**, at 3 s. Once settings has fired, the release does
nothing. So holding for settings can never dismiss a plane first.

## 8. Simulator vs device
`tftsim.py` draws `drawWideLine` / `drawArc` **without** anti-aliasing. The device
anti-aliases them (on the 16-bit dome sprite and on the direct TFT), so sun rays and the
direction arrow will look smoother on hardware than in the mockups. The mockups
err on the ugly side, so they are a conservative preview.
