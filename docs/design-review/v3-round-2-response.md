# v3 round 2: response to Mr Stacks (A 8/10 · B 7/10)

The renders are `docs/mockups/png/v3r3-*` (1× plus @3x). Specs: `09-map-v3.md` and `10-rain-radar.md`.
**This round, the firmware caught up with the spec.** Everything marked **(fw)** is in the tree,
compiles (`pio run`), and is host-tested (`sh test/host/run.sh`, all passing).

## Required changes

| # | Item | Status | What changed |
|---|---|---|---|
| 1 | [B] The deferral radius starves the radar | **Fixed (fw)** | The 4.5 nm rule is gone. Radar work is deferred **only** while a card is up, a plane is will-pop, or a plane passes the ENTER test (`mapQualifies`). A **starvation cap** of `RADAR_DEFER_MAX_S` = 120 s then lets one frame run (a pop is at most one fetch, about 3 s, late). The log prints `[radar] deferred N s in the last hour`, and it's in the device tests. `net_task.cpp`. |
| 2 | [B] The headline names a speck; the mask works per pixel | **Fixed (fw + mock)** | **Per blob:** `radarClean(…, nearMask)` accumulates each blob's size, max level and pixels within 2 px of the mask (`RADAR_CLUTTER_NEAR`, generated). It drops a blob that is < 6 px, or ≥ half near the mask, and never punches holes in a surviving blob. **Naming:** a surviving blob that isn't ≥ 12 px with moderate+, or ≥ 40 px of any rain, is flagged `RADAR_UNNAMED`: drawn, never named. The strip and the cue share this one rule. `radar-monsoon` now leads with **`Heavy rain 40 mi NE`** (the 681 px storm); the 7 px speck is drawn and unnamed. Host tests: a storm crossing a mask site keeps every pixel; a blob hugging the mask is dropped; the 7 px speck is unnamed while the storm is named; 30 px of light rain is unnamed. |
| 3 | [A] The cycle fails on the 3rd tap | **Fixed (fw)** | The frozen list keeps the first tap as its **anchor**. A tap within 28 px of the anchor, with the focus in the list, advances. Host test: 4 taps at one spot give 2, 3, 1, 2 (wrapping), and the order survives a poll moving the planes; a tap 40 px away starts a new list. |
| 4 | [A] The callout slab is oversized and not a fixpoint | **Fixed (fw + mock)** | Your first option: the backing is **tag-sized (+2 px)**, and every glyph or badge it touches is **not drawn** (hidden, counted). There's no growth, so there's no fixpoint issue. The fewest-hidden side wins, and the callout steps out when both sides touch the ring. `map-z2-busy`, `map-cycle-1/2`. |
| 5 | [A] 09 described firmware that didn't exist | **Fixed (fw)** | It exists now. `mapStripRight()` is in `map_model.cpp`, host-tested with the real Font 2 widths (every state, the Sun Country worst case, a 40 px stub for all states). `ui_map.cpp` has no `right[16]` any more; it uses 24-byte segments plus a `static_assert`. **The whole v3 map is ported:** focus/hold/after-pop, leader, pop square, pointer, ticks, backings/callouts, anchored cycle, capped idle hooks. |
| 6 | [B] Traffic rename in firmware | **Fixed (fw)** | `ui_map.cpp` `Traffic offline`, `ui_weather.cpp` `Traffic offline`, boot rows `Traffic` and `Weather, traffic`. The field is now `NetStatus::trafficTried`, and the `radarUp` parameters are `trafficUp`. |

## Nice to have

| # | Status | Note |
|---|---|---|
| 1 | **Done** | Stale words are MUTED (`radar-stale`). |
| 2 | **Done** | `radar-partial` keeps the chip on the age rule; `partial coverage` carries the warning. |
| 3 | **Done** | `Bad radar data · retrying in 30 s`. |
| 4 | **Done** | The cue is capitalized: `Rain 10 mi W ›`. |
| 5 | **Done** | No intensity bar when there's no rain (`radar-none`). |
| 6 | **Done** | `test/host/fixtures/iem_n0q_320x240.tif`: 320×240 planar, 40 strips of 25 rows, all levels equal to the Python reference, 0 misses. (Stored raw: the test build has no zlib.) |
| 7 | **Done (fw + mock)** | An off-map leader is drawn only when it leads to a pop square (`map-offscreen` shows just the pointer). |
| 8 | **Done (fw + mock)** | The flight number is inserted only when the state's preferred option was kept (`mapStripRight(…, &preferred)`). |

## Still to land before the flash (tracked, not claimed)
- `ui_radar.cpp` (the screen, band playback from SD, frame loop, UI ack of the frame list)
- the weather cue slot in `ui_weather.cpp`
- navigation (weather hero → radar, idle) in `main.cpp`
- `mapCardClosed()` / `mapIdleExpired()` wiring in `main.cpp`

The pipeline those use (download, TIFF, classify, per-blob clean, words, wet mask, `.tmp`→rename,
delete-after-ack, scheduling) is already in firmware.
