# 03 — Firmware Architecture

> Audience: agents writing or modifying firmware. Explains module boundaries,
> threading, and the screen state machine. Behavior rules live in
> `01-product-spec.md`; pixel layouts in `06-ui-spec.md`.

## Stack
- PlatformIO, `espressif32`, `esp32dev`, Arduino framework.
- Libraries (pinned in `platformio.ini`): `bodmer/TFT_eSPI`,
  `PaulStoffregen/XPT2046_Touchscreen`, `bblanchon/ArduinoJson@^7`.
- No async web server in v1 (keeps RAM for TLS). OTA via `ArduinoOTA` is
  optional behind `ENABLE_OTA`.

## Directory layout
```
SkyDesk/
├─ platformio.ini            # board, libs, TFT_eSPI pin flags (no User_Setup.h)
├─ AGENTS.md                 # rules for agents (read first)
├─ README.md                 # human quick start + on-device acceptance
├─ include/
│  ├─ config.h               # every threshold, timing, palette, location default
│  ├─ secrets.h.example      # WiFi + optional location/OTA (secrets.h is gitignored)
│  └─ *.h                    # one header per module
├─ firmware/                 # src_dir
│  ├─ main.cpp               # setup/loop, screen state machine, touch routing, backlight, LED
│  ├─ app_state.cpp          # shared snapshots + mutex (net task <-> UI)
│  ├─ net_task.cpp           # core 0: WiFi, NTP, poll schedule, route lookups
│  ├─ http_json.cpp          # one HTTPS GET streamed into ArduinoJson with a filter
│  ├─ adsb_client.cpp        # adsb.fi / adsb.lol fetch + failover
│  ├─ adsb_parse.cpp         # readsb JSON -> Aircraft[] (pure, host-tested)
│  ├─ route_client.cpp       # adsbdb lookup + LRU cache
│  ├─ route_plausible.cpp    # "schedules lie" check (pure, host-tested)
│  ├─ weather_client.cpp     # Open-Meteo fetch + WMO code mapping
│  ├─ geo.cpp                # haversine, bearing, elevation, cross-track (pure, host-tested)
│  ├─ tracker.cpp            # ENTER/EXIT, dwell, grace, featured plane (pure, host-tested)
│  ├─ aircraft_names.cpp     # ICAO type table, operator shortening, header labels
│  ├─ settings.cpp           # NVS: facing direction, night dim, touch calibration
│  ├─ touch_input.cpp        # XPT2046 polling -> tap / release-long-press / hold
│  ├─ ui_common.cpp          # fonts, drawNumber, degree ring, glyphs, weather icons
│  ├─ ui_screens.cpp         # sprite allocation, wipe, time formatting, boot screen
│  ├─ ui_weather.cpp         # Weather screen
│  ├─ ui_plane.cpp           # Plane screen (dome, LOOK, route, stats)
│  └─ ui_setup.cpp           # Settings menu, Facing dial, touch calibration
├─ test/host/                # g++ host tests: sh test/host/run.sh
└─ docs/                     # this series + mockups + design review
```

## Threads
```
          core 0                                   core 1
┌──────────────────────────┐   AppState (mutex)  ┌──────────────────────────┐
│ netTask (FreeRTOS, 16 KB)│ ──────────────────► │ loop()  every UI_TICK_MS │
│  wifiEnsure()            │   snapshot copy     │  touch → events          │
│  every 5s/2s: adsb poll  │ ◄────────────────── │  tracker.update(snapshot)│
│  lookups for near planes │   UI requests       │  screen state machine    │
│  every 10m: weather      │   (flags)           │  draw only dirty regions │
└──────────────────────────┘                     └──────────────────────────┘
```
- The net task **owns** all HTTP. It writes results into `AppState` under a
  mutex and bumps a `version` counter per data kind.
- The UI copies a snapshot under the mutex (fast memcpy of POD structs), then
  releases it before drawing. Never hold the mutex while drawing or doing HTTP.
- The UI tells the net task its current screen (`uiScreen` field) so the poll
  cadence can switch between 5 s and 2 s.

## Shared state (POD, fixed-size — no `String` inside)
```cpp
struct Aircraft {            // one row from the ADS-B feed, enriched
  char hex[7]; char callsign[9]; char reg[9]; char type[5];
  char desc[24]; char ownOp[24];
  float lat, lon; int32_t altFt; bool onGround;
  float gsKt, track; int16_t vRateFpm; char category[3];
  float seenPos;
  // computed by net task after parse (geo.cpp):
  float distNm, azDeg, elDeg;
};
struct TrafficSnapshot { Aircraft ac[MAX_AIRCRAFT]; uint8_t n; uint32_t fetchedMs;
                         bool ok; uint8_t failStreak; uint8_t provider; };
struct RouteInfo {           // from adsbdb, cached by hex
  char hex[7]; char flightLabel[10]; char airline[28]; char typeName[28];
  char origIata[4], destIata[4]; char origCity[20], destCity[20];
  float origLat, origLon, destLat, destLon; bool hasRoute, plausible, found;
};
struct WeatherSnapshot { ... current, hourly[8], today hi/lo, sunrise/sunset ... };
```
`MAX_AIRCRAFT = 40` (~40 × 120 B ≈ 5 KB). If the feed has more, keep the 40
closest.

