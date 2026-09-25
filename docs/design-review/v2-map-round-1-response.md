# v2 map, round 1 — Designer response to Mr Stacks (score 6/10)

Round 1 scored below 7, so every required change was addressed. Round-2 renders are
`docs/mockups/png/v2r2-map-*`. The spec is rewritten in `docs/08-plane-map.md`.

> Note: the round-1 renders (`v2-map-*`) were deleted by mistake while re-rendering,
> and can't be regenerated exactly, because the basemap was rebuilt for miles and 40 nm.
> Your review text still describes them.

## Required changes
| # | Change | Check |
|---|---|---|
| 1 | Tap on the map **selects** the nearest airborne plane within **28 px**. **Tapping the info strip** opens its card. The selection sticks until the plane leaves, or the 120 s idle clears it | 08 §Navigation; `map-z0-selected` |
| 2 | **Amber = the focus plane** (the one the strip describes): glyph, callsign and ring. Everyone else is white. It's written down that qualifying planes pop the card within one poll, and that dismissed planes are drawn dim amber | `map-z1` (FALCON7 amber and in the strip); 08 §Visual rules |
| 3 | Busy case: glyphs at 0.45 at 20 mi; **clusters** within 12 px become one glyph + count badge; **airport labels are placed before tags**; a **30-airborne** mock (the real 35 nm sample + 12 synthetic PHX arrivals, labelled as synthetic) | `map-z2-busy` |
| 4 | Rings are dotted `M_DIM`. The clashing slot 6 is now `M_TRAIL` (#4A5670). All 16 palette entries were checked unique in RGB565 | 08 §Visual rules; `make_basemap.py` PALETTE |
| 5 | **Miles**: the zooms are 5 / 10 / 20 mi (the basemaps were re-rendered at 23.0 / 11.5 / 5.75 px/nm), and the chip says `10 MI` | all renders |
| 6 | Altitude tags read `900` / `7.2k` / `12k` | all renders |
| 7 | Offline: ≤ 60 s shows the positions dimmed, with no tags and `positions 40 s old`; > 60 s clears the planes and shows `No live traffic · retrying in 20 s` | `map-offline-recent`, `map-offline-cleared` |
| 8 | The `‹` and `›` chevrons are made of `fillTriangle`s. The rule "no AA calls in a 4-bit sprite" is in 08. **tftsim now raises** on `drawWideLine` / `drawArc` while `sprite4` is set, and the whole map mock runs in that mode | `tftsim.py` `_no_aa`; 08 §Rendering |
| 9 | (a) `map-empty`; (b) `map-loading` (dotted coverage ring at 12 nm + `Widening radar to 20 mi`); (c) the card over the map is the v1 card, and the return plus idle-timer restart are defined in 08 §Navigation; (d) the stale plane SWA1296 is drawn DIM next to live ones in `map-z1` | renders + 08 |
| 10 | Ground aircraft are dropped in `adsbParse` **before truncation**. The 40 nearest are kept, and the **selected hex is pinned** through app_state so it's never evicted. Host tests for both are listed for the firmware step | 08 §Data |

## Nice to have
| # | Status |
|---|---|
| 1 | **Done.** The basemap reaches 40 nm for motorways, trunks, canals and towns (so the 20 mi corners are filled), and the poll goes to 2× the zoom radius, capped at 35 nm |
| 2 | **Done.** The layout is computed once per frame and replayed per band (already in the `ui_map.cpp` draft) |
| 3 | **Done.** Non-focus altitude lines are muted, and only the focus tag is amber |
| 4 | **Done.** The strip reads `Southwest 2208 · 737-800` and plain types (`MD 500`, `Robinson R22`, `HondaJet` …), with a fit rule: the type drops before the name truncates |
| 5 | **Done.** Tags go right or left only |
| 6 | **Done.** A plane whose 60 s projection enters the zone is drawn dim amber |
| 7 | **Done.** Touch areas are 48 × 36 and 74 × 36, and the zoom is remembered in NVS |
| 8 | **Done.** `(c) OSM` at every zoom |
| 9 | **Done.** The N marker area is reserved, and the focus tag sits 14 px out, clear of its ring |
