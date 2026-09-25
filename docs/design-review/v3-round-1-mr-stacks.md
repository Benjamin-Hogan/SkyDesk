# v3 round 1: Mr Stacks

Read: AGENTS, 07, 08, 09, 10, my v3 ideas note, `map_screen.py` (`mk`, `advance`, focus/leader/pointer/strip, `SCENARIOS_V3`), `radar_screen.py`, `radar_data.py`, `screens.py` `weather()`, plus `ui_map.cpp`, `ui_weather.cpp`, `weather_client.cpp` `wxLabel()`, `sd_store.cpp`. Judged on every `v3r1-*@3x.png` and `final-plane-airliner-multi@3x.png`.

## Score: A 7/10 · B 6/10

## Verdict
**A (map).** The bundle does what it promised. `map-inbound` shows which plane, where it pops and when, without a tap. The pop square inside the disc finally makes it obvious that the disc isn't the trigger. Amber discipline held across all 14 states. What's left is behaviour at the edges (an idle pause with no limit, the M7 precedence, an ambiguous cycle rule) and one visible rendering defect in the busy case.

**B (radar + cue).** The foundations are strong: real NEXRAD, no decoder, a timestamp on every frame, and an honest offline state. But the surfaces people see first are leaky:
- the weather hero loses its condition and contradicts its own icon
- the strip words change on every frame
- loading hides the answer
- there are several ways a broken frame can read as "No rain"

Fix those and B is a 7–8.

## Required changes

