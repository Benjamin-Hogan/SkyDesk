# 13 — Sky Trails (V4)

> **Decision:** `design-review/v4-feature-council.md` (the team: aviation, UI/UX and systems).
> **Owner's call:** a **new screen**, integrated **seamlessly** with the map and Today.
> **Status:** step 1 (the recorder) in build; the screen goes mocks → Mr Stacks (3 rounds) →
> firmware.

## What it is
"What today drew". Every heard aircraft paints its path on a dimmed map as it flies, coloured by
altitude. The day builds up like a long exposure, and one play pill replays it in about 30 s.
- The map stays **"what's here now"**. Trails is **"what today drew"**.
- It is never a history browser: there is no date picker and no list.

## Seamless integration (the owner's requirement)
Trails is a separate screen, but it should feel like a *layer* of the map, not a place you
travel to.
1. **Map ↔ Trails is one tap, the same view.**
   - The map's chrome gains a `TODAY` pill beside the zoom pill.
   - Trails opens at **the same zoom and centre**, with no wipe: the day's paint appears under the
     same planes.
   - Trails has a `LIVE` pill in the same spot that goes back. The zoom pill works on both and is
     shared (one saved zoom).
2. **Today → Trails.** Tapping the hourly panel opens Trails, and tapping a **bar** opens the
   replay paused at that hour. The bars and the canvas are the same day, seen two ways.
3. **The card hands off.** After a card closes, Trails draws that plane's path in amber until the
   next pop. The chip's `Passed … ago` → the map (as now), then one tap to Trails.
4. **The same rules as the map:**
   - The card pops over Trails and returns to it.
   - Idle for 120 s → weather; the `‹` pill → weather.
   - Offline, stale or no-SD states use the map's wording.
5. **No new settings and no new gestures.**

## The screen (mock first; Iris leads the visual system)
- **Canvas:** the basemap is dimmed to about 3 tones. The day's paths are drawn in 4 altitude hues
  (feet MSL: < 3k, 3–8k, 8–15k, > 15k) × 2 brightness levels (the last hour bright, older dim).
- **Live heads:** TEXT. The last popped plane: amber.
- **Top right:** `since 6:00 AM · 214 heard`, then the zoom pill and `LIVE`.
- **Bottom strip:** a play/pause pill, a 24 h timeline (tap to jump) and a 4-step altitude key.
- **Palette:** 15–16 slots, verified in the mock.

## Honesty (Vega)
- Only **fresh ADS-B/MLAT positions** are drawn (`seen_pos` ≤ 15 s). TIS-B (`~` hexes) and ground
  traffic are skipped.
- A gap over 30 s in one aircraft's track **breaks its line**. Nothing is interpolated.
- The wording is "heard", never "flew". If the radius can't reach Sky Harbor, the pill says so and
  the screen doesn't claim the PHX flow.

## Step 1: the recorder (`firmware/trails_log.cpp`, pure encoder + device glue)
- **What is recorded:** every position the net task parses, **before** the 40-aircraft screen
  cap (up to 80), so the distant PHX streams survive busy hours.
- **Radius:** the default ADS-B radius `POLL_RADIUS_NM` becomes 25 nm (Sky Harbor is about 17 nm away).
  The plane card's 2 s polls and the 5 mi map's 3 s polls stay at 12 nm (`NEARBY_NM`) to keep
  fetches short (review). The net log prints `[adsb] fetch avg/max ms, cap80 hits` every minute.
  - The chip's "N nearby" and Today's nearby count stay at 12 nm (`NEARBY_NM`), so nothing the
    owner already sees changes meaning.
  - The map's own plan still widens further at its 20 mi zoom.
- **Format** (`include/trails_log.h`): one file per local day, `/skydesk/trails/YYYY-MM-DD.bin`.
  - A 16 B header: `"SKT1"` (magic and version), `u32` day (yyyymmdd), and the build lat/lon ×1e6
    as `i32`.
  - Then per poll: `{u8 0xA5, u8 n, u16 sec-of-day/2}` followed by `n` points of 8 B each:
    `{u8 icao[3], i16 dx, i16 dy, u8 alt/200 ft}`.
  - `dx`/`dy` are in 0.0005° units from the build centre (about 50 m resolution, about ±16° of
    range). A point outside that range is **skipped, never clamped**.
  - The ICAO address is stored per point (the same 8 B as an id plus flags would take, and no
    12 KB RAM id table).
  - A header torn by a power cut (a file under 16 B) is truncated and rewritten.
- **Size:** about 20 aircraft × 17,280 polls × 8 B ≈ 2.8 MB per day; 80 × 8 B every 5 s is about 11 MB
  worst case. 30 days is under 350 MB. There's no free-space check: on FAT32 that scan would hold
  `sdLock` for seconds.
- **Writes:** buffered per poll (≤ 80 × 8 B) and appended under `sdLock` by the net task **between
  jobs**; no new static buffers over 700 B. Days older than 30 are deleted, oldest first
  (`TRAILS_KEEP_DAYS`).
- **No SD:** nothing is recorded (the screen will say so).

## Measured targets (before the screen)
- 8-bit heap at rest stays ≥ 60 KB with the 25 nm polls, as today's diagnostics line reports it.
- The ADS-B payload at 25 nm stays inside the parse buffer (80 aircraft), and the fetch time stays
  under 2 s.
- **A real day recorded,** then the mocks: 7 AM, 6 PM peak, a monsoon day if possible, replay
  paused, an outage gap, no SD; 1× and night backlight (Iris's mud test).
