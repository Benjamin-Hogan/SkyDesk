# Design Review — v2 Plane Map — Round 1 — Mr Stacks

Reviewed: `08-plane-map.md`; `mockups/map_screen.py`; `tools/basemap/make_basemap.py`; the four
`v2-map-*@3x.png` renders; `06-ui-spec.md` §1 (the GLCD baseline quirk); `include/config.h` /
`app_state.h` (`MAX_AIRCRAFT 40`, `min_spiffs.csv` partitions); and the 25 nm fixture. I
measured the basemap coverage and checked the map palette in RGB565.

**Reading-distance note.** The map is a *lean-in* screen: you only get there by touching the
device, so the user is at arm's length. I'm not holding it to v1's 2 m rule. The floor
here is "readable at 50–70 cm", so glcd and f2 are acceptable.

## Score: 6/10

## Verdict
Good bones and a smart engineering call. Pre-rendered 4-bit basemaps in flash, a single
7.7 KB band sprite, and real traffic in the mocks make this the right architecture for a
43 KB-largest-block heap. The overhead-zone disc with a cyan "you" is an instantly
readable anchor. It isn't ready yet:
- You can't reliably tap a 15 px plane with a fingertip on resistive touch.
- The info strip isn't visually linked to its plane.
- The busy 20 nm case smears into a white bar.
- The range ring is invisible, and it shares an RGB565 colour with trunk roads.
- Offline freezes stale planes on the map indefinitely.
- The map says `NM` while the rest of the product says `mi`.
- The `‹` chip repeats the anti-aliasing-in-a-4-bit-sprite bug from v1 round 2.

## Required changes

