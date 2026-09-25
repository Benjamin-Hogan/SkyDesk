# AGENTS.md

Guidance for AI coding agents working in this repository. Read this first, then
the doc series in `docs/` (index: `docs/README.md`).

## Project summary
SkyDesk is firmware for an ESP32 **CYD** (Cheap Yellow Display, ESP32-2432S028R,
CYD2USB revision; 320×240 landscape). By default it shows **weather**
(Open-Meteo). When an aircraft passes close overhead (ADS-B from adsb.fi, with
adsb.lol as fallback), it switches **automatically** to a **plane card**. The card
says where to look in the sky, what the aircraft is, and where it's going (adsbdb).
When the plane leaves, the display goes back to weather. C++, Arduino framework,
PlatformIO. All data sources are keyless.

## Where things are decided
| Question | Source of truth |
|---|---|
| What should it do? thresholds? | `docs/01-product-spec.md` and `include/config.h` |
| Pins, drivers, RAM limits | `docs/02-hardware.md` |
| Threads, modules, sprites, memory | `docs/03-architecture.md` |
| API URLs, fields, quirks | `docs/04-data-sources.md` |
| Azimuth/elevation math | `docs/05-sky-geometry.md` |
| Pixel layout | `docs/mockups/screens.py` (executable) and `docs/06-ui-spec.md` (rules) |
| Why the UI looks like this | `docs/07-design-review.md`. **Don't regress the listed decisions.** |
| Plane map (v2) | `docs/08-plane-map.md`, `docs/mockups/map_screen.py`, `tools/basemap/make_basemap.py` |

## Build & verify
- Compile: `pio run` (no device needed). This is the default check for every change.
- Host logic tests: `sh test/host/run.sh` (g++). Covers geo, tracker state
  machine, readsb parsing on real captured payloads, route plausibility, and
  header labels. Run it after touching any of those modules.
- Basemap: `python tools/basemap/make_basemap.py` regenerates `firmware/basemap_data.cpp`
  (a generated file; don't hand-edit it). Run it after changing the observer location.
- Mockups: `python docs/mockups/screens.py final` renders every screen with the
  real TFT_eSPI fonts into `docs/mockups/png/`. Run it after any UI change. Update
  `screens.py` **first**, then port to `firmware/ui_*.cpp`.
- Flash: `pio run -t upload`. Only do this if a human confirms the board is plugged in.
- Serial: `pio device monitor` (115200). Tags: `[net] [adsb] [route] [wx] [trk] [ui] [touch] [geo]`.
- Hardware behavior (touch, colors, real aircraft) can only be verified by the
  human on-device. Don't claim more than "compiles and host tests pass".

## Hardware constraints (short version, see docs/02)
- Keep `ILI9341_2_DRIVER` + `TFT_INVERSION_ON` + `TFT_RGB_ORDER=TFT_BGR` in
  `platformio.ini`. Don't touch the TFT pins (12/13/14/15/2, BL 21) or the touch pins
  (25/32/39/33). Construct the touch driver **without** its IRQ pin.
- ~520 KB RAM and no PSRAM. The dome sprite (16-bit, ~41 KB) and the text sprites
  (4-bit) are allocated once in `setup()`, **before WiFi**. Never allocate a
  full-screen sprite. One TLS connection at a time.
- `loop()` must not block. The only exception is the ~100 ms screen wipe.

## Code conventions
- All networking lives in `net_task.cpp` (core 0). The UI (core 1) only reads
  snapshots via `app_state`. Never do HTTP from `loop()`.
- ArduinoJson v7 (`JsonDocument`) with **filters**, streamed from
  `http.getStream()` with `useHTTP10(true)`.
- Pure logic (geo, tracker, adsb_parse, route_plausible, aircraft_names) must
  stay free of hardware and network calls, so the host tests keep working.
- Drawing: only fonts from `setFont(Font::…)`. **Never put non-ASCII in a string**;
  use the primitive helpers (`drawDegreeRing`, `drawArrowRight`, `drawSep`…) and
  `drawNumber()` for fsb9 numbers. On 4-bit sprites, pass `Pal` indices and
  don't use anti-aliased calls (`drawWideLine`, smooth `drawArc`).
- Colors come from `COL_*` in `config.h`. Keep them in sync with `screens.py`.
  `COL_PLANE` (amber) is reserved for the aircraft and "look here".
- Secrets go in `include/secrets.h` (gitignored). Never commit real WiFi credentials.

## Privacy & politeness
- Never display a GA aircraft owner's name. Registries list private individuals.
- Respect the provider rate limits in docs/04: ≥ 2 s between ADS-B polls, one
  adsbdb lookup per new aircraft, cached, with exponential backoff on failure.
- `api.airplanes.live` blocks unregistered clients. Don't switch to it without the
  owner arranging access.

## Things to avoid
- Adding libraries without a reason. The async web server libs are deliberately
  absent, to save RAM for TLS.
- Changing a threshold in code instead of in `config.h`.
- "Simplifying" a UI rule listed in `docs/07-design-review.md` without re-running
  the mockups and noting why.
