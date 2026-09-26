# 11 — Today's Sky (3.0, feature 1)

> Audience: agents and humans building 3.0. Council decision:
> `design-review/3.0-feature-council.md`. Pixel layouts: `mockups/today_screen.py`
> (executable), rendered by `python docs/mockups/screens.py final`. Status: **approved** (Mr Stacks A 8 · B 8,
> round 3), **ported**; on-device checks pending. Rounds: `design-review/3.0-today-round-*`.

## What it is
SkyDesk forgets every plane the moment it leaves. Today's Sky remembers.
- **Missed it?** For 10 minutes after an overhead pass, the weather chip says so:
  `Passed 2 min ago · Southwest 737-800 ›`.
- **What flew over today?** A Today page with overhead passes, planes nearby, an hourly bar chart
  (the Phoenix east/west flow flips show as clusters), the rarest type seen, and the last passes
  with their routes.
- **A log you keep.** One CSV line per overhead pass on the SD card, readable on a computer.
- **The Heads-up gate** (feature 3). It logs every will-pop "promise" against its outcome, so
  Heads-up ships only if its countdown is honest (≥ 80 % kept over 7 days).

## Definitions
- **Overhead pass:** the tracker's ENTER event (docs/01: ≤ 3 nm and ≥ 25° up).
  - The pass stays open while the tracker keeps the plane. It closes on EXIT or lost.
  - The record keeps the **peak** (highest elevation), the time and distance of that peak, and
    the compass word **toward** the plane's track at the peak (where it went).
  - The same hex entering again within `TODAY_MERGE_S` (10 min) re-opens the same pass.
    Pattern-work trainers circle; one plane is one pass. The window is measured in wall-clock time
    (it survives a reboot).
  - A pass pushed out of the 3-row recent list inside its window is parked (3 slots), still
    mergeable, and logged only when its window closes (review B1).
  - At most `TODAY_OPEN` (4) passes are open at once. A fifth simultaneous ENTER is not counted
    anywhere, so the bars, OVERHEAD and the CSV always agree.
  - A pass whose Leave event was lost closes after the plane has been gone from the feed for
    `LOST_TIMEOUT_S` + 10 s.
  - A plane opened with a tap (Forced) is not a pass. Dismissing a card (long-press) does not
    un-count the pass.
- **Nearby:** a distinct airborne aircraft seen within `POLL_RADIUS_NM` (12 nm ≈ 14 mi) during the
  local day.
  - The map's wider poll radius (z2) does not inflate it: it counts `distNm ≤ 12` only.
  - Counted with a 4,096-bit hash set, estimated by linear counting:
    `n = −m·ln(1 − k/m)`. The standard error is about 1 % at 1,500 a day; the host test bounds
    it at 3 %.
