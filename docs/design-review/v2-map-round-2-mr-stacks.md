# Design Review — v2 Plane Map — Round 2 — Mr Stacks

Reviewed: `v2-map-round-1-response.md`; the rewritten `08-plane-map.md`; `map_screen.py`
(traffic model, `inbound` flag, clustering, chevrons); the `tftsim.py` `_no_aa` guard; and all
seven `v2r2-map-*@3x.png` renders. I also ran the scenario data through `geo.py` to check
elevation and qualification for every plane inside the zone disc.

Noted: the round-1 renders were deleted. My round-1 text stands as the record, and the
round-2 renders are what I judge.

## Score: 7/10

## Verdict
A solid round. Every round-1 item landed:
- Tapping is now select-then-strip.
- Amber marks the plane the strip describes.
- The busy case has clusters and airport-first labels.
- Rings are visible, and units are miles.
- Offline is honest.
- tftsim now enforces the no-anti-aliasing rule in 4-bit sprites.

The map looks like a product. One conceptual problem is left, and it undercuts the feature's
headline promise. The brown "overhead zone" disc is drawn at 3 nm ground distance, but a card
only pops at ≥ 25° elevation. For typical Gilbert traffic (2–7k ft), that means well inside
the disc. `v2r2-map-z0-selected` shows it: SWA2073 sits 1.8 nm from you, inside the disc, at
20°, and no card will ever pop. The "heading for the zone" dim amber uses the same
distance-only test. Fix that, plus one small strip-priority slip, and this ships.

## Round 1 items

