# 08 — Plane Map (v2 feature 1)

> Audience: agents building the map screen. Status: **approved (Mr Stacks 8/10, round 3); implemented in `firmware/ui_map.cpp`**
> (`design-review/v2-map-*`). Behaviour builds on `01-product-spec.md`. Layout rules
> follow `06-ui-spec.md`. Mockups: `map_screen()` in `docs/mockups/map_screen.py`,
> rendered via `python docs/mockups/screens.py <round> map`.

## What it is
A top-down, north-up map with **you in the middle** and every airborne aircraft at its
real position (pointing where it flies, with a short trail). A **nearby disc**
(3 nm = 3.5 mi) is drawn around you. Dim amber marks planes that *will* pop the card
within a minute, so you can watch one coming.

**What the disc is, and isn't** (review v2-R2-1). The card pops at **≤ 3 nm AND ≥ 25°
up** (v1 ENTER rule). For typical Gilbert traffic that is well inside the disc: a plane
2,000 ft above you triggers within ~0.7 nm, 5,000 ft within ~1.8 nm, and 8,000 ft within
~2.8 nm. So the disc means "within 3 nm of you", **not** "will pop". The "will pop" signal
is the dim-amber prediction below, and the strip shows the elevation of a focused plane
inside the disc, so you can see why it hasn't popped.

It is a **lean-in** screen: you only get there by touching the device, so the
legibility floor is arm's length (50–70 cm), not v1's 2 m glance distance. That makes
glcd and f2 text acceptable here.

## Navigation
| From | Gesture | Result |
|---|---|---|
| Weather | tap the traffic chip (`4 nearby ›`) | Map. This replaces v1's "force-show nearest" tap |
| Map | tap `‹` (touch area 48 × 36, top-left) | Weather |
| Map | tap the zoom chip (touch area 74 × 36, top-right) | Cycle 5 → 10 → 20 mi. The last zoom is saved in NVS |
| Map | **tap anywhere on the map** | **Select** the airborne plane nearest the touch point, **within 28 px** (a fingertip is ~56 px on this resistive panel). The info strip and highlight follow it. A tap with nothing within 28 px clears the selection |
| Map | **tap the info strip** (320 × 26) | Open the strip plane's card (forced, `NOT OVERHEAD`), then return to the map |
| Map | no touch for `MAP_IDLE_S` (120 s) | Weather. The selection is cleared |
| Anywhere | a plane enters the overhead zone | The v1 plane card pops **over the map**. When it's done, you return to the screen you were on. **The map idle timer restarts** when you come back |

The selection sticks until that plane leaves the map's traffic, or the idle timeout clears it.

## Data
- The same ADS-B snapshot as v1. While the map is up, the poll radius becomes
  `ceil(zoom_radius_nm × MAP_POLL_SCALE)`, where `MAP_POLL_SCALE` = 2.0, so the corners
  (198 px from the centre) are covered. It is capped at `MAP_POLL_MAX_NM` = 35 nm. The cadence is 5 s,
  or 3 s at the 5 mi zoom.
- **Aircraft on the ground are dropped in `adsbParse`, before truncation.** In the
  live 25 nm sample, 23 of 36 aircraft were on the ground. Then the
  `MAX_AIRCRAFT` = 40 nearest are kept, and the **selected hex is pinned**: the UI publishes
  it via `app_state`, and the parser swaps it into the last slot if it would be
  evicted. The selected plane is never dropped.
- Trails come from the UI keeping the last **8 positions** per hex, one per new position
  (`map_model.cpp`, 40 × 8 floats ≈ 2.6 KB). They expire after 60 s missing.
- **First frame after entering, or after a zoom-out.** The snapshot still covers only the
  previous radius, so draw a dotted `M_MUTED` ring at the covered radius with a glcd
  `WIDENING...` label beside it, until the first wide fetch lands (≤ 5 s). The strip keeps
  describing the focus plane, so the amber link isn't broken.