- **Day:** local midnight to midnight (the clock's TZ). Nothing is counted before the clock syncs.
- **Rarest today:** among today's *nearby* aircraft with a known ICAO type, the type with the
  fewest past sightings.
  - A sighting is one aircraft on one day; the lifetime counts are on SD.
  - Types seen on 3 or more earlier days don't qualify.
  - For the first `TODAY_LEARN_DAYS` (3) days, the page says `learning` instead of guessing.
  - It needs the SD card.
- **Promise** (for Heads-up):
  - **Made:** the first poll where `mapWillPopSecs(a) > 0` for a plane that isn't already a pass.
    Due = poll time + secs; lead = secs.
  - **Kept:** the hex ENTERs within `max(15 s, 0.3 × lead)` of due, judged against the **first**
    promise made for that hex.
  - **Broken:** it enters outside that window, or the window passes without an ENTER.
  - **Excluded:** the ADS-B feed failed (`failStreak > 0`) or the provider changed while the promise
    was live. Exclusions are counted and reported. (A "dismissed" exclusion was dropped: a card
    can only be dismissed after its ENTER, which already judged the promise.)
  - **One promise per plane:** after a hex's promise is judged, it gets no new promise for
    `TODAY_MERGE_S`. A late plane is one broken promise, not a broken one plus a kept one
    (review B2).
  - The gate is kept ÷ (kept + broken) over the last 7 days, with at least 30 promises.

## Surfaces
### 1. Weather chip: `chipMessage()` (pure, host-tested)
The chip has an arbiter. The first matching row wins. **The chip opens the map in every state**
(round 1, M1: one target, one meaning).

| Priority | State | Left (f2) | Right (f2 MUTED) | Map opens on |
|---|---|---|---|---|
| (3.1) | NWS warning | reserved | | |
| (feature 3) | will-pop | reserved: `overhead in ~40 s` | | that plane |
| 1 | traffic offline | `Traffic offline` WARN | `retrying in N s` | the map's offline state (no chevron, as in v2) |
| 2 | **passed, ≤ 10 min** | `Passed 2 min ago` TEXT | `Southwest 737-800 ›` | **the passed plane** |
| 3 | nearby | `4 nearby` TEXT | `737-800  5.2 mi W ›` | nearest, as today |
| 4 | quiet | `0 nearby` | `quiet skies` | as today |

Passed-state details:
- The age and the 10-min window count from the pass's **close** (EXIT/lost), not its peak. A
  hovering helicopter can peak minutes before it leaves (round 1, S3). Under 1 min, it says
  `Passed just now`.
- The right side fits, with the same chain as the Today rows: operator + type, then code + type
  (`BA A350-1000`, well-known IATA codes only), then type, then the ICAO designator.
- The glyph stays amber (it's the chip's icon, not a focus mark).
- **Tap → map, focused on the passed plane** (the **chip-passed** focus tier, docs/09 M1).
  - Focus order: selected > will-pop > just popped (M7, 30 s) > chip-passed > nearest.
  - Chip-passed is entered only by a chip tap. It uses M7's tag (`passed · 6.1 mi NE`) and M4's
    pointer when the plane is off the map.
  - **If the hex has left the traffic** (a departing jet leaves the 12 nm poll in about 3 min), the
    map opens on its normal focus. Renders: `map-from-passed`, `map-from-passed-gone`.
  - The map strip opens the plane's **live** card, with its route. That's the "replay", with no
    frozen-card mode (the council's replay is withdrawn).

### 2. The Today page (new screen `Scr::Today`)
**Entry: the weather header row, y 0–36, full width** (round 1, M3).
- After the date, a `drawSep()` dot, then `31 overhead ›` in f2 MUTED, which is also a glance
  stat.
- The header's right side stays reserved for degraded status (R1-15).
- **`headerEntryFit()`** (chip.cpp, host-tested) keeps the entry clear of the status by 12 px:
  `104 overhead`, else `104` (or `1.2k`), else the text is hidden and the target stays.
- The hero band that opens the radar is now **y 36–112** (it was 30–112). The clock is not a
  target; the rain cue sits well inside the radar band.
- Before the clock syncs, the entry's text is hidden but the target stays live. The page then
  explains itself (`Waiting for clock`).

**Layout** (320×240; see the mock):
- **Top chrome:** the `‹` pill (4,4,36,24), `Today` in fsb12. The right side shows the date in f2
  MUTED, or the worst degraded status in WARN with a dot: `Traffic offline` >
  `No SD card: no log`.
- **Stats row**, three cells, each a glcd DIM label above an fsb18 value:
  - `31` OVERHEAD
  - `212` WITHIN 14 MI: `1.2k` at 1,000 or more. When the hash set was lost (a reboot without
    SD), re-seen planes may count twice, so the value is **at most** this. It shows as `~212` in
    MUTED with `since reboot` in glcd DIM under it. `--` MUTED before the clock syncs.
  - RAREST: the type (fsb18 → fsb12 → fsb9), or one fs9 MUTED state:
    - `learning` (with glcd `day 2 of 3` under it)
    - `needs SD`
    - `none new`: nothing qualifies today
    - `waiting`: clock not synced
- **Hourly panel** (PANEL, 308 × 64), titled `PASSES BY HOUR  -  PEAK 6` in glcd DIM. The peak is
  folded into the fixed title: no floating label, no collision.
  - 24 bars of overhead passes per hour, each 11 px with a 1 px gap. Heights are scaled to
    max(peak, 4), 30 px max. Past hours are MUTED; the current hour is TEXT.
  - A 2 px TEXT **now tick** under the current hour, even at zero.
  - An hour with more than 5 min of traffic outage keeps its bar and gets a **3 px DIM checker
    stipple** under it: not watching is not quiet, and the bars still add up to OVERHEAD.
  - Hour labels `12A 6A 12P 6P` are glcd **MUTED**, so they survive the night backlight.
- **Last passes:** up to 3 rows, newest first. They **survive midnight** (they're "last", not
  "today's"); only the counts and bars reset.
  - Time is `7:21` in f2 MUTED plus `PM` in glcd DIM. A row from an earlier day draws its time in
    **DIM**, and the heading becomes `LAST PASSES  -  SINCE YESTERDAY`.
  - The words are f2 TEXT. The right column is f2 MUTED, at least 12 px clear: the route (drawn
    arrow), **only if `routePlausible`**, else the registration, else the callsign.
  - One font on every row. Fit chain: `Operator Type` + right, `Code Type` + right (the IATA
    airline code, `BA A350-1000`), `Operator Type`, `Type` + right, `Type`.
  - Rows are not tappable (no chevrons).
  - No owner names, ever: labels come from `planeLabels()`.
- **Empty list:** `No overhead passes yet today`, or `Waiting for clock` before sync.

**Behaviour:**
- It redraws **per cell**: the stats cells when their text changes; the bars and the list on pass
  events only. No full-screen redraw on a poll (round 1, S4).
- Idle 120 s → weather. The card still pops over it and returns to it.

### Rarest: exact rules
- **Candidates:** today's nearby aircraft with a real ICAO type. Excluded: blank, `TWR`, `GND`,
  `GRND`, and category `C*` (ground vehicles and obstacles).
- **Past record:** a type seen on 3 or more earlier days is out. Among the rest, the fewest past
  sightings wins.
- **Ties:** a type that passed overhead today beats one that didn't; then the most recent wins.

## Storage
| What | Where | When written |
|---|---|---|
| Pass log `YYYY-MM-DD.csv`: time, hex, callsign, reg, type, operator, orig, dest, peak_el, peak_mi, toward | SD `/skydesk/log/` | on pass close, batched by the net task |
| Promise log `YYYY-MM-DD-promises.csv`: made, hex, lead_s, outcome, err_s | SD `/skydesk/log/` | on resolve, batched |
| Day summary `days.csv`: date, overhead, nearby, rarest, kept, broken, excluded | SD `/skydesk/log/` | at midnight |
| Lifetime type counts `types.bin`: ≤ 1,024 × {type[4], days u16, lastDay u16} | SD `/skydesk/` | batched by the net task |
| Today summary blob: date, counts, hourly[24], rarest, last 3 passes, promise counts, 7-day promise ring | NVS `today` | at most once a minute for passes, promises or rarest; every 10 min for the nearby count alone (flash wear; each erase stalls both cores) |
| Nearby hash set (512 B + its day) | SD `/skydesk/today.bit`, overwritten in place | every 5 min when changed, and right after midnight. A set from another day is never restored |

- The owner field (`registered_owner`) and adsb.fi's `ownOp` are **never written**. The operator
  comes from `planeLabels()` and is blank for GA.
- CSV text is ASCII; commas in names are replaced by spaces.

## Architecture
- **`firmware/spotter.cpp`** (pure, host-tested): passes, nearby set, hourly bars, promises and
  the recent list.
  - It is fed on core 1 by the traffic snapshot and new tracker pass events
    (`trackerTakePassEvent`), and by labels from the glue.
  - It emits an **outbox** of pass lines, promise lines and type queries.
- **`firmware/today_store.cpp`** (device): the net task drains the outbox **between jobs** under
  `sdLock`. It appends CSV lines, updates `types.bin` and answers type queries back to the
  spotter (for rarest). It also saves the NVS blob and the hash set, and restores both at boot.
  - The outbox is a fixed ring behind a mutex. On overflow the oldest line is dropped and counted.
    A failed write is counted too. Nothing blocks the UI.
  - It runs through WiFi outages too, so the log keeps saving.
  - After 3 failed SD writes in a row, Today stops using the card until a reboot (a pulled card
    must not stall the net task on SdFat timeouts). "Learning" counts only days with the log
    running.
  - `types.bin` survives a torn record: an append always lands on the 12-byte grid.
- **`firmware/ui_today.cpp`**: the page. `ui_weather.cpp` gets `chipMessage()`.
- **RAM (measured):** static V2 = 91,480 B; 3.0 = 87,088 B, so **−4,392 B**.
  - The first 3.0 build was +2 KB and failed every TLS connection on the device. See docs/10 →
    *3.0 field lessons*.
  - Paid for by `ROUTE_CACHE_N` 16 → 8 (in-radius entries are never evicted), dropping the
    `ownOp` field (2 × 40 × 28 B of traffic snapshots), and stack instead of static copies.
  - **On the device** (6-min run with a radar frame): 8-bit free ~70 KB at rest (V2: 63–66),
    heap minimum 5.8 KB (V2: 3.5), 0 TLS failures. Stack headroom: net 8.5 KB, loop 5.4 KB.
- **Flash:** +18.4 KB measured. **Network:** none.

## Tests (host) - `test/host/test_today.cpp`
- **Passes:** ENTER → EXIT makes one pass with the right peak, and closes after the peak (age from
  close). Re-entry within 10 min merges. Forced is not a pass. The CSV line is held until the merge
  window closes and is logged once.
- **Midnight:** the counts and bars reset, **the last passes survive**, a `days.csv` line is
  written, and the promise ring rolls.
- **Rarest:** a common type (≥ 3 earlier days) is never rarest (`none new`); the fewest sightings
  win; ties go to the newest; junk codes (`TWR`, category `C*`) never become candidates.
- **Header entry:** `headerEntryFit()` at 31 with no status, and at 0, 104 and 1,204 against
  `Updated 47 min ago`, plus the no-room case.
- **Nearby:** a fixture over 1,500 synthetic hexes stays within 2 %. The 12 nm filter holds.
  Ground traffic is ignored.
- **Promises:** kept / early / late / timeout / failover-excluded, one promise per plane (a late plane), the first
  promise wins, and the 7-day gate.
- **Chip arbiter:** the priority order, the 10-min expiry, `just now`, and the fit fallbacks.
- **CSV:** privacy (the owner never appears, even when the fixtures carry `ownOp`), and commas
  escaped.

## Device checks (human)
1. After a real pass, the chip reads `Passed … ago`. Tapping it opens the **map** focused on that
   plane (or the normal map if it has left). Tapping the header opens **Today** with the pass on
   top.
2. `/skydesk/log/<today>.csv` on a computer has the line.
3. After a reboot the counts and the last passes are still there.
4. 8-bit heap before and after (the `[net] heap` log or the diagnostics line): ≥ 60 KB free at rest.