| # | Item | Status | Evidence / note |
|---|---|---|---|
| 1 | Fingertip tapping | **Fixed** | Tap selects the nearest plane within 28 px; tap the strip opens the card; the selection is sticky and the idle timeout clears it (08 §Navigation). The `z0-selected` state is now reachable. |
| 2 | Amber = the strip's plane | **Fixed** | FALCON7 in `z1`: amber glyph, callsign and ring, and it's the plane in the strip. Rules for qualifying and dismissed planes are written down. |
| 3 | Busy case | **Fixed** | `z2-busy` (30 airborne, synthetic arrivals labelled as synthetic): 0.45 glyphs, `2` cluster badges, airport codes placed before tags. The arrival stream now reads as a stream, not a bar. |
| 4 | Invisible rings / duplicate palette slot | **Fixed** | Dotted `M_DIM` exit and range rings are visible in every render. Slot 6 is now `M_TRAIL`, and all 16 entries are unique in RGB565. |
| 5 | Miles | **Fixed** | `5 / 10 / 20 MI`, the basemaps re-rendered per mile, `mi` in the strip. |
| 6 | Plain altitude | **Fixed** | `1.9k`, `7.2k`, `10k`, `900`. |
| 7 | Offline honesty | **Fixed** | `offline-recent`: dim planes, no tags, `positions 40 s old`. `offline-cleared`: map empty, `No live traffic · retrying in 20 s`. |
| 8 | Anti-aliased `‹` in the 4-bit sprite | **Fixed** | Triangle chevrons. tftsim raises on `drawWideLine`/`drawArc` when `sprite4` is set, and the whole map mock runs in that mode. |
| 9 | Missing states | **Fixed** | Empty, loading (coverage ring + `Widening radar to 20 mi`), card-over-map return with an idle-timer restart (spec), and a stale plane drawn DIM in `z1`. |
| 10 | Cap and eviction | **Fixed (spec)** | Ground aircraft are dropped before truncation, the 40 nearest are kept, and the selected hex is pinned. The host tests are listed for the firmware step. I'll hold you to them. |
| NTH 1–9 | | **All done** | 40 nm basemap (no blank corners), layout computed once per frame, muted non-focus altitude lines, `Southwest 2208` / `MD 500`, right/left-only tags, a dim-amber prediction (but see #1 below), padded touch areas + zoom saved in NVS, `(c) OSM` at every zoom, N marker reserved. |

## Required changes

1. **The zone disc and the "heading for the zone" amber must match the real trigger.**
   A card pops only when a plane is ≤ 3 nm **and** ≥ 25° up (01 §Core behavior). The 25° rule
   means the plane has to be within `AGL / tan 25°`. That works out to:

   | Height above ground | Plane must be within |
   |---|---|
   | 2,000 ft | 0.7 nm |
   | 3,760 ft | 1.3 nm |
   | 5,000 ft | 1.8 nm |
   | 8,000 ft | 2.8 nm |

   The 3 nm disc only means "will pop" for planes above about 8,500 ft above the ground,
   and most local traffic is lower than that. In `z0-selected`, SWA2073 is 1.8 nm away,
   at 5.2k ft and 20° up: inside the disc, and no card ever pops. SWA2208, the selected
   "heading for the zone" example, has `inbound = True` but would reach only ≈ 21°, so it
   won't pop either. `mk()` sets `inbound` from ground distance alone. The user watches the
   plane cross the brown disc, nothing happens, and the "killer feature" reads as broken.
   Fix:
   - **(a) Required:** the dim-amber prediction uses the full `qualifies()` test at +60 s
     (distance, elevation, and ≥ 300 ft AGL), never distance alone. Dim amber must mean
     "this one will pop the card".
   - **(b) Required:** when the focus plane is within the disc, the strip's right side
     says v1's words, `20° up · 1.8 mi SW`, instead of just altitude. Elevation is the
     actual trigger, and it's the number the user will see on the card. This explains the
     "why no card?" moment without a legend.
   - **(c) Pick one and document it in 08 §Visual rules.** Either keep the disc as
     "within 3 mi of you" (rename it in the spec and never call it the trigger zone), or,
     for the focus plane only, draw its personal trigger ring: dashed `M_PLANE_DIM`,
     `r = min(3 nm, AGL / tan 25°)`. I lean towards (c)-rename plus (a) and (b). It's
     simpler, and the amber carries the promise.
   - Re-mock `z0-selected` with a plane that *will* qualify (≥ 8.5k ft AGL near the rim,
     or lower and closer) so the hero mock demonstrates the real behaviour.

2. **The strip drops the wrong thing first.** The fit rule "type drops first, then the
   name truncates" turns `z0-selected` into `Southwest 2208`, which loses the aircraft type
   (v1 priority #2) and keeps the flight number, which v1 deliberately demoted to line 2.
   Fix the order to match v1:
   - Show `Southwest · 737-800`.
   - Add the flight number (`Southwest 2208 · 737-800`) only if it fits.
   - Then truncate the operator. Never truncate the type.

## Nice to have

1. **Be honest about truncation.** At the 20 mi zoom the poll reaches 35 nm, but only the
   40 nearest airborne planes are kept. At PHX peak, the outer ring and corners will be
   empty while traffic is there. When `totalInRadius` > 40, draw the dotted `M_MUTED`
   coverage ring (you already have it for loading) at the 40th plane's distance, so an
   empty edge doesn't read as an empty sky.
2. **Keep the focus link while loading.** `map-loading` shows FALCON7 in amber, but the
   strip says `Widening radar to 20 mi`, so for up to 5 s the amber has no explanation.
   Keep the focus info in the strip, and put `widening…` as glcd MUTED text next to the
   coverage ring instead.
3. **Guarantee the focus tag.** In `z2-busy` the amber FALCON7 has no tag: glyph
   reservations blocked both sides. Let the focus tag overlap non-focus glyphs (drawn on
   a 1 px `M_PANEL` backing). Its ring also collides with a neighbouring `2` cluster badge.
   Suppress badges within the ring.
4. **Dim amber has two meanings.** "Will pop soon" and "dismissed" share `M_PLANE_DIM`.
   After #1a, "will pop soon" is the valuable one. Draw dismissed planes as ordinary white,
   since the dismissal is already the user's own memory, or use a hollow glyph.
5. **Empty state.** `No aircraft within 5 mi` could add the nearest one outside
   (`nearest: 7.2 mi NW`, tap `+` to zoom). It's cheap and turns a dead end into a
   suggestion.

## What works
- **The select-then-strip model** is exactly right for resistive touch. The big target
  confirms and the small target only selects, so mis-taps cost nothing.
- **Amber means the plane in the strip.** With the ring and amber callsign, map and strip
  now read as one sentence.
- **The busy case scales.** Clusters, smaller glyphs and airport-first placement turn the
  PHX arrival stream into something you can read, and the synthetic data is labelled
  honestly.
- **The offline and loading states** are honest and specific (`positions 40 s old`,
  `No live traffic`, `Widening radar to 20 mi`). No frozen map pretends to be live.
- **Engineering discipline.** The tftsim guard turns the anti-aliasing-in-4-bit-sprite rule
  into a hard error. The layout is computed once per frame. Ground aircraft are filtered
  before truncation, and the selected hex is pinned. Palette uniqueness was checked in
  RGB565.
- **Visual quality is good.** The quiet navy basemap, the dotted rings, the brown disc with
  the cyan "you", and plain `7.2k` tags make it a calm, legible map at arm's length.
