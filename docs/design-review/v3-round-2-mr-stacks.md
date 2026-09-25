# v3 round 2: Mr Stacks

**Read:**
- `v3-round-1-response.md`, and the rewritten 09 and 10
- the new decision lines in 07
- `map_screen.py` (focus, callout/backing, leader, pointer)
- `radar_screen.py` (`rain_words`, `cue_from`, states) and `radar_data.py` (mask + clean)
- in firmware: `radar_client.cpp`, `net_task.cpp` (the radar scheduling), `config.h`, `ui_map.cpp`, `ui_weather.cpp` and `ui_screens.cpp`

**Viewed:** every `v3r2-*@3x.png` and `radar-clutter@3x.png`.

**Checked by script:**
- blob sizes and distances on the newest monsoon and scattered frames
- the clutter-mask geometry
- how many airborne planes sit within 4.5 nm in the 4 captured ADS-B fixtures

## Score: A 8/10 · B 7/10

## Verdict
**A (map).** All round-1 items landed, and the new mocks prove the hard cases. `map-after-pop-inbound` shows the precedence, and `map-offscreen-inbound` shows the pointer, leader and square telling one story. `needs 25°` is better than what I asked for. The rest is two spec bugs (one of them is mine) and a callout slab that's too big in the busy case.

**B (radar + cue).** A big step up:
- the hero keeps its condition
- the words are fixed to the newest frame
- the failure states are real and distinct
- most of the plumbing I asked for is already in firmware and tested

Correcting your own clutter numbers was the right call. Two things still stop it shipping:
- the new radar-deferral rule would starve radar work under Gilbert's normal sky
- the headline on the flagship monsoon mock names a 7 px speck hugging a masked clutter site, while a 681 px heavy storm comes second

## Round-1 status

| # | Item | Status | Note |
|---|---|---|---|
| 1 | [B] Broken frame reads as "No rain" | **Closed** | Exact colours only; `RADAR_MISS_PERMILLE` rejects in `radar_client.cpp`; empty-after-wet re-fetch; quorum WARN; the KIWA caveat is written down. `radar-unreadable` and `radar-partial` are mocked |
| 2 | [B] Cue replaces the condition | **Closed** | Own slot, and the condition is never dropped. The worst cases are mocked, and `Freezing drizzle` falls back to f2 cleanly |
| 3 | [B] Words change per frame | **Closed** | `radar-monsoon-f2` and the loop sheet carry the newest words |
| 4 | [A] Unbounded idle pause | **Closed (spec)** | `MAP_IDLE_MAX_S 300` is in `config.h`; `mapIdleExpired` is still to land |
| 5 | [B] "Radar" means two things | **Mocks closed, firmware open** | The mocks and docs say Traffic. But `ui_map.cpp:341` and `ui_weather.cpp:135` still say `Radar offline`, and the boot row (`ui_screens.cpp:130`) still says `Radar`. The response says "everywhere". See new item 6 |
| 6 | [B] Clutter threshold | **Closed, new issue** | The mask plus the 6 px floor are measured and documented. But the mask is applied per pixel, which lets clutter leak around it and punches holes in real rain (new item 2) |
| 7 | [A] Backed tag clips glyphs | **Closed, follow-up** | No fragments in any v3r2 frame, and the ticks now show in the stream. The slab is now oversized, and its growth isn't a fixpoint (new item 4) |
| 8 | [B] Loading hides the answer; hollow means three things | **Closed** | `radar-loading` keeps the words; `radar-none` has no bar; stale is all-DIM |
| 9 | [B] Staleness vs cadence | **Closed** | `radar-updating` versus `radar-stale` is exactly the distinction I wanted. The 2-min poll fixes the healthy worst case |
| 10 | [B] Pipeline blinds the tracker; file lifecycle | **Closed, new issue** | `.tmp`→rename, delete only after the UI acks, and one frame per pass with an owed ADS-B poll are all in firmware. The deferral radius that came with it breaks radar (new item 1) |
| 11 | [A] M7 precedence | **Closed** | `map-after-pop-inbound` proves it |
| 12 | [A] Ambiguous cycle rule | **Closed, new issue** | Frozen hexes and debounce are good. But the "advance" trigger I proposed fails on the third tap (new item 3) |
| 13 | [A] `right[16]` + testable strip | **Open (firmware)** | `ui_map.cpp:374` is still `char right[16]`, and `mapStripRight()` doesn't exist. The response says "fw next", but 09 says "fixed now" and describes it as done (new item 5) |

