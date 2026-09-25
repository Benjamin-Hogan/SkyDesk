# 01 — Product Spec: SkyDesk

> Audience: AI coding agents and humans working on this repo. This is the
> source of truth for *what* the device does. `03-architecture.md` says *how*.
> If code and this doc disagree, raise it — don't silently pick one.

## One-line pitch
A desk display (ESP32 "Cheap Yellow Display") that shows **the weather** by
default and, **when an aircraft passes close overhead, automatically switches to
a plane card** telling you *what it is*, *where it's going*, and *where to look
in the sky*. When the plane leaves, it switches back to weather.

## Users & context
- One person at a desk / kitchen counter, glancing at the screen from
  0.5–2 m. Must be readable at a glance; nobody reads paragraphs on a 2.8" LCD.
- The "aha" moment: you hear a jet, glance at SkyDesk, it already says
  *"Southwest 737 · Phoenix → Denver · look NE, 40° up"*, you look out the window
  and find it.
- Default install location: Gilbert, AZ (under the Phoenix Sky Harbor / Mesa
  Gateway / Chandler traffic flows). Location is configurable.

## Screens (see `06-ui-spec.md` for pixel layouts)
| Screen | When shown | Purpose |
|---|---|---|
| **Boot / Connecting** | Power-on until first WiFi + first data | Progress + actionable error if WiFi fails |
| **Weather** (default) | No qualifying aircraft | Time, current conditions, today hi/lo, next-hours strip, "traffic nearby" chip |
| **Plane** | A qualifying aircraft is overhead | Identity, route, sky-pointer (compass + elevation), altitude/speed/distance |

There is no manual navigation required for normal use. Touch is a convenience.

## Core behavior: the overhead trigger

### Definitions
- **Observer**: the configured home lat/lon/elevation (`config.h` / `secrets.h`).
- **Ground distance `d`**: great-circle distance observer → aircraft (nm).
- **Elevation angle `el`**: angle above the horizon the observer must look
  (degrees; 90 = straight up). See `05-sky-geometry.md`.
- **Azimuth `az`**: compass bearing observer → aircraft (0 = N, clockwise).

### A plane "qualifies" (ENTER) when ALL are true
1. Position is fresh: `seen_pos` ≤ 15 s.
2. Airborne: `alt_baro` is numeric (not `"ground"`) and ≥ 300 ft AGL.
3. Close: `d ≤ ENTER_RADIUS_NM` (default **3.0 nm**).
4. High enough in the sky to find: `el ≥ ENTER_MIN_ELEV_DEG` (default **25°**).

### It stops qualifying (EXIT) when ANY is true
1. `d > EXIT_RADIUS_NM` (default **4.5 nm**) — hysteresis vs. ENTER.
2. `el < EXIT_MIN_ELEV_DEG` (default **15°**).
3. No position update for `LOST_TIMEOUT_S` (default **20 s**).

### Screen switching rules
- Weather → Plane: as soon as ≥1 plane qualifies.
- Plane → Weather: when no plane qualifies **and** the plane screen has been up
  for at least `MIN_PLANE_DWELL_S` (default **12 s**). Prevents flashing a plane
  for 2 seconds then vanishing.
- Plane → Weather transition is delayed by a short **"departing" grace**
  (`DEPART_GRACE_S`, default **4 s**) during which the card stays but shows the
  plane is leaving (arrow/ghosted). Avoids flapping when one poll drops a plane.
- While on the Plane screen, the **featured** plane is the qualifying plane with
  the highest elevation angle. The featured plane only changes if another plane
  beats it by ≥ 10° elevation (hysteresis) — no ping-ponging between two jets.
- If >1 plane qualifies, the card shows a small "+N more" badge; tapping the
  card cycles through them (manual choice sticks until that plane exits).

