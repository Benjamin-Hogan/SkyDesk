# 05 — Sky Geometry: "where to look"

> Audience: agents implementing `firmware/geo.cpp` and the sky-pointer widget.
> All angles in degrees at the API boundary, radians internally.

## Inputs
- Observer: `OBS_LAT`, `OBS_LON`, `OBS_ELEV_FT` (ground elevation, e.g. Gilbert ≈ 1,240 ft MSL).
- Aircraft: `lat`, `lon`, altitude `h_ac` (ft MSL). Use `alt_geom` if present,
  else `alt_baro` (pressure altitude; error of a few hundred ft is irrelevant
  for pointing).

## 1. Ground distance & bearing (haversine)
```
φ1,λ1 = observer; φ2,λ2 = aircraft (radians); R = 6371008.8 m
Δφ = φ2−φ1, Δλ = λ2−λ1
a  = sin²(Δφ/2) + cos φ1 · cos φ2 · sin²(Δλ/2)
d  = 2R · atan2(√a, √(1−a))                      // metres
az = atan2( sin Δλ · cos φ2,
            cos φ1 · sin φ2 − sin φ1 · cos φ2 · cos Δλ )   // → normalise 0..360
```
`d_nm = d / 1852`.

## 2. Elevation angle (with earth curvature)
```
Δh = (h_ac − OBS_ELEV_FT) · 0.3048                // metres above observer
el = atan2( Δh − d² / (2 · R_eff), d )            // R_eff = 4/3·R for refraction
```
If `d < 1 m` → `el = 90`. Clamp to [−10, 90].

Sanity values (observer at 0 ft):
| Aircraft | d | alt | el |
|---|---|---|---|
| Directly overhead | 0 nm | any | 90° |
| Approach traffic | 2 nm | 3,000 ft | ≈ 14° |
| Jet climbing out | 3 nm | 10,000 ft | ≈ 29° |
| Cruise | 3 nm | 35,000 ft | ≈ 62° |
| Cruise | 10 nm | 35,000 ft | ≈ 30° |

(So at the default ENTER thresholds — 3 nm and ≥25° — low approach traffic at
2 nm/3,000 ft does *not* trigger, but cruising jets within 3 nm do. Tune
`ENTER_MIN_ELEV_DEG` per location; near an airport you may want 20°.)

## 3. Compass words
16-point for text, from `az`:
`N NNE NE ENE E ESE SE SSE S SSW SW WSW W WNW NW NNW` — index = `round(az/22.5) % 16`.
The Plane screen uses **8-point only** (N, NE, E…) and never shows azimuth
degrees; a bearing number next to the elevation number gets misread. 16-point is
used only on the Setup → Facing screen.

## 4. "Fists" helper
At arm's length a closed fist spans ≈ **10°**. `fists = round(el / 10)`.
UI says "about 4 fists" (min 1). At `el ≥ 75°` the UI switches to **"UP / overhead"**
and hides the azimuth sector (azimuth is unstable near the zenith).

## 5. Where it's heading on the sky dome (trail)
Project the aircraft forward along `track` at `gs` for `TRAIL_S` (default **60 s**)
using the destination-point formula, recompute `(az, el)` for that point, and
draw an arrow on the dome from now → +60 s (hidden if its shaft would be < 12 px). This tells the user which way to
sweep their eyes.
```
δ = (gs_kt · 0.514444 · t) / R;  θ = track
φ2 = asin( sin φ1·cos δ + cos φ1·sin δ·cos θ )
λ2 = λ1 + atan2( sin θ·sin δ·cos φ1, cos δ − sin φ1·sin φ2 )
```

## 6. Sky dome projection (polar plot)
- Center = zenith (90°), outer ring = horizon (0°). Rings at 30° and 60°.
- Radius: `r = R_dome · (90 − el) / 90` (equidistant, easy to read).
- Angle: screen-up is `VIEW_UP_DEG` (default **0 = north up**). If the owner
  sets `VIEW_UP_DEG` to the compass direction they face when looking at the
  screen, the dome becomes "you-relative": top = in front of you (behind the
  screen), bottom = behind you.
```
θs = (az − VIEW_UP_DEG) in radians
x  = cx + r · sin θs
y  = cy − r · cos θs
```

## 7. Route plausibility math
Cross-track distance of point P from great circle A→B:
```
δ13 = angular distance A→P, θ13 = bearing A→P, θ12 = bearing A→B
dxt = asin( sin δ13 · sin(θ13 − θ12) ) · R
dat = acos( cos δ13 / cos(dxt/R) ) · R       // along-track (sign from cos(θ13−θ12))
```
Thresholds are in `04-data-sources.md` §Route plausibility.

## 8. Test vectors (put these in `test/test_geo` or a serial self-test)
| Observer | Aircraft | Expect az | Expect d | Expect el |
|---|---|---|---|---|
| 33.3528,−111.7890, 1240 ft | 33.3528,−111.7890, 20,000 ft | n/a | 0 | 90 |
| same | 33.4028,−111.7890, 10,000 ft | 0 (N) | 3.0 nm | ≈ 25.6° |
| same | 33.3528,−111.7290, 5,000 ft | 90 (E) | 3.0 nm | ≈ 11.6° |
| same | 33.3028,−111.8490, 35,000 ft | ≈ 225 (SW) | ≈ 4.25 nm | ≈ 52.6° |