Nice-to-haves 1–12 are all done or reasonably substituted:
- NTH 4 (`needs 25°`) is accepted, and it's better.
- NTH 10 (virga) is implemented, but frame-wide rather than per blob; see new item 2.

## Required changes

1. **[B] The radar-deferral radius starves the radar under Gilbert's normal sky.**
   - *What's wrong:* `net_task.cpp:164` skips all radar work while any aircraft is within `RADAR_DEFER_NM` (4.5 nm). Gilbert sits between CHD, IWA and FFZ, among some of the busiest flight-training traffic in the country. In **all 4** captured ADS-B fixtures, at least one airborne plane is within 4.5 nm (`adsbfi_gilbert` 2, `_25nm` 1, `_35nm` 1, `adsblol_gilbert` 2).
   - *Why it matters:* in daytime the cue would essentially never refresh, and backfill would never finish. The feature would work at night and fail the monsoon afternoons it exists for.
   - *Do:*
     - Defer only on what can actually delay a pop: a card is up, any plane is will-pop, or any plane passes the ENTER test.
     - Add a starvation cap: after 120 s of deferral, allow one frame anyway. A frame is ≤ ~3 s, so a pop is late by at most one fetch.
     - Log the deferred seconds per hour (`[radar] deferred N s`) and add them to the device tests.

2. **[B] The headline names a speck, and the mask works per pixel.**
   - *The headline:* `radar-monsoon`, `-partial` and `-night` all lead with `Rain 25 mi E`. By script, that's a **7 px** blob at (209,113), **2.2 px from a masked clutter patch**. It hugs two mask sites (23.7 and 26.5 mi E), and it appears only in the last 2 of 6 frames. The 681 px heavy storm to the NE is relegated to the second phrase. On the weather screen, the cue says `rain 25 mi E` about the same speck.
   - *The virga filter doesn't filter:* `cue_from()` counts `light_px` over **every** wet pixel in the **whole frame**, so on any stormy day it passes. The cue's trigger (moderate anywhere) and its text (the nearest pixel of anything) also refer to different blobs.
   - *The mask punches holes:* zeroing mask pixels before cleaning means a real storm crossing the 44 px Estrella patch or the 26 px NE patch gets a hole in it. And clutter that wanders 1–2 px survives as a "shower" beside the hole.
   - *Do:*
     - **Mask per blob:** clean first; then drop a blob if ≥ 50 % of its pixels lie within 2 px of the mask; never zero the mask pixels of a surviving blob.
     - **Name only significant blobs,** for both the strip and the cue, with one rule: ≥ 12 px containing moderate or above, or ≥ 40 px of light. Smaller blobs are drawn but not named.
     - Re-render the monsoon set and the cue mocks.
     - Host tests: a blob hugging the mask is dropped; a large storm crossing a mask patch keeps every pixel; a 7 px moderate speck isn't named while the heavy storm is.

3. **[A] The cycle's "advance" trigger fails on the third tap. That was my wording, and it's wrong.**
   - *What's wrong:* "a tap whose nearest plane is the current focus advances". After tap 2 the focus is candidate #2, which usually isn't the nearest to the finger. So tap 3 at the same spot hits #1 ≠ focus, "starts a new list", and selects #1 again. The user can only toggle between #1 and #2.
   - *Do:*
     - Advance while the tap is **within 28 px of the frozen list's anchor (the first tap point)** and the focus is in that list; otherwise start a new list.
     - Host test: N+1 taps at the same spot give 1, 2, …, N, then 1. A tap 40 px away starts a new list.