### Polling cadence
| State | ADS-B poll | Why |
|---|---|---|
| Weather screen | every **5 s** | Catch planes before they're overhead (a 250 kt jet covers 0.35 nm in 5 s) |
| Plane screen | every **2 s** | Smooth sky-pointer; respect ≥1 s provider limit |
| Weather data | every **10 min** | Weather changes slowly; Open-Meteo updates every 15 min |
| Route/type lookup | once per new hex+callsign, cached | Metadata rarely changes |

## What the Plane screen must answer (priority order)
1. **Where do I look?** 8-point compass word (no azimuth degrees; a bearing
   next to an elevation gets misread) + elevation shown as `42° up`, + a sky-dome
   pointer. This is the #1 job; it must be the largest element.
2. **What is it?** Airline/operator + aircraft type in plain words
   (e.g. "Southwest · Boeing 737-700"), not just `B737`.
3. **Where is it going?** Origin → destination (IATA + city). If the route is
   unknown or fails the plausibility check, say so honestly.
4. **Supporting numbers**: altitude, ground speed, distance, climbing/descending.
   Direction of travel is shown only as the trail arrow on the dome — never as a
   second compass word (users will look the wrong way).

## Route honesty
Callsign → route databases are *schedules*, not truth. Southwest-style
multi-leg flight numbers are frequently wrong (observed: `SWA1637` reported as
PIT→TPA while descending into PHX). Rule:
- Run a **plausibility check** (see `04-data-sources.md` §Route plausibility).
- Plausible → show route normally.
- Implausible → show route dimmed with "route unverified", or hide it entirely
  if either airport is missing.
- No callsign / GA / military → show "Private / GA" or operator instead of a route.

## Weather screen content
- Clock (HH:MM, 12h by default), date.
- Current temp (big), condition icon + word, feels-like, humidity, wind
  (speed + compass), today hi/lo.
- Next ~6 hours strip: hour, icon, temp, precip %.
- Sunrise/sunset next event.
- **Traffic chip**: count of aircraft within the wider poll radius, e.g.
  "✈ 4 nearby", plus the nearest one's bearing. Signals the radar is alive.
- Status: WiFi / data staleness indicator when something is wrong (hidden when
  healthy — no permanent clutter).

## Touch (optional conveniences)
| Where | Gesture | Action |
|---|---|---|
| Plane screen | tap | Cycle to next qualifying plane (if "+N") |
| Plane screen | press 1–3 s, release | Dismiss this plane → back to weather (it won't re-trigger until it exits) |
| Weather screen | tap traffic chip | Force-show the nearest plane even if not overhead, for 20 s |
| Anywhere | hold 3 s | Settings: facing direction, touch calibration, brightness |

## Failure behavior (never show a blank or frozen screen)
| Failure | Behavior |
|---|---|
| WiFi down at boot | Boot screen shows SSID, retry countdown, and hint |
| WiFi drops later | Keep last screen; status dot turns amber; retry in background |
| Weather API fails | Keep last-good data; show "updated 42m ago" when > 30 min stale |
| ADS-B API fails | Fall back to the secondary provider; traffic chip shows "radar offline" after 3 failures |
| Route lookup fails | Plane screen still shows type/position; route line says "route unknown" |

## Non-goals (v1)
- No map rendering / tiles.
- No local ADS-B receiver (RTL-SDR) — network feeds only. (Architecture should
  make a local `readsb` JSON URL a drop-in provider later.)
- No accounts, cloud, or phone app. No sound (CYD has no speaker by default).
- No historical logging.

## Acceptance criteria (for the human testing on-device)
1. Power on → weather within 20 s of WiFi connect.
2. Plane passes within 3 nm & ≥25° → plane screen within ≤ 6 s.
3. The sky-pointer direction matches reality (verify with a phone compass).
4. Plane screen never flickers between two aircraft.
5. Plane leaves → weather returns within ~10 s of the exit condition.
6. Pull WiFi → device keeps showing last data with a visible stale indicator,
   recovers automatically.
