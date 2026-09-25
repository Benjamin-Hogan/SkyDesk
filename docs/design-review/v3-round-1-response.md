# v3 round 1: response to Mr Stacks (A 7/10 · B 6/10)

The renders are `docs/mockups/png/v3r2-*` (1× plus @3x). Specs: `09-map-v3.md` and `10-rain-radar.md`,
both rewritten. The decisions are logged in `07-design-review.md`. Firmware work that is already done
is marked **(fw)**, and **(host)** marks host tests that pass today (`sh test/host/run.sh`, 91 checks).

## Two corrections to my own round-1 claims
1. **The clutter numbers were wrong.** "Clutter ≤ 8 px, showers ≥ 10 px" came from 3 frames.
   I surveyed 24 frames. Two of my "dry" dates (6 May and 2 Jun 2025) turned out to have real
   weather, and are now excluded by rule. **Unmasked dry clutter reaches 10 px**, so size alone
   *cannot* separate clutter from small showers (6–11 px blobs occur 26 times on the real wet frames).
   Full table in 10 → *Clutter measurement*.
2. **The "dry" frame in `radar-none` wasn't dry.** At the lower threshold, 23 Jul 2025 (monsoon
   season) keeps a 6 px echo at 32 mi E. `radar-none` now uses a **held-out** frame (25 Jun 2025)
   that wasn't in the mask survey.

## Required changes

| # | Item | Status | What changed |
|---|---|---|---|
| 1 | [B] Broken frame reads as "No rain" | **Fixed** | (a) **Exact colours only**: the nearest-colour fallback is gone from both the mock and the firmware (`radarDbz2` returns a miss). More than 0.5 % misses rejects the frame; the previous frame stays up (`radar-unreadable`, WARN). Measured: 0 misses on all 13 real frames. (b) An empty frame after a wet frame < 20 min earlier is re-fetched once, 60 s later. (c) Quorum < 95 % gives a WARN chip and `partial coverage` (`radar-partial`). The KIWA caveat is in 10. Host tests for the shifted ramp and empty-after-wet are listed for the firmware pass. |
| 2 | [B] The cue replaces the condition | **Fixed** | The condition is never dropped. The cue has its own slot: right-aligned at x 306 on y 104, under the sunset line. fs9, falling back to f2 within 8 px. Mocked with `Thunderstorm`, `Freezing drizzle` (uses f2), `Storm, hail`, and `Raining here`. The hero target is now the full band 0–320 × 30–112, covering the cue. |
| 3 | [B] Words change per frame | **Fixed** | Words always come from the newest frame's header (`h.rain/heavy` **(fw)**). `radar-monsoon-f2` and the loop sheet read `Rain 25 mi E · heavy 42 mi NE` on every frame. |
| 4 | [A] Unbounded idle pause | **Fixed (spec)** | `MAP_IDLE_MAX_S` = 300 caps the pause (09 M1b). `mapIdleExpired()` plus a host test with a stream of will-pop planes lands with the firmware. |
| 5 | [B] "Radar" means two things | **Fixed** | ADS-B is now **Traffic** everywhere: the weather chip `Traffic offline` (`weather-degraded`), the map `Traffic offline · positions 40 s old`, and the boot row `Traffic`. Logged in 07. |
| 6 | [B] Clutter threshold above the smallest shower | **Fixed, with new data** | A **static clutter mask** (`tools/radar/make_clutter_mask.py` → `firmware/radar_clutter.cpp`, 172 px, preview `png/radar-clutter@3x.png`), then a **6 px** blob floor. The masked dry clutter maximum is 5 px. The measurement table is in 10. Host test: a 6 px shower survives while a 5 px speck is dropped (firmware pass). |
| 7 | [A] Backed tag clips glyphs; tick width; arrivals have no rate | **Fixed** | One `tag_w()` serves both placement and the backing. The backing grows to cover every touched glyph **and cluster badge** whole. It may not touch the focus ring. If neither side works, the tag steps outward as a **callout** with a 1 px amber line (`map-z2-busy`, `map-cycle-1/2`). The arrivals now descend at −600 … −1000 fpm, so the ticks show in the stream. |
| 8 | [B] Loading hides the answer; hollow means three things | **Fixed** | The words show during loading, and the bar is the only loading indicator (`radar-loading`). No rain means **no bar, no loop, no backfill** (`radar-none`). Stale fills every segment DIM. Hollow now means only "not downloaded". |
| 9 | [B] Staleness vs cadence | **Fixed** | On open with a frame ≥ 15 min old: fetch at once, MUTED chip plus `updating` (`radar-updating`). WARN only after a failed attempt. `n0q_0.json` is polled every **2 min** while the radar is open (worst healthy age ≈ 12 min, under the 15 min threshold). Stale rain is drawn in a **dimmed ramp** (a palette swap, nothing near white) (`radar-stale`). |
| 10 | [B] The pipeline blinds the tracker; file lifecycle | **Fixed** | Scheduling rule in 10: at most one frame per pass, an ADS-B poll between frames, and **no radar work while a card is up, any plane is will-pop, or any is within 4.5 nm**. Files are written to `.tmp` and renamed **(fw)**. A frame is deleted only after the UI acknowledges a frame list without it. An ADS-B-gap device test was added. |
| 11 | [A] M7 precedence | **Fixed** | **Selected > will-pop > after-pop > nearest.** `map-after-pop-inbound` proves it: AAL2651 is inbound and wins; the passed SWA3319 is white. |
| 12 | [A] Ambiguous cycle rule | **Fixed (spec)** | Candidates are frozen as hex codes at the first tap. **A tap whose nearest plane is the current focus advances** (no timer). Polls can't reshuffle the list. Taps < 250 ms apart are debounced. `k of N here` shows for 3 s after each tap. `mapTap()` host tests land with the firmware. |
| 13 | [A] `right[16]` now + a testable strip | **Fixed (spec), fw next** | 32 bytes with a `static_assert`. The right-side state and fallback choice become a pure `mapStripRight()`, host-tested with the Font 2 width table. |

