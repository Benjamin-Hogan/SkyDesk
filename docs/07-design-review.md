# 07 — Design Review Log

> Audience: anyone changing the UI. These are the reasons behind the layout. Before
> "simplifying" something, check whether a round below asked for it.

The mockups were reviewed by **Mr Stacks**, an independent reviewer agent acting
as a senior embedded-display designer. Each round scored 1–10, with 7 as the ship bar.
The designer had to respond to every required change. The brief was three rounds.

| Round | Mockups | Score | Review | Response |
|---|---|---|---|---|
| 1 | `mockups/r1-*.html` (HTML/SVG, browser fonts) | **6/10** | [round-1-mr-stacks.md](design-review/round-1-mr-stacks.md) | [round-1-response.md](design-review/round-1-response.md) |
| 2 | `mockups/png/r2-*` (tftsim: real TFT_eSPI fonts, RGB565) | **7/10** | [round-2-mr-stacks.md](design-review/round-2-mr-stacks.md) | [round-2-response.md](design-review/round-2-response.md) |
| 3 | `mockups/png/r3-*` | **8/10** | [round-3-mr-stacks.md](design-review/round-3-mr-stacks.md) | [round-3-response.md](design-review/round-3-response.md) |
| final | `mockups/png/final-*` (round-3 fixes applied; what firmware implements) | n/a | | |

## v2 — plane map (docs/08-plane-map.md)
| Round | Mockups | Score | Review | Response |
|---|---|---|---|---|
| 1 | `v2-map-*` (deleted by mistake; the review text is the record) | **6/10** | [v2-map-round-1-mr-stacks.md](design-review/v2-map-round-1-mr-stacks.md) | [v2-map-round-1-response.md](design-review/v2-map-round-1-response.md) |
| 2 | `mockups/png/v2r2-map-*` | **7/10** | [v2-map-round-2-mr-stacks.md](design-review/v2-map-round-2-mr-stacks.md) | [v2-map-round-2-response.md](design-review/v2-map-round-2-response.md) |
| 3 | `mockups/png/v2r3-map-*` | **8/10** | [v2-map-round-3-mr-stacks.md](design-review/v2-map-round-3-mr-stacks.md) | [v2-map-round-3-response.md](design-review/v2-map-round-3-response.md) |

## Decisions that came out of review (don't regress these)
- **Honest previews.** Mockups are drawn by `tftsim.py` with the real bitmap
  fonts. It refuses glyphs the fonts don't have (R1-6, R1-16).
- **One compass word per screen.** No heading cell; direction of travel is only the
  dome arrow (R1-4).
- **`42° up` on one baseline.** No azimuth degrees anywhere (R1-1).
- **The type is in the big header font.** The flight number is secondary (R1-2).
- **Routes can be wrong.** Show `ROUTE UNVERIFIED` as text and color (R1-3).
- **Fixed-length direction arrow**, computed from true positions (R2-1).
- **`drawNumber()`** for fsb9 numbers, because `8` has no side-bearing (R2-4).
- **The dome sprite is 16-bit.** 4-bit sprites break anti-aliased drawing (R2-6).
- **Dismiss fires on release; settings on hold.** No gesture collision (R2-9).
- **Confidence styling.** Stale and departing never render in full white/amber (R2-3).
- **Dome sprite pushed with `COL_BG` transparent.** Labels around the dome survive every update (R3-1).
- **The glyph follows its on-sky motion** when an arrow is drawn, so the two never disagree (R3 nice-to-have 1).
- **Map: select, then tap the strip.** A fingertip can't hit a 15 px plane on resistive touch (v2-R1-1).
- **Map: amber = the plane the strip describes; dim amber = "will pop" only**, computed with the real trigger test along the path (v2-R1-2, v2-R2-1, v2-R3-2).
- **Map: the disc is "nearby", not the trigger.** Inside it the strip shows `20° up` (v2-R2-1).
- **Map: miles, plain altitudes (`7.2k`), and the type before the flight number in the strip** (v2-R1-5/6, v2-R2-2).
- **Map: band sprite, no anti-aliased calls in 4-bit sprites** (tftsim enforces it) (v2-R1-8).
- **"Radar" means rain radar; the ADS-B feed is "Traffic"** (`Traffic offline`, the boot row `Traffic`). The two can fail independently, and the words must say which one did (v3-R1-5).
- **Map focus: selected > will-pop > just popped (30 s) > nearest.** The idle pause behind a will-pop focus is capped at 300 s since the last touch (v3-R1-4, v3-R1-11).
- **Map: the focus tag never clips glyphs.** Its backing covers whole glyphs (and badges), or the tag becomes a callout (v3-R1-7).
- **Radar never lies "No rain":** exact n0q colours only (> 0.5 % misses rejects the frame), an empty-after-wet frame is re-fetched once, quorum < 95 % gives a WARN (v3-R1-1).
- **Radar words always describe the newest frame**, whatever frame the loop is showing (v3-R1-3).
- **The weather condition text is never dropped** for the rain cue; the cue has its own slot (v3-R1-2).
- **Radar clutter: a measured static mask plus a 6 px blob floor.** Change them only by re-running `tools/radar/make_clutter_mask.py` and updating the table in 10 (v3-R1-6).
- **Radar never says "No rain" over drawn echoes** (`Small echoes only`), and rain within 5 mi is always named, whatever its size (v3-R3-1).
- **Idle uses two stamps:** card returns restart the 120 s timer but never the 300 s cap since the last real touch (v3-R3-2).
