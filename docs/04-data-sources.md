# 04 — Data Sources

> Audience: agents implementing `adsb_client`, `route_client`, `weather_client`.
> All endpoints below were **verified live on 2026-09-24**. All are keyless and
> free for non-commercial use — be a polite client (rate limits, User-Agent,
> caching). Payload examples are trimmed.

## Summary
| Need | Primary | Fallback | Auth | Rate limit (self-imposed) |
|---|---|---|---|---|
| Aircraft near me | **adsb.fi** opendata v2 | **adsb.lol** v2 | none | ≥ 2 s between calls (provider limit 1 req/s) |
| Type / operator / route | **adsbdb.com** v0 | built-in type table | none | 1 lookup per new aircraft, cached |
| Weather | **Open-Meteo** | last-good cache | none | every 10 min |
| Rain radar (v3) | **IEM NEXRAD n0q** composite (WMS-T, TIFF) | keep the previous frame | none | JSON every 2–30 min; one ~300 KB frame when `valid` changes (docs/10) |

> ❌ `api.airplanes.live` was tested and now returns
> `{"error":"Please contact us..."}` for unregistered clients. Don't use it
> without the owner arranging access.

User-Agent for every request: `SkyDesk/<FW_VERSION> (ESP32 CYD)`.
TLS: `WiFiClientSecure::setInsecure()` (as in the sibling project) is accepted
for v1 — these are public read-only feeds with no secrets in the request.

---

## 1. ADS-B: aircraft near a point

### adsb.fi (primary)
```
GET https://opendata.adsb.fi/api/v2/lat/{lat}/lon/{lon}/dist/{nm}
```
Response root key: **`aircraft`** (array). `now` is epoch seconds.

### adsb.lol (fallback)
```
GET https://api.adsb.lol/v2/point/{lat}/{lon}/{nm}
```
Response root key: **`ac`** (array). Same per-aircraft schema (readsb).

→ The parser must accept **either** `aircraft` or `ac`.

Query radius: `POLL_RADIUS_NM` (default **25 nm** since V4, Sky Trails, docs/13; "nearby" counts use `NEARBY_NM` 12 nm; the plane card polls 12 nm). Wider than the ENTER radius
so the Weather screen's traffic chip and "approaching" logic have data.
Observed size near Phoenix: ~5 KB for 9 aircraft within 10 nm (≈ 600 B/aircraft).
Budget for 60 aircraft = ~36 KB streamed; **filtered** doc stays < 8 KB.

### Per-aircraft fields we use (readsb schema)
| Field | Type | Meaning | Notes |
|---|---|---|---|
| `hex` | string | ICAO 24-bit address | Stable key. `~` prefix = non-ICAO (TIS-B) |
| `flight` | string | Callsign, **space-padded** (`"SWA1637 "`) | Trim. May be missing |
| `r` | string | Registration (`N429WN`) | May be missing |
| `t` | string | ICAO type designator (`B737`) | May be missing |
| `desc` | string | Type description (`BOEING 737-700`) | adsb.fi only; uppercase |
| `ownOp` | string | Owner/operator (`SOUTHWEST AIRLINES CO`) | adsb.fi only. **Not parsed since 3.0** (RAM; it names private owners) |
| `alt_baro` | number **or** `"ground"` | Pressure altitude, ft | Check type before reading! |
| `alt_geom` | number | GNSS altitude, ft | Prefer for geometry when present |
| `gs` | number | Ground speed, kt | |
| `track` | number | True track over ground, ° | |
| `baro_rate` / `geom_rate` | number | ft/min | ± → climbing/descending |
| `lat`, `lon` | number | Position | May be missing (no position) → skip aircraft |
| `seen_pos` | number | Seconds since last position | Skip if > 15 s |
| `category` | string | `A1`..`A7`, `B*` | A1 light, A3 large, A5 heavy, A7 rotorcraft |
| `dst`, `dir` | number | Distance (nm) / bearing from query point | Handy, but **recompute** ourselves from our exact observer coords |

**Parsed as a stream, one aircraft object at a time** (`adsbParseStream`, since v2): at an 18 nm
radius, parsing the whole document ran out of heap while TLS was open. Memory use is now
constant. A stream that breaks off mid-array returns −2, and no partial snapshot is published.
The per-object filter uses the same fields as the old whole-document filter:
```cpp
JsonDocument f;
for (const char* k : {"aircraft", "ac"}) {
  JsonObject a = f[k].add<JsonObject>();
  for (const char* fld : {"hex","flight","r","t","desc","alt_baro",
       "alt_geom","gs","track","baro_rate","geom_rate","lat","lon","seen_pos",
       "category"}) a[fld] = true;
}
```

### Provider failover
- 3 consecutive failures (non-200, timeout, JSON error) on primary → switch to
  fallback for 10 minutes, then try primary again.
- Failures on both → Weather traffic chip shows "Traffic offline" ("Radar" means rain, v3); Plane screen
  (if up) keeps last state until `LOST_TIMEOUT_S`, then returns to Weather.

---