## Nice to have

| # | Status | Note |
|---|---|---|
| 1 | **Done** | The leader and pop square are drawn where on-map even when the glyph isn't (`map-offscreen-inbound`). The pointer is 14 px with a 1 px BG outline, ≥ 20 px below the back chip. |
| 2 | **Done** | The pop square is hidden within 16 px of the glyph (`map-inbound-30s`). |
| 3 | **Done** | The fit rule keeps the operator whole: `Sun Country 2417 · 737 MAX 8   in ~45 s`. The 09 worst case now uses Sun Country. |
| 4 | **Done, differently** | One plain verb, but `needs 25°` instead of `overhead at 25°`. The latter never fits beside `Southwest · 737 MAX 8`, so the threshold would never be seen. `20° up · needs 25°` fits (`map-z0-selected`). |
| 5 | **Done** | No leader on a passed plane. `passed · 3.0 mi NE` is the preferred form. |
| 6 | **Done** | Labels spread by octant beyond 15 mi, then by population. Globe now appears in the east. The generated town table keeps every in-view town (48 max). |
| 7 | **Done** | The radar palette darkens trunk, motorway and water (`#1E283A #2A3448 #11243A`). Light rain moved to `#2358A8`. |
| 8 | **Done** | The night preview uses ×0.62 on sRGB (35 % linear). Added `weather-rain-cue-night`. |
| 9 | **Done** | The ramp is one continuous 30×6 bar. |
| 10 | **Done** | The cue fires for ≥ moderate rain, or light rain over ≥ 40 px. `Raining here ›` shows within 2 mi (`weather-cue-here`). |
| 11 | **Done, different method** | Two-pass **run-length union-find**, not a flood fill: about 12 KB transient, **no SD spill**, since pass 2 recomputes identical labels. Overflow (> 3,000 runs) keeps the frame uncleaned and logs it. **(fw, host)** Inversion nights are listed as a device test and a known limit. |
| 12 | **Done** | The cue mocks use the real frame's `rain 9 mi W`, the same as the strip. The `cue_from` docstring is fixed. `retrying in 30 s` is used everywhere. The span is 160 mi E–W. |

## Already built while you reviewed (not UI, so it won't move with the design)
- **SD on HSPI, touch bit-banged** (`touch_input.cpp` mirrors XPT2046_Touchscreen command for command, so saved calibrations stay valid). **(fw, compiles)**
- **`radar_model`:**
  - TIFF parsing (chunky + planar)
  - the exact n0q lookup (a generated 255-entry table)
  - union-find cleaning
  - nearest rain

  **(fw, host)** On the real IEM TIFF fixture, every pixel's level matches the Python reference.
- **`map_model` v3:**
  - `mapWillPop` (5 s grid plus point)
  - `mapPickFocus` (hold/drop)
  - `mapTapCandidates`

  **(fw, host)**
- **The radar basemap zoom R and `RADAR_TOWNS`** in `make_basemap.py`. The existing zooms are byte-identical.
