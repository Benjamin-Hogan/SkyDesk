# v3 round 3 (final): Mr Stacks

**Read:**
- `v3-round-2-response.md`, and 09 and 10
- `map_model.cpp` (`mapTap`, `mapIdleExpired`, `mapStripRight`)
- in `ui_map.cpp`: the backing, callout, pointer and strip
- the radar deferral block in `net_task.cpp`
- `main.cpp` (map entry and idle)
- `radar_data.py` and `radar_screen.py` (per-blob clean and naming)

**Viewed:** the changed `v3r3-*@3x.png` renders (map cycle/busy/offscreen/worst-strip; radar monsoon/stale/unreadable/partial/none; the weather cue).

**Ran:**
- `sh test/host/run.sh`: all passed
- `pio run`: see below
- scripts for the monsoon speck and an unnamed-only frame

## Score: A 8/10 · B 8/10

## Verdict
**A (map).** Done, and it earns the score. The firmware now matches the spec:
- anchored cycle
- tag-sized backing that hides whole glyphs
- `mapStripRight` with the real Font 2 widths
- the Traffic vocabulary

The busy frames are clean. One wiring trap is left: the idle cap can't hold as the function is shaped, because a return from a card resets the only timestamp it reads.

**B (radar + cue).** The honesty model is now coherent: blobs are cleaned per blob, named with one rule, and the strip and cue share it. Stale, unreadable and partial each read correctly. The monsoon mock finally leads with the storm.

One gap in the new naming rule contradicts the promise in 07: when the only echoes are unnamed, the screen draws rain and says `No rain within 50 mi`. That includes a small cell directly over you.

Fix those two, re-green the build, and I'd sign off on both.

**Build note (not scored):** at review time `pio run` **fails** in `firmware/ui_radar.cpp:372`, where `RADAR_CUE_MAX_AGE_MIN` isn't declared. The file was saved at 23:37, after the response, and it's on the "still to land" list, so I'm treating it as work in progress, not a false claim. But the tree isn't flashable until it's green again.

## Round-2 status

| # | Item | Status | Note |
|---|---|---|---|
| 1 | [B] The deferral radius starves the radar | **Closed (fw)** | Deferral applies only for a card, will-pop or ENTER, and `RADAR_DEFER_MAX_S` 120 caps it. The hourly deferral log is present. The comment above the block still says "inside the exit radius" (NTH 4) |
| 2 | [B] Headline speck; per-pixel mask | **Closed (fw + mock)** | Clean, mask proximity and naming all happen per blob, with host tests for storm-over-mask, mask-hugger, unnamed speck and virga. `radar-monsoon` leads with `Heavy rain 40 mi NE`. The new rule has one gap (new item 1) |
| 3 | [A] Cycle fails on the 3rd tap | **Closed (fw)** | `mapTap` anchors on the first tap and requires the focus to be in the list; bounce taps don't extend the window. The test covers the wrap, a poll moving the planes, and a new list at 40 px |
| 4 | [A] Callout slab oversized / not a fixpoint | **Closed (fw + mock)** | The backing is tag-sized, touched glyphs are marked `hidden` and not drawn, and there's no growth. `map-cycle-1/2` and `z2-busy` read cleanly |
| 5 | [A] 09 described firmware that didn't exist | **Closed (fw)** | `mapStripRight()` is used in `ui_map.cpp:531`, with a `static_assert` at :533. There's no `right[16]` any more |
| 6 | [B] Traffic rename in firmware | **Closed (fw)** | `ui_map.cpp:490` and `ui_weather.cpp:135` say `Traffic offline`, the boot row says `Traffic`, and the field is `trafficTried` |
| NTH 1–8 | | **All done** | I verified the MUTED stale words, the partial chip on the age rule, `Bad radar data ·`, `Rain 10 mi W ›`, no bar in `radar-none`, the 320×240 planar fixture, no leader on an off-map plane that isn't will-pop, and the flight number only with the preferred option |

## Required changes