## Screen state machine (runs on the UI side, `tracker.cpp` + `main.cpp`)
```
            ┌───────────┐ WiFi+first weather ┌──────────┐
  power ───►│   BOOT    │ ──────────────────►│ WEATHER  │◄──────────────────┐
            └───────────┘                    └────┬─────┘                   │
                                   any plane ENTERs│                         │
                                                   ▼                         │
                                             ┌──────────┐  none qualify &    │
                                             │  PLANE   │  dwell ≥ 12 s  ┌───┴──────┐
                                             │ (featured│ ──────────────►│DEPARTING │
                                             │  plane)  │◄────────────── │ grace 4 s│
                                             └──────────┘ plane re-ENTERs└──────────┘
```
- `tracker.update()` is pure logic over a snapshot + `millis()`; it returns
  `{screen, featuredHex, extraCount}`. Keep it free of drawing so it's testable.
- Dismissed hexes (long-press) go in a small set; they can't re-trigger until
  they EXIT.
- Featured-plane hysteresis: switch only if a challenger's `el` exceeds the
  current one's by ≥ 10°.

## Rendering strategy
- Draw each screen's static chrome once on entry (`enter()`), then redraw
  **only** regions whose *displayed string/value* changed (`update()`), comparing
  against a cache of what was last drawn.
- FreeFonts cannot erase-behind like Font 2: TFT_eSPI clears the glyph box and
  then draws, which flashes. So every changing FreeFont region is drawn off-screen
  into a sprite and pushed in one go:
  | Sprite | Size | Depth | RAM |
  |---|---|---|---|
  | Sky dome | 144 × 144 | **16-bit** | ~41 KB |
  | LOOK block (compass word, `42° up`, fists line) | 148 × 98 | 4-bit | ~7 KB |
  | Stats values row | 320 × 22 | 4-bit | ~3.5 KB |
  | Weather clock + next sun event | 120 × 58 | 4-bit | ~3.5 KB |
  The weather hero temperature is **not** a sprite. It only changes when new
  weather arrives (every 10 min), so the body is redrawn directly at that point.
  The LOOK sprite is 148 × 108 (it includes the divider line).
- **Why the dome is 16-bit.** In a 4-bit sprite TFT_eSPI treats `color & 0x0F` as a
  palette index. Its anti-aliased primitives (`drawWideLine`, smooth `drawArc`,
  `fillSmoothCircle`) blend real RGB565 values, which would come out as garbage
  indices. The dome uses those primitives, so it is 16-bit.
- **4-bit text sprites** use only non-blending calls: `fillRect`, `fillTriangle`,
  `fillCircle`, `drawCircle`, and text. Each sprite gets its **own** 16-entry palette,
  built from just the `COL_*` tokens it needs (there are 17 tokens in total, so no
  single palette holds them all). Draw calls pass **palette indices** (`PAL_TEXT`, …),
  not `COL_*` values.
- All sprites are created **once in `setup()`, before WiFi starts** (the dome
  first), so they get contiguous heap. Nothing is allocated per frame.
- Font 2 / GLCD text that changes uses `setTextColor(fg, bg)` + `setTextPadding()`,
  because those fonts do paint their background.
- Screen transitions: an 8-band wipe (`fillRect` bands, ~100 ms), with no full-screen
  sprite (150 KB would not fit). This is the only deliberate blocking in `loop()`.
- **The dome sprite is pushed with `COL_BG` as the transparent key**, so labels drawn
  directly around the dome survive (06 §4.2).

## Memory budget (target, measured with `ESP.getFreeHeap()` on serial)
| Item | KB |
|---|---|
| WiFi + LwIP | ~50 |
| One TLS session (sequential) | ~45 |
| Traffic snapshot ×2 (net + UI copy) | ~10 |
| JSON doc (filtered) | ≤ 12 |
| Sprites (dome 16-bit + 4 text 4-bit, persistent) | ~55 |
| **Must remain free** | **≥ 60** |

## Configuration
- `include/config.h`: every threshold named in `01-product-spec.md`, poll
  intervals, palette, location defaults (`OBS_LAT`, `OBS_LON`, `OBS_ELEV_FT`,
  `VIEW_UP_DEG`, `NTP_TZ`), units.
- `include/secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD`, optional overrides of the
  location macros. Gitignored.

## Logging
Serial 115200. Prefix tags: `[net] [adsb] [route] [wx] [trk] [ui] [touch]`.
Every tracker screen change logs one line with the reason, e.g.
`[trk] WEATHER→PLANE a51f0f SWA1637 d=2.1nm el=38.4`.