1. **[B] A broken frame must never read as "No rain".**
   - *What's wrong:* `No rain within 50 mi` is just the absence of classified pixels. Three failures produce it on a stormy day:
     - (a) IEM changes the n0q ramp, or the WMS resamples, so the exact-RGB lookups miss. The mock hides this with the nearest-colour fallback in `radar_data.dbz()`, and the firmware plan has no fallback and no miss handling, so every pixel becomes "none".
     - (b) WMS-T returns a fully transparent image for a `TIME` it hasn't built yet.
     - (c) Radar quorum is low. Doc 10 stores `radar_quorum` in the frame header, but no rule reads it.
   - *Why it matters:* this is the exact "missing frame masquerading as dry sky" failure that doc 10 says it prevents. It's the worst lie this screen can tell.
   - *Do:*
     - Count lookup misses per frame. If more than 0.5 % of non-transparent pixels miss, reject the frame: keep the previous frame, and show WARN `Radar data unreadable`.
     - Treat an all-transparent frame that follows a wet frame (< 20 min apart) as suspect, and retry once before accepting it.
     - Set a quorum floor (e.g. < 95 %: WARN chip plus `partial coverage` on the strip's right side).
     - State in doc 10 that the national quorum doesn't prove KIWA (the Phoenix radar, about 8 mi SE of you) is up.
     - Host tests: a fixture with a shifted ramp must be rejected, and an empty frame after a wet one must trigger a retry.

2. **[B] Weather hero: the cue always replaces the condition, and the hero then contradicts itself.**
   - *What's wrong:* the condition and the cue must share about 203 px of fs9 (x 82→308, minus the separator and chevron). Of the real `wxLabel()` strings, only the very short ones fit alongside the cue (`Sunny`, `Clear`, `Fog`, `Rain`). So "append" is dead code, and both mocks prove it.
   - *Consequences:*
     - `Thunderstorm` and `Storm, hail`, the one weather word that's a safety cue, disappear exactly when radar echoes are close.
     - `weather-rain-cue-long` puts a raining-here icon next to `Rain 11 mi W`, so the hero says two opposite things.
   - *Do:*
     - Never drop the condition.
     - Give the cue its own slot: right-aligned at x 306 on the condition baseline (y 104), under `Sunset 7:34`. Use fs9, falling back to f2 when it would come within 8 px of the condition.
     - Mock the cue with `Freezing drizzle`, `Thunderstorm` and `Storm, hail` as the worst cases.
     - Make sure the hero touch target (currently 0–250 × 30–112) covers the cue's new position.

3. **[B] Strip words must come from the newest frame on every frame.**
   - *What's wrong:* doc 10 says the words come from the newest frame. But `radar_screen()` calls `rain_words(lv)` with `lv` set to the *displayed* frame `fi`. So `radar-monsoon-f2` reads `Rain 46 mi S · heavy 52 mi NE`, while the newest reads `Heavy rain 40 mi NE`.
   - *Why it matters:* during the loop the words would change 5 times in 2 s, which is unreadable. Worse, it describes the past as if it were now.
   - *Do:* take the words from the newest frame's header on every frame, then re-render `radar-monsoon-f2` and the loop sheet.

4. **[A] The idle-timer pause has no limit.**
   - *What's wrong:* under a PHX arrival bank, a new will-pop plane every 1–2 min pauses the 120 s timer. Each pop then returns to the map with a fresh timer (08, Navigation). So an untouched map can hold the screen for hours, and weather, the home screen, never comes back.
   - *Do:*
     - Add `MAP_IDLE_MAX_S` (e.g. 300) to `config.h`: the pause can never extend the time since the last touch beyond it.
     - Host-test the timer with a synthetic stream of will-pop planes.

5. **[B] "Radar" now means two things.**
   - *What's wrong:* the firmware already uses "Radar" for ADS-B:
     - `Radar offline` on the weather chip (`ui_weather.cpp:135`)
     - `Radar offline` on the map strip (`ui_map.cpp:341`, `map-offline-recent`)
     - the `Radar` row on the boot screen

     The new screen adds `Radar offline`, `Radar 25 min old` and `Insert an SD card for radar`.
   - *Why it matters:* both can be degraded at the same time, and the words won't say which one is down.
   - *Do:*
     - Rename the ADS-B vocabulary to **Traffic**: `Traffic offline · positions 40 s old`, and a boot row `Traffic`. The map already says `No live traffic`.
     - Keep "Radar" for rain only.
     - Log the decision in 07.

6. **[B] The clutter threshold is above your own smallest real shower.**
   - *What's wrong:* you measured clutter at ≤ 8 px and real showers at ≥ 10 px, then set the threshold to 12. That deletes real 10–11 px showers from both the picture and the words.
   - *Why it matters:* a young monsoon cell near you is missing for a full frame (10 min), exactly when it matters.
   - *Do:*
     - Set `MIN_BLOB_PX` inside the measured gap (9 or 10).
     - Add a location mask for known static specks, such as the one 15.6 mi N (pixels that are wet on most recent dry frames). That removes clutter without betting on size.
     - Put the measurement table in doc 10 (frames used, maximum clutter size, minimum shower size) so the number can be reviewed.
     - Host test: a 10 px shower survives.

7. **[A] The backed focus tag in the busy case.**
   - *What's wrong:*
     - (a) The `M_PANEL` backing is painted after the glyphs, so it clips them into fragments that read as characters: a `-` just right of the tag in `map-cycle-1` (about 183,98 at 1×), and a `D`-like stub in `map-cycle-2`.
     - (b) The backing width (`map_screen.py` about line 388) omits the +7 px tick that placement reserves. A tag whose widest line is altitude + tick hangs its tick off the backing.
     - (c) The 12 synthetic PHX arrivals have no vertical rate. So `z2-busy` and the cycle frames never draw a tick in the stream, and the one case where tick width changes placement (my guardrail) is untested.
   - *Do:*
     - Every glyph box the backing touches is either fully covered (grow the backing) or makes that side rejected. Covered glyphs count toward the "fewest planes" rule.
     - Use one width function for both placement and backing.
     - Give the arrivals −600 to −1000 fpm, and re-render `z2-busy`, `cycle-1` and `cycle-2`.

8. **[B] Loading hides the answer, and the frame bar's "hollow" means three things.**
   - *What's wrong:*
     - (a) `radar-loading` replaces the words with `Loading earlier frames 2 of 6`, even though the newest frame is already on screen. So the answer you opened the screen for is hidden for the whole backfill.
     - (b) `radar-none` shows 5 hollow segments with no message, which by `radar-loading`'s own rule reads as "stuck loading".
     - (c) `radar-old` also shows 5 hollow segments, for a stopped loop.
   - *Do:*
     - Keep the words while loading. The frame bar filling in is the only loading indicator it needs.
     - When the newest cleaned frame has no rain, skip the backfill and hide the frame bar (one frame, no loop). That saves about 1.5 MB and the fetch time.
     - When the frame is stale, draw every segment DIM and filled. Hollow then means only "not downloaded yet".

9. **[B] Staleness and cadence are out of step.**
   - *What's wrong:*
     - (a) Worst case for a healthy system with the radar open: IEM's latency (about 5 min), plus the 5-min product step, plus your 5-min JSON poll, is about 15 min. That's exactly the WARN threshold, so a healthy system will flash WARN.
     - (b) Off-screen, the dry cadence is 30 min. So the most common open (a dry afternoon that's turning) shows a 20–30 min old frame as `Radar 25 min old · retrying` before anything has even failed.
     - (c) `radar-old` draws stale rain in full colour, and very heavy is full white. 07 (R2-3) says stale data never renders in full white.
   - *Do:*
     - On open, if the newest frame is ≥ 15 min old, fetch at once. Show the chip MUTED with `updating` on the strip's right side until the first attempt resolves. WARN only after a failed attempt.
     - While the radar is open, poll `n0q_0.json` every 2 min (it's a few hundred bytes).
     - When stale, swap the rain slots to a dimmed ramp. It's a palette swap and costs zero RAM.

10. **[B] The radar pipeline must not blind the plane tracker.**
    - *What's wrong:* the net task makes one TLS connection at a time, sequentially. The first-open backfill is 6 × ~300 KB, which doc 10 estimates at 12 s. With SD writes in 1 KB chunks, budget 20 s. For that whole time no ADS-B poll runs, so the map goes DIM (stale > 15 s) and a card pop can be late by the whole backfill. That breaks the product's primary job.
    - *Do:*
      - Add a scheduling rule to doc 10: at most one radar frame per net-task cycle, an ADS-B poll between every two frames, and no radar work at all while any tracked plane is will-pop or in the zone.
      - Add "ADS-B gap during backfill" to the device tests.
    - *Related, the file lifecycle:* the UI (core 1) reads `f<unix>.bin` at 2.5 fps while the net task (core 0) writes and deletes frames.
      - Write to `.tmp` and rename when complete.
      - Delete a file only after the UI has taken a new frame-list snapshot. FATFS won't save you from deleting an open file.

11. **[A] M7 precedence isn't specified, and the mock gets it backwards.**
    - *What's wrong:* `map_screen()` does `sel = selected or after_pop`, so the just-passed plane outranks a will-pop plane. Under a PHX bank the next inbound stays dim amber for up to 30 s, while the strip talks about a plane that's already gone.
    - *Do:*
      - Set the order to **selected > will-pop > after-pop (30 s) > nearest**, and put it in the 09 table.
      - Add a host test.
      - Re-mock `map-after-pop` with a second inbound plane, to prove it.

12. **[A] The tap-again cycle rule is ambiguous and fragile on resistive touch.**
    - *What's wrong:* "Planes within 28 px of the tap, ordered by distance from the tap": which tap?
      - If the list is recomputed on every tap, a finger landing 6 px off re-orders it, and "next" can re-select the same plane.
      - A poll (every 3–5 s) moves the planes inside the window.
      - After 3 s, the same tap jumps back to `1 of 5`.
    - *Do:*
      - Freeze the candidates as hex codes at the first tap, and advance an index on each repeat tap. Drop candidates that leave the traffic.
      - Replace the 3 s window with a rule: "a tap whose nearest candidate is the current focus advances". That needs no timer. Keep `k of N here` for 3 s after each tap.
      - Treat taps < 250 ms apart as one tap (panel bounce).
      - Host-test all of it.

13. **[A] Fix `right[16]` now, and make the strip table testable.**
    - *What's wrong:* 09 files the bug as "fix alongside", but `ui_map.cpp:374` truncates a shipping string today.
    - *Do:*
      - Size the buffer at 32, with a `static_assert` against the longest literal.
      - Move the right-side state selection and fallbacks into a pure function (like the header labels), host-tested with the f2 width table. Otherwise the 09 table and `ui_map.cpp` will drift apart.

## Nice to have

1. **[A] Off-map focus.**
   - Draw the leader and pop square wherever they're on screen, even when the glyph is off the map. M1 can focus a will-pop plane that's still off the map at 5 mi: 60 s at 250 kt is about 4 nm, plus the 3 nm zone, which can put it past the ~5 nm N/S edge.
   - Make the pointer 14 px with a 1 px `M_BG` outline.
   - When it slides below the back chip, an amber left-pointing triangle reads as a second back button. Keep it ≥ 20 px below the chip.
2. **[A]** In `map-inbound-30s`, the pop square sits on the focus ring. Suppress the square when it's within 16 px of the glyph centre; the countdown already carries it.
3. **[A] Fit rule and worst case.**
   - `Sun Coun...` loses the brand to keep the word "overhead". Stop at the first fallback where the operator fits whole; otherwise use the first with a 40 px stub. `in ~45 s` next to amber is clear enough.
   - Align 09's worst-case test with this: `American Eagle` isn't in the firmware table, and `Sun Country` is.
4. **[A]** Use one verb for the trigger. `overhead in ~45 s` and `pops at 25°` name the same event in the same slot, and "pops" is dev slang. For example, `20° up · overhead at 25°`, falling back to `20° up`.
5. **[A] M7 details.**
   - Drop the leader on the passed plane: it's leaving, and a leader invites you to watch something that's over (the spirit of R2-3).
   - The preferred `passed overhead · 3.0 mi NE` never fits next to a real type, so make `passed · 3.0 mi NE` the stated preference.
6. **[B] Radar town labels: choose them for spread, not population.**
   - Four of the 10 slots go to west-valley suburbs (Surprise, Goodyear, Avondale, Buckeye).
   - Mesa (515 k, 5 mi away) loses every placement to Phoenix and the you-dot.
   - Globe, the NE monsoon reference, is cut by the cap.
   - Fix: one label per octant beyond about 15 mi, then fill by population.
7. **[B] Light rain vs roads.** `#1F4F94` has roughly the luminance of motorway `#3C4A66`, and in `radar-night` light rain melts into the roads. Darken slots 2, 3 and 5 in `RADAR_PALETTE`; it costs nothing.
8. **[B] Night preview method.** A 35 % backlight is linear light, which is about ×0.6 on sRGB values, not ×0.35. The current preview is too pessimistic and could push you to over-brighten. Add a night version of `weather-rain-cue`.
9. **[B] Ramp.** Unlabelled is acceptable. Draw it as one continuous bar (for example 30×6, no gaps), so it doesn't rhyme with the six-segment frame bar next to it.
10. **[B] Virga and the "here" case.**
    - Desert echoes at 20–30 dBZ are often virga. Consider firing the cue only for ≥ moderate rain, or for light rain covering ≥ ~40 px.
    - When the radar shows rain within 2 mi but Open-Meteo's condition isn't rain, show `Raining here ›` instead of nothing.
11. **[B] Cleaning pass.**
    - A bounded flood fill (stop at 12 px) over a rolling 24-row window needs no 150 KB SD spill and can't overflow an equivalence table.
    - If you keep the two-pass version, cap the table and define what happens on overflow (keep the frame uncleaned and log it).
    - Also test on an inversion or outflow night: anomalous propagation isn't static, and can exceed any size threshold.
12. **[B] Mock and doc hygiene.**
    - `weather-rain-cue-long` hard-codes `rain 11 mi W`, while the same data gives `10 mi W` on the radar.
    - The `cue_from()` docstring still mentions an 80×60 fetch.
    - `retrying` (old) and `retrying in 30 s` (offline) should match.
    - Doc 10 says the view "spans about 80 mi E–W". It spans about 160 (80 each way).

## What works
- `map-inbound`: the leader, the pop square and the amber countdown answer "which, where, when" with no tap. That's the v2 promise, finally kept.
- Amber stays one plane:
  - the leader, square and pointer are focus-only
  - dim amber is unchanged in `z0-selected`
  - there's no amber anywhere on the radar
- `k of N here` from the first tap teaches the gesture without chrome.
- The fallback table never truncates the type, even in `map-worst-strip`. Mocking the strings is what surfaced the `right[16]` bug.
- The M5 tick is legible at 1×, and it sits at the card's ±300 fpm threshold.
- The whole v2 state set was re-rendered under the v3 rules, and none of them regressed.
- Real archived NEXRAD, the official colour table, and clutter measured rather than guessed.
- Choosing TIFF to SD kills my #1 risk (decoder RAM) outright. That's the right trade.
- Every frame carries its time: MUTED for older frames, WARN when stale. After 60 min offline the rain clears rather than freezes.
- Radar has an explicit no-SD state, and without a card the cue is off.
- The palette swap costs zero RAM and adds no new sprite. Label occlusion is computed across the whole loop, so labels don't flicker.
- The you-dot has a BG ring, so it survives white rain.