- **Truncation honesty.** When more airborne aircraft are in range than the 40 kept
  (`totalInRadius > n`), draw the same dotted `M_MUTED` ring at the 40th aircraft's
  distance. The empty area beyond it is "not shown", not "empty sky".
- **Offline.** For ≤ 60 s since the last good fetch, keep the last positions, drawn DIM
  with no tags; the strip says `Radar offline · positions 40 s old`. After 60 s, clear the
  aircraft; the strip says `No live traffic · retrying in 20 s`. A frozen map that looks
  live is never shown. Taps don't select planes while offline.

## Basemap (pre-rendered, zero RAM)
- `tools/basemap/make_basemap.py`, run once and cached in `tools/basemap/cache/`, pulls from OSM:
  - motorways, trunks, runways, major canals and towns to **40 nm**, so the 20 mi zoom has no blank corners
  - primary roads to 25 nm (drawn only at 5 and 10 mi)
- It renders **3 zooms: 5 / 10 / 20 mi**. The ring is 100 px, so 23.0 / 11.5 / 5.75 px per nm.
  Each is a 320 × 240 **4-bit indexed** image, written to `firmware/basemap_data.cpp` (3 × 38.4 KB of flash).
- The pixel packing matches TFT_eSprite 4-bit (row-major, even x in the high nibble), so a band
  is a straight `memcpy` from flash.
- Labels (towns, airport codes) are drawn on the device with the real fonts.
- Re-run the script after changing `OBS_LAT/OBS_LON`. Attribution `(c) OSM` is shown in glcd,
  bottom-left, **at every zoom**.

## Rendering: band sprite (the RAM decision)
Measured on the device (v1, WiFi + TLS): **116 KB free, 55 KB minimum during TLS,
43 KB largest block.** A full-screen 4-bit sprite (38.4 KB) would starve TLS. So:
- One **320 × 48 4-bit band sprite (7.7 KB)**, allocated at boot, with `MAP_PALETTE`.
- **The layout is computed once per frame** (projection, clustering, tag placement,
  label placement), then replayed into each band b = 0..4 (y0 = 48·b):
  1. `memcpy` the basemap rows into the sprite.
  2. Tint the overhead zone: basemap index 0 → `M_ZONE` inside the 3 nm disc.
  3. Draw every overlay offset by `−y0`. The sprite clips, so anything crossing a seam
     comes out whole.
  4. `pushSprite(0, y0)`.
- **No anti-aliased calls in the band sprite** (`drawWideLine`, smooth `drawArc`,
  `fillSmoothCircle`). They blend RGB565 values, which a 4-bit sprite reads as palette
  indices. Chevrons are made of `fillTriangle`s. `tftsim` raises if an AA call is made while
  `sprite4` is set.