1. **Tap-to-select has to work with a fingertip.** A fingertip covers about 10 mm, which
   is about 56 px on this panel. Glyphs are about 15 px, and at 10–20 nm several planes sit
   within 20 px of each other (`v2-map-z1` top-left, `v2-map-z2` west cluster). "Tap an
   aircraft → card" will open the wrong plane or nothing at all. Fix, and write it in 08
   §Getting there:
   - **Tap on the map = select** the airborne aircraft nearest the touch point, within
     28 px. Otherwise keep the current selection. The selection updates the info strip
     and the highlight (see #2).
   - **Tap the info strip = open that plane's card** (forced). The strip is a 320×26
     target, which is easy to hit.
   - Selection sticks until that plane leaves the map, or 120 s idle.

   This also gives the `selected` state in `v2-map-z0-overhead` a way to happen. Right
   now the navigation table has no path that produces it.

2. **Amber must mark the plane in the info strip.** In `v2-map-z1` the strip describes
   `FALCON7`, but FALCON7 is drawn white like the other 12 planes, so the user has to hunt
   for the callsign. In v1, amber meant "the plane that matters". On the map, that's the
   strip plane (selected, else nearest). Make that plane and its tag `PLANE` amber, with
   the r 11 ring; draw everything else in TEXT. Handle qualifying planes explicitly: while
   the map is up, a qualifying plane pops the v1 card ("Anywhere" rule), so an amber
   "qualifies" state on the map is only visible for one poll, or after a dismiss. Write that
   down. After a dismiss, draw the dismissed plane `PLANE_DIM`.

3. **The busy case needs overlap handling.** In `v2-map-z2`, five westbound PHX arrivals
   render as one white bar with no tags, and the PHX label is squeezed out by glyph
   reservations. This is the *normal* evening picture under Phoenix traffic, not an edge
   case. Fix:
   - At the 20-unit zoom, draw glyph scale 0.45 (or a 3 px dot plus a 5 px heading tick).
   - Merge aircraft whose glyph centres are within 10 px into one glyph with a glcd count
     (`5`). Tapping the group selects its nearest member.
   - Reserve airport labels (at least PHX) *before* aircraft tags. They're the
     landmarks that make the map readable.

   Mock a synthetic 30-airborne fixture to prove it.

4. **Rings are invisible, and one palette slot is wasted.** Palette index 2 (trunk,
   `#2A3750`) and index 6 (rings, `#2A3752`) quantise to the **same RGB565 value**
   (5,13,10). The 4.5 nm exit ring and the range ring disappear into trunk roads, and the
   1-on-4 dashed range ring is barely visible even on bare BG (`v2-map-z1`). The zoom chip
   "names the ring", but you can't see the ring. Fix: drop index 6 and draw rings in
   `M_DIM` (`#7886A2`) as 1-on-3 dots. Use the freed slot for something useful, e.g. a
   `PLANE_DIM`-style tint for dismissed or predicted planes, or ERR.

5. **Units: pick miles, like the rest of SkyDesk.** v1 uses `mi` and `mph` everywhere, and
   the map's own info strip says `5.0 mi NW`, but the zoom chip says `10 NM`. A normal
   person reads "NM" as New Mexico. Define the zooms as **5 / 10 / 20 mi** (px per statute
   mile in `make_basemap.py` and the projection), label the chip `10 MI`, and keep nm
   internal only.

6. **Altitude tags must be plain language.** `072` / `124` (hundreds of feet) is
   pilot shorthand, and v1 was built on "plain words, not codes". Use `7.2k` / `12k`
   (≥ 10,000 ft: round to whole k; below: one decimal). It's the same width in glcd, and
   anyone can read it as thousands of feet.

7. **Offline must not freeze a live-looking map.** `v2-map-offline` keeps 13 dimmed planes
   at their last positions, with tags, for as long as the radar is down. After ten minutes
   that's fiction. Fix: show the last positions DIM for at most `LOST_TIMEOUT_S`/60 s
   (the same as the trail drop), then clear the aircraft and show `No live traffic` centred
   in muted text. Keep the strip's `Radar offline · retrying in 20 s`. Mock both steps.

8. **The `‹` chevron uses `drawWideLine` inside the 4-bit band sprite.** That's the exact
   bug fixed in v1 round 2: TFT_eSPI's `drawWideLine` alpha-blends RGB565, and a 4-bit
   sprite treats `color & 0x0F` as a palette index, so the back chip will get speckled
   edges. Draw the chevron with two `fillTriangle` quads. Add the rule "band sprite = 4-bit
   → no `drawWideLine`/smooth arcs/`fillSmooth*`" to 08 §Rendering, and make `tftsim` raise
   on it (my round-3 nice-to-have, now load-bearing).

9. **Mock the missing states and define their behaviour:**
   - (a) **Empty sky** (`No aircraft within 10 mi`).
   - (b) **First frame after entering the map.** The snapshot is still from the v1 12 nm
     poll, so at the 20-unit zoom planes pop in on the next poll. Show a `widening radar…`
     strip, or pre-fetch before switching.
   - (c) **Card pops over the map and returns.** Does the idle timer reset? It should.
   - (d) **Per-aircraft stale** (DIM glyph when `seen_pos` > 15 s) next to live ones.

10. **Traffic cap and eviction.** `MAX_AIRCRAFT` is 40, and the trail table is 40 × 8.
    The fixture had only 13 airborne at 25 nm, but a PHX evening push at a 27 nm radius
    can exceed 40 *including ground*. Specify that ground aircraft are filtered **before**
    truncation, that truncation keeps the nearest by ground distance, and that the
    selected plane is never evicted. Otherwise the map silently drops the nearest planes
    in whatever order the API returns them.

## Nice to have

1. **Basemap coverage.** The fetch radius is 25 nm, but the 20-unit zoom spans ±32 nm
   horizontally, so the basemap is blank at x < 31 and x > 295 (measured). Polling is
   capped at 27 nm, so the corners are dead too. Fetch the basemap to 40 nm (build-time
   only, no flash change), and either poll to the visible extent or draw one faint arc at
   the data radius so an empty edge doesn't read as "no planes".
2. **Compute the layout once per frame, then replay it into each band.** Declutter,
   label placement and tag rects must be computed once, not per band. Otherwise a tag can
   be placed differently in bands 1 and 2 and get sliced at the seam. Say so in 08.
3. **Tags in MUTED, strip plane in amber.** Thirteen white glyphs plus thirteen white tags
   is a lot of equal-weight noise. Make the other tags `M_MUTED` so the amber strip plane
   and the white glyphs lead.
4. **Info strip in v1 language.** The strip has room for `Southwest 2208 · 737-800`
   (about 150 px f2) instead of `SWA2208 · 737-800`. Reuse v1's operator lookup for the
   strip; ICAO callsigns stay on the map tags. Raw ICAO types like `H500` / `PA44` should
   go through the type table (`Hughes 500`, `Piper Seminole`) or fall back to the category
   word (`Helicopter`).
5. **Leader ticks for flipped tags.** Above/below tag placements (`N214WT`, `SWA1296` in
   `z1`) are ambiguous between two nearby planes. Either allow only right/left placement,
   or draw a 3 px `M_DIM` tick from tag to glyph.
6. **Predicted entries.** Tint a plane `PLANE_DIM` if its 60 s projection (v1 geo) enters
   the 3 nm disc. That answers "is anything coming my way?", the main reason to glance at
   this map.
7. **Hit area for chrome.** The `‹` and zoom pills are 36×24 and about 50×24. Pad their
   *touch* rects to ≥ 44×36 (invisible) for resistive touch. Also remember the last zoom.
8. **OSM attribution** is shown only at the widest zoom. Show `(c) OSM` at every zoom
   (18 px in glcd at the bottom-left), which is cheap and respects ODbL's spirit.
9. **Reserve the N marker** (155–165, 3–24) in the declutter `taken` list, and keep the
   selection ring clear of its own tag (`SWA2208` overlaps the ring in `z0`).

## What works
- **The architecture is right for the measured heap.** Pre-rendered 4-bit basemaps
  (3 × 38.4 KB flash on a `min_spiffs` 1.9 MB slot, no problem), `memcpy_P` into one
  7.7 KB band sprite, and five pushes per frame give no full-screen buffer and no flicker.
  Well reasoned from real numbers.
- **The overhead zone on the map** is the killer feature. You can watch a plane approach
  the brown disc and know the card is about to pop. It connects v2 to v1's core promise.
- **Filtering out ground traffic** before drawing is exactly right (23 of 36 were on the
  ground).
- **Labels drawn on-device from lat/lon tables** instead of baked into the bitmap stay crisp
  and can be decluttered. Runways plus airport codes give instant orientation.
- **Real traffic in the mocks** exposed the real problems (clusters, odd callsigns like
  `0XF9610`). Keep doing this.
- **The navigation model is simple and has a home.** Chip → map, `‹` back, 120 s idle →
  weather, and the card returns you to the screen you came from.
- The palette discipline mostly holds: a quiet navy basemap, cyan used only for "you", and
  amber reserved (once #2 lands) for the plane that matters.