## 2. Aircraft metadata & route: adsbdb
```
GET https://api.adsbdb.com/v0/aircraft/{hex}?callsign={CALLSIGN}
```
One call returns both aircraft and route. Example (trimmed):
```json
{"response":{
  "aircraft":{"type":"737NG 7H4/W","icao_type":"B737","manufacturer":"Boeing",
              "registration":"N429WN","registered_owner":"Southwest Airlines"},
  "flightroute":{"callsign":"SWA1637","callsign_iata":"WN1637",
     "airline":{"name":"Southwest Airlines","iata":"WN","icao":"SWA"},
     "origin":{"iata_code":"PIT","municipality":"Pittsburgh",
               "latitude":40.4915,"longitude":-80.2329},
     "destination":{"iata_code":"TPA","municipality":"Tampa",
               "latitude":27.9755,"longitude":-82.5332}}}}
```
- `response` may be the **string** `"unknown aircraft"` / `"unknown callsign"`
  → treat as not found (check `is<JsonObject>()`).
- ⚠ adsbdb returns **HTTP 404 for the whole request** when only the *callsign* is
  unknown (verified: `a51f0f?callsign=ZZZ999` → 404 `"unknown callsign"`), which
  would throw away good aircraft data. `route_client.cpp` retries without
  `?callsign=` on a 404.
- If no callsign, call `/v0/aircraft/{hex}` only.
- Cache: LRU of **16** entries keyed by `hex`, storing only the few strings we
  display (~200 B/entry). Never re-query the same hex+callsign within 6 h.
- Lookups happen only for aircraft within `LOOKUP_RADIUS_NM` (default 6 nm), so
  the card is ready before the plane is overhead.

### Route plausibility (REQUIRED — schedules lie)
Observed live: `SWA1637` returned **PIT→TPA** while the aircraft was descending
westbound into PHX. Mark a route **plausible** only if:
1. The aircraft is within **40 nm** of origin or destination, **or**
2. Cross-track distance from the origin→destination great circle ≤
   `max(60 nm, 0.15 × route length)` **and** the along-track position is between
   −40 nm and route length + 40 nm, **and** the aircraft's `track` is within
   ±100° of the bearing from the aircraft to the destination.

Implausible → UI shows "route unverified" (dimmed) — see `06-ui-spec.md`.
Math is in `05-sky-geometry.md`.

### Display names
- Operator: prefer `flightroute.airline.name`; else `registered_owner`; else
  the airline table (by callsign); else "Private". (`ownOp` is no longer used, since 3.0.)
- Type: `manufacturer` + friendly model from the **built-in type table**
  (`firmware/aircraft_types.cpp`, ICAO designator → "Boeing 737-700"). The
  adsbdb `type` string (`737NG 7H4/W`) is too cryptic for display. Fallback order:
  table → `desc` title-cased → `t` raw.
- Flight label: IATA flight (`WN1637`) if known, else callsign, else registration.

---

## 3. Weather: Open-Meteo
```
GET https://api.open-meteo.com/v1/forecast
    ?latitude={lat}&longitude={lon}
    &current=temperature_2m,relative_humidity_2m,apparent_temperature,
             weather_code,wind_speed_10m,wind_direction_10m,is_day
    &hourly=temperature_2m,weather_code,precipitation_probability
    &daily=temperature_2m_max,temperature_2m_min,sunrise,sunset
    &temperature_unit=fahrenheit&wind_speed_unit=mph
    &timezone=auto&forecast_days=2&forecast_hours=8
```
(no whitespace in the real URL). Response ≈ 2.5 KB. `utc_offset_seconds` in the
response is authoritative for local time display if NTP TZ is unset.

Weather codes (WMO) → icon + label:
| Code | Label | Icon |
|---|---|---|
| 0 | Clear | sun / moon (use `is_day`) |
| 1, 2 | Partly cloudy | sun+cloud / moon+cloud |
| 3 | Overcast | cloud |
| 45, 48 | Fog | fog bars |
| 51–57 | Drizzle | cloud+light drops |
| 61–67, 80–82 | Rain / Showers | cloud+drops |
| 71–77, 85, 86 | Snow | cloud+flakes |
| 95–99 | Thunderstorm | cloud+bolt |

Units are configurable in `config.h` (`UNITS_IMPERIAL 1`).

---

## 4. Rain radar: Iowa Environmental Mesonet (v3)
Full rules, states and pipeline: `10-rain-radar.md`. The short version:
- `GET https://mesonet.agron.iastate.edu/data/gis/images/4326/USCOMP/n0q_0.json`
  → `meta.valid` (the newest composite, ISO UTC) and `meta.radar_quorum` (`"144/147"`).
- `GET https://mesonet.agron.iastate.edu/cgi-bin/wms/nexrad/n0q-t.cgi?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=nexrad-n0q-wmst&STYLES=&SRS=EPSG:4326&BBOX=<w,s,e,n>&WIDTH=320&HEIGHT=240&FORMAT=image/tiff&TRANSPARENT=true&TIME=<valid>`
  → **uncompressed** little-endian RGBA TIFF, 307,886 bytes, **planar** (40 strips of 25 rows), IFD at offset 8.
- Colours are the official n0q ramp (`tools/radar/composite_n0q.txt`, from pyIEM): 255 exact
  colours, 0.5 dBZ apart. Lookups are exact; > 0.5 % misses rejects the frame.
- Archived frames exist back to 2011 (5-min steps), which is how the mockups use real storms.

## Politeness checklist
- [ ] Never poll ADS-B faster than every 2 s; never in parallel.
- [ ] Back off ×2 (max 60 s) on HTTP 429 / 5xx.
- [ ] Cache adsbdb; lookups only for nearby aircraft.
- [ ] Send the User-Agent string.
- [ ] IEM: JSON first; a frame only when `valid` changes; one request at a time; backoff 60 s × 2ⁿ (max 30 min).