## Visual rules
| Element | Rule |
|---|---|
| You | cyan (`M_YOU`) dot r 3 inside a white disc r 5, at (160,116) |
| Nearby disc | 3 nm disc tinted `M_ZONE` + 1 px `M_PLANE_DIM` rim. Named "nearby", not "overhead", because it isn't the trigger (see top) |
| Rings | **dotted `M_DIM`**: the exit ring (4.5 nm) every 4 px, and the range ring (100 px = the zoom chip distance, unlabelled) every 7 px. (The old ring colour `#2A3752` was identical to the trunk colour in RGB565; that slot is now `M_TRAIL`) |
| **Focus plane** | The **selected plane, else the nearest**: the one the info strip describes. **Amber** glyph + amber callsign + amber ring r 11. Its tag sits 14 px out so it clears the ring |
| Other planes | White glyph, white callsign, muted altitude |
| **Will pop soon** | A plane that doesn't qualify now but **passes the full v1 ENTER test at +15, +30, +45 or +60 s**, projected along track, ground speed and vertical rate: ≤ 3 nm, ≥ 25° up, ≥ 300 ft above ground → **dim amber** glyph. The path is sampled because a 250 kt jet can cross the trigger area inside a minute. `mapWillPopSecs()` in `map_model.cpp` is host-tested. This is the only meaning of dim amber. When the focus plane is a will-pop plane, the strip's right side reads `overhead in ~30 s`, which teaches the colour |
| Dismissed plane | white, like any other plane (dim amber is reserved for "will pop") |
| Stale (> 15 s) | DIM glyph, no tag |
| Why amber rarely means "in the zone" | A qualifying plane pops the v1 card over the map within one poll, so an in-zone plane is only seen on the map for ≤ 1 poll, or after a dismiss (after a dismiss it's drawn white) |
| Glyph size | 0.6 (~15 px). **0.45 at 20 mi** |
| Clustering (20 mi) | Planes within 12 px merge into one glyph with a count badge (glcd on `M_PANEL`). The focus plane never merges into a cluster or hosts one. A badge that would touch the focus ring flips to the glyph's left |
| Tags | glcd callsign + a plain altitude line (`900` / `7.2k` / `12k`). Placed **right, else left** of the glyph (never above or below, which would be ambiguous). Placed greedily, focus first, then by distance. Skipped when they'd overlap anything already placed. **The focus tag is guaranteed:** if both sides are blocked by other glyphs, it is placed against the chrome only, on an `M_PANEL` backing, on **the side that covers the fewest planes** (hidden planes can't be tapped) |
| Placement order | chrome (back, zoom, N marker, OSM) → you → **all glyphs** → **airport codes** → tags → towns (towns only where free; your own town is skipped) |
| Trails | 1 px dots in `M_TRAIL` |
| Chrome | `‹` chip (made of triangles), zoom chip `10 MI`, top-centre `N` marker |
| Info strip (214–239) | `M_PANEL`. The focus plane: amber glyph, then **`<operator> · <type>`** (v1 priority: type before flight number). The flight number is inserted, `Southwest 2208 · 737-800`, **only if it fits**. If still too wide, the **operator** truncates; **the type never does**. The right side is fixed: `12k ft · 4.1 mi SW`, or **`20° up · 1.8 mi SW` when the plane is inside the disc** (elevation is the real trigger, worded like the card). Then a `›` affordance. Other states: `Nothing within 5 mi · nearest 7.2 mi NW`, `Radar offline …`, `No live traffic …` |

## Config (`config.h`)
`MAP_IDLE_S 120`, `MAP_POLL_SCALE 2.0`, `MAP_POLL_MAX_NM 35`, `MAP_POLL_Z0_MS 3000`,
`MAP_DEFAULT_ZOOM 1`, `MAP_TAP_RADIUS_PX 28`, `MAP_CLUSTER_PX 12`, `MAP_OFFLINE_KEEP_S 60`.

## States (all mocked)
Round 3, `png/v2r3-map-*`:
- `map-z1`: live, with a stale plane
- `map-z2-busy`: 30 airborne, the real 35 nm sample + 12 **synthetic** PHX arrivals; the focus tag sits on a backing
- `map-z0-selected`: the selected SWA2073 is inside the disc but only 20° up, so no card; the strip shows `20° up`. SWA2208 at 12,000 ft is dim amber because it *will* qualify within 60 s
- `map-empty`: shows the nearest plane beyond range
- `map-loading`, `map-offline-recent`, `map-offline-cleared`
The plane card popping over the map is the v1 card (`final-plane-*`); the return is
defined above.

## Tests
- Host (`test/host/test_map.cpp`, done): projection accuracy, trail dedupe, cap and expiry,
  hit-testing, and that ground aircraft can't be tapped.
- To add with the firmware: `adsbParse` drops ground aircraft before truncation, and the
  pinned hex survives eviction.
- Device: the map centre matches home (check against a known plane on FlightAware), the band
  seams are invisible, and there is no flicker at 3 s updates.