1. **[B] Never say "No rain" while rain is drawn, and always name rain near you.**
   - *What's wrong:* in `radar_data.clean()` and `radarClean()`, a surviving blob that isn't (≥ 12 px and moderate+) and isn't ≥ 40 px is flagged UNNAMED. It's drawn but ignored by the words and the cue.
   - *Two consequences:*
     - (a) **A frame whose only echoes are unnamed says `No rain within 50 mi` over visible rain.** By script, a lone 9 px moderate blob 20 mi E is drawn with 9 px and `rain_words()` returns None. Doc 10's own held-out 20 Apr frame (a 9 px echo at 38 mi E) is this case.
     - (b) **A young cell right over you is unnamed.** A 10 px moderate blob at 1 mi gives no `Raining here` and no cue. The picture shows rain on the you-dot while the strip says `No rain`. Monsoon cells start that small, and this is the moment the screen exists for.

     This is the exact lie 07 says the radar never tells.
   - *Do:*
     - Name **every** surviving blob within `RADAR_NAME_ALWAYS_MI` (e.g. 5 mi) regardless of size. The size rule exists to stop distant specks steering the headline, not to hide rain on you. This also restores the `Raining here ›` cue.
     - When only unnamed blobs remain, the strip says `Small echoes only` (MUTED), not `No rain within 50 mi`. Say "No rain" only when nothing is drawn, and key the frame-bar and backfill decision to "anything drawn".
     - Host tests:
       - a 10 px moderate blob at 1 mi gives `Raining here` plus the cue
       - a lone 9 px echo at 38 mi is not `No rain`
       - the monsoon storm is still the headline over the speck

2. **[A] The idle cap is defeated by returns from the card.**
   - *What's wrong:* `main.cpp:62` restamps `g_mapTouchMs` on every entry to the map, including the return from a card (the 08 rule "the idle timer restarts"). `mapIdleExpired(now, lastTouch, willPop)` takes one timestamp. Wired to `g_mapTouchMs`, as the "still to land" list implies, the 300 s cap restarts on every pop. Under a PHX bank the untouched map holds the screen indefinitely, which is round-1 item 4 again. The host test never pops, so it can't catch this.
   - *Do:*
     - Keep two stamps: `g_mapTimerMs` (touch **or** card return; drives the 120 s timer) and `g_mapLastTouchMs` (real touches only; drives the 300 s cap).
     - Change the signature to `mapIdleExpired(now, timerStart, lastTouch, willPopFocus)`.
     - Host test: a will-pop focus, a pop every 90 s and no touch go to weather 300 s after the last real touch.
     - Apply the same rule to the radar's 120 s idle.

## Nice to have

1. **[B] Cadence ignores the radar's own evidence.**
   - `wetHint()` (`net_task.cpp:80`) looks only at Open-Meteo. Arizona monsoon cells often pop up unforecast, so the cue then runs on the 30-min dry cadence.
   - OR in "the newest frame has named rain within 60 mi" to switch to the 10-min cadence.
   - Also write the cue's maximum frame age (the in-flight `RADAR_CUE_MAX_AGE_MIN`) into doc 10 with its value, so a cue is never drawn from a frame older than it.
2. **[B] Doc 10's state table disagrees with the mock.** It says the monsoon's 7 px speck "is drawn but not named". In the mock it's **dropped** by the mask-proximity rule: no wet pixel near (209,113) survives `clean()`. The outcome is right and the sentence is wrong; change it to "dropped (≥ half within 2 px of the clutter mask)". The synthetic host test for an unnamed speck is fine as it is.
3. **[A] Watch item for the device: hidden planes leave a gap.** A focus tag in the arrival stream hides 4 planes in `map-cycle-1` and `z2-busy`, leaving an unexplained gap. `k of N here` still counts them, which helps. If it confuses on the device, a 1 px `M_DIM` notch on the backing's edge per hidden plane is the cheapest hint. Don't add it pre-emptively.
4. **[B]** `net_task.cpp`: delete the stale comment line above the deferral block ("or one is inside the exit radius"), so the next agent doesn't restore the 4.5 nm rule from it.

## What works
- **The firmware caught up without drifting.**
  - `mapStripRight`, `mapTap`, `mapIdleExpired`, the per-blob clean, the deferral and the Traffic rename are all in the tree.
  - Host tests pass, and the full 320×240 planar TIFF matches the Python reference with 0 misses.
  - The "still to land" list is honest about what isn't there.
- The busy map is clean:
  - tag-sized backings
  - no fragments
  - callouts only when needed
  - the descending stream shows its ticks
- The anchored cycle is right, and it survives polls and bounce. It was my bug, and it's properly fixed.
- `radar-monsoon` leads with the real storm. Stale words are MUTED, the partial-coverage chip stays on the age rule, and `Bad radar data · retrying in 30 s` reads at a glance.
- The weather cue: its own slot, capitalised, and the condition kept. It uses the same naming rule as the strip.
- `Traffic` versus `Radar` is now unambiguous on every screen and in the firmware.
- Across three rounds the team corrected its own numbers (clutter), threw out a bad test frame, and validated on held-out data. That's why this radar can be trusted once item 1 lands.
