# SkyDesk documentation

Written for AI agents and humans picking up this project. Read in order the
first time.

| # | Doc | Read it when |
|---|---|---|
| 01 | [Product spec](01-product-spec.md) | Always first: behavior, trigger rules, acceptance criteria |
| 02 | [Hardware](02-hardware.md) | Touching pins, drivers, SPI, memory |
| 03 | [Architecture](03-architecture.md) | Adding or changing modules, threads, sprites |
| 04 | [Data sources](04-data-sources.md) | Touching any HTTP client or JSON parsing |
| 05 | [Sky geometry](05-sky-geometry.md) | Azimuth, elevation, dome projection, route checks |
| 06 | [UI spec](06-ui-spec.md) | Any drawing code |
| 07 | [Design review log](07-design-review.md) | Before changing how anything looks |
| 08 | [Plane map](08-plane-map.md) | v2 map screen: navigation, band rendering, basemap, visual rules |
| 09 | [Plane map v3](09-map-v3.md) | v3 map changes: inbound-first focus, leader, tap-again cycle, edge pointer (in review) |
| 10 | [Rain radar](10-rain-radar.md) | v3 radar screen, weather rain cue, IEM TIFF-to-SD pipeline (in review) |
| 11 | [Today's Sky](11-today.md) | 3.0: overhead-pass log, Today page, the weather chip's arbiter, Heads-up promise log |
| 12 | [Setup portal](12-setup-portal.md) | 3.0: phone setup over a WiFi hotspot + QR, location/time zone, the map gates, recovery |
| — | [3.0 feature council](design-review/3.0-feature-council.md) | What 3.0 builds (Today's Sky, setup portal, Heads-up), in what order, and what was cut |

## Mockups
- `mockups/screens.py` is the **executable layout spec**. It draws every screen
  through `mockups/tftsim.py`, a TFT_eSPI look-alike that uses the real bitmap
  fonts and quantizes to RGB565.
- `mockups/geo.py` is the Python mirror of `05-sky-geometry.md`. Mock data is computed
  from real aircraft positions.
- Render with `python docs/mockups/screens.py final`. This writes
  `mockups/png/final-*.png` (1×) and `final-*@3x.png` (nearest-neighbor 3×).
- `mockups/png/r2-*` and `r3-*` are the reviewed rounds. `mockups/r1-*.html` is round 1,
  kept for history.

![overview](mockups/png/overview.png)