4. **[A] The callout backing is oversized, and its growth isn't a fixpoint.**
   - *Oversized:* in `map-cycle-1` and `-2` the slab is about 80×36 px, roughly 3× the tag. It hides 3–4 planes of the arrival stream, including tap candidates for the very cycle on screen, so "3 of 5" will focus a plane you can't see. It's now the heaviest object on the map.
   - *Not a fixpoint:* in `map_screen()` (about lines 369–376), `r` grows inside a single pass over `shown`. A glyph tested before the growth isn't re-tested, so it can still be clipped, which is the round-1 defect coming back on other data.
   - *Do:*
     - Keep the backing at tag size (plus 2 px), and **don't draw** any glyph or badge whose box it touches. Same hidden count, no fragments, half the area.
     - If you keep growth, iterate to a fixpoint and cap the slab at 1.5× the tag area; beyond that, take the next step outward.
     - Host-test the chosen rule on the busy fixture.

5. **[A] 09 describes firmware that doesn't exist.**
   - *What's wrong:* the M-table row says `right[16]` is "fixed now … now 32 with a `static_assert` … moved into the pure, host-tested `mapStripRight()`". None of it is in the tree. The device still shows `overhead in ~45 ` today.
   - *Why it matters:* a spec that overstates the code is how a "don't regress" list rots.
   - *Do:* land the one-line fix plus `mapStripRight()` with its test now. Otherwise, reword 09 to "to do" until it lands.

6. **[B] Put the Traffic rename in firmware.**
   - *What's wrong:* 07 now lists "Radar means rain radar" as a do-not-regress decision, but the firmware already contradicts it in three places: `ui_map.cpp:341`, `ui_weather.cpp:135` and `ui_screens.cpp:130`.
   - *Do:* change the three strings (the boot row is `n.radarTried`, so rename the field too) before any radar UI ships, so the two words never coexist on a device.

## Nice to have

1. **[B]** In `radar-stale` the rain is dimmed, but `Rain 9 mi W` is still full TEXT. Draw the words MUTED when stale (R2-3).
2. **[B]** In `radar-partial`, the time chip turns WARN although the time is fresh, so it reads as "old". Keep the chip on the age rule, and let `partial coverage` carry the warning.
3. **[B]** In `radar-unreadable`, `Radar data unreadable retrying in 30 s` runs edge to edge as one sentence. Add the separator dot, or shorten it to `Bad radar data`.
4. **[B]** Now that the cue sits in its own slot, lowercase `rain 9 mi W ›` next to `Raining here ›` looks inconsistent. Use `Rain 9 mi W ›`.
5. **[B]** Hide the intensity bar when there's no rain (`radar-none`): it's a legend with nothing to explain.
6. **[B]** The TIFF host fixture is 64×48 (`iem_n0q_64x48.tif`). The device will parse 320×240 planar with 40 strips of 25 rows. Add a full-size fixture (it's mostly transparent, so it gzips small) so the strip-offset path is exercised.
7. **[A]** In `map-offscreen`, a selected off-map plane that isn't will-pop draws a leader fragment entering the map about 15 px from the pointer. That's two amber marks on one edge for one plane. Draw an off-map leader only when there's a pop square to lead to.
8. **[A]** In `map-worst-strip`, `in ~45 s` next to `Sun Country 2417` is fine. But the flight number is the lowest-priority item and was inserted *after* "overhead" was dropped. Insert the flight number only when the preferred right side is kept.

## What works
- **The response's honesty.** You corrected your own clutter numbers, caught the non-dry "dry" frame, and validated on held-out frames. That's the discipline this screen needs.
- The weather hero keeps `Thunderstorm` and `Storm, hail`. The cue has its own slot, and `Freezing drizzle` falls back to f2 without a collision.
- The failure states are distinct and truthful: `updating` (nothing failed), `stale` (dimmed ramp), `unreadable` (previous frame, WARN), `partial`, `offline`, `no-sd`.
- The frame bar has one meaning for hollow; no rain means no bar and no backfill; loading keeps the answer on screen.
- The firmware already carries the miss guard, `.tmp`→rename, delete-after-ack, and one frame per pass with an owed ADS-B poll, plus pixel parity between TIFF and Python.
- Map:
  - `after-pop-inbound` and `offscreen-inbound` prove precedence and off-map focus in one frame each
  - the pop square hides near the glyph
  - the pointer is outlined and can't pass for a back button
- `20° up · needs 25°` is plain, fits beside the operator and type, and shows the threshold. Keep it.
- The Traffic vocabulary reads cleanly in `weather-degraded` and `map-offline-recent`.
- The night preview now uses the right method, and the light-rain palette separates from the roads.
