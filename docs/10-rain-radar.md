# 10 — Rain Radar (v3 feature)

> Audience: agents building the radar screen, its data path, and the weather-screen rain cue.
> Status: **approved (Mr Stacks: A 8/10 · B 8/10, round 3), round-3 items applied; implemented** (`design-review/v3-*`). Depends on the **SD card** (see *Hardware*).
> Mockups: `docs/mockups/radar_screen.py` + `radar_data.py`, rendered by
> `python docs/mockups/screens.py final radar` and `... final weather`. **Every rain pixel in the
> mocks is real archived NEXRAD data**, classified exactly the way the firmware classifies it
> (the host test `testRadar` checks the firmware against the Python reference pixel for pixel).

## What it is
A north-up rain map, about 50 mi around you, with the **last 50 minutes looping**. It answers
"is rain coming, how far is it, and how heavy?" In Gilbert it rains on about 30 days a year, so
the weather screen tells you when there's something worth opening.

**Rain cue on the weather screen** (v3-R1-2):
- **Its own slot**, right-aligned at x 306 on the condition baseline (y 104), under `Sunset 7:34`.
  In rain blue with a `›`.
- **The condition text is never dropped.** `Thunderstorm` and `Storm, hail` are safety words.
- Font: fs9, falling back to f2 when fs9 would come within 8 px of the condition. The worst
  case `Freezing drizzle` uses f2.
- **When it shows:** the nearest **named** blob (see *Naming*) is 2–40 mi away: `Rain 10 mi W ›`.
  Within 2 mi it reads `Raining here ›`. **Never from a frame older than 30 min** (`RADAR_CUE_MAX_AGE_MIN`).
- The text comes from the newest frame's header, **with the same rule and the same words as the
  radar strip**. The cue and the strip can't talk about different rain.

**Radar screen:** tap the weather **hero**, the band 0–320 × 30–112. It covers the icon,
temperature, condition and cue. It works with or without a cue, and says so when there's no rain.

## Navigation
| From | Gesture | Result |
|---|---|---|
| Weather | tap the hero band (incl. the cue) | Radar |
| Radar | tap `‹` (48 × 36) | Weather |
| Radar | no touch for 120 s | Weather |
| Anywhere | a plane qualifies | The v1 card pops **over the radar**. Afterwards you return to the radar, and its idle timer restarts (as on the map) |

Nothing else on the radar is interactive: no zoom, no pan, no pause. It is one fixed view.

## Vocabulary (v3-R1-5)
**"Radar" means rain radar only.** The ADS-B feed is **"Traffic"** everywhere:
- `Traffic offline` on the weather chip
- `Traffic offline · positions 40 s old` on the map
- a `Traffic` row on the boot screen

Both can be down at the same time, and the words say which is which. This is logged in 07.

## Screen (320 × 240, band-rendered like the map)
| Element | Rule |
|---|---|
| View | Equirectangular around you, the same projection as the map. The **50 mi ring = 100 px** (2.30 px/nm), so the view spans about **160 mi E–W** (80 each way) and about 100 mi N–S. You sit at (160,116), as on the map |
| Basemap | Zoom **R**, pre-rendered by `make_basemap.py` (motorways, trunks and named rivers to 95 nm; 38.4 KB flash). Same palette indices 0/2/3/5 as the map, but those slots are **darker** in `RADAR_PALETTE`, so light rain never melts into a road |
| Rain | 5 levels from dBZ via the official n0q table: **light ≥ 20, moderate ≥ 30, heavy ≥ 40, very heavy ≥ 50, extreme ≥ 60**. Echoes under 20 dBZ are dropped. Colours: `#2358A8 #3C8EF0 #8AD8FF #F4F7FF #FF4FD2` (blue → pale cyan → white → magenta). **No amber or orange** |
| Clutter | **Per blob** (v3-R2-2): drop a blob **under 6 px**, or one with **≥ half its pixels within 2 px of the static clutter mask** (`firmware/radar_clutter.cpp`, 172 px). A real storm crossing a mask site keeps every pixel. Clutter that wanders 1–2 px off its site is still caught. See *Clutter measurement* |
| Naming | A surviving blob is **named** (can appear in the words or the cue) if it is **≥ 12 px with moderate+ rain, or ≥ 40 px of any rain, or ANY size within 5 mi of you** (v3-R3-1: a young cell over you must say `Raining here`). Other blobs are drawn but never named. One rule for the strip and the cue |
| You | A white disc r 5 with a cyan core, on a BG ring r 7 (it survives pale-cyan and white rain) |
| Ring | A dotted `M_DIM` 50 mi ring, labelled `50 mi` (glcd, haloed) at the lower right |
| Towns | Up to 10, glcd `M_MUTED` with a 1 px BG halo and a 2 px dot. **Spread first:** one label per compass octant beyond 15 mi (the biggest town that fits), then fill by population. Each label tries above / right / left / below. **A label never covers rain:** one that would touch rain in **any** frame of the loop is hidden for the whole loop. On the device each frame header carries an 80×60 "wet cell" mask; the UI ORs them |
| Frame time | Top-right chip, **on every frame** (`7:00 PM`). TEXT on the newest frame, MUTED on older frames and while `updating`, **WARN** when stale or unreadable. **Partial coverage doesn't change the chip**; the time is fresh, and the strip's `partial coverage` carries the warning |
| Strip | `M_PANEL`. Left: the frame bar. Then the words (**MUTED when stale**). Right: a **continuous** 30×6 intensity bar (light → extreme, **hidden when there's no rain**), or a status word |
| Frame bar | 6 segments, oldest → newest. **Filled DIM** = downloaded; **filled TEXT** = the frame on screen; **hollow** = not downloaded yet (its only meaning). Stale: every segment filled DIM (loop stopped). **Hidden when nothing is drawn in the newest frame** (one frame, no loop, no backfill); any drawn echo, named or not, gets the loop |
| Words | **Always from the newest frame's header**, whatever frame the loop is showing. **Named blobs only**, visible area only: `Rain 10 mi W · heavy 19 mi W`; `Heavy rain 40 mi NE` when the nearest named rain is already heavy; `Raining here` inside 2 mi. When only unnamed echoes are drawn: **`Small echoes only`** (MUTED). **`No rain within 50 mi` only when nothing at all is drawn** (v3-R3-1). The second phrase is dropped if it doesn't fit |
| Attribution | `(c) OSM  IEM`, glcd, haloed, bottom-left |
| Motion | 6 frames, **10 min apart** (the last 50 min). Frames 1–5 show 0.4 s each; the newest holds **2 s** |

## States (mocked, `png/final-*`)
| State | What you see |
|---|---|
| `radar-monsoon`, `-f2`, `-loop` | 14 Jul 2024, 7:00 PM, a real outbreak. The words name the 681 px storm, `Heavy rain 40 mi NE`, on every frame. The 7 px speck 25 mi E is **dropped** (≥ half of it lies within 2 px of the clutter mask); round 2 led with it. The 6:20 frame has a MUTED time |
| `radar-scattered` | 31 Jul 2025, 7:30 PM: a real shower, `Rain 10 mi W · heavy 19 mi W` |
| `radar-small-echoes` | 20 Apr 2025, a held-out frame whose only echo is 9 px at 38 mi E: it is drawn, and the strip says `Small echoes only`, never `No rain` |
| `radar-none` | 25 Jun 2025, a **held-out** dry evening (not a mask-survey frame): no frame bar and no intensity bar, `No rain within 50 mi` |
| `radar-loading` | The newest frame arrived first. **The words show at once**; the bar has 2 of 6 filled and 4 hollow |
| `radar-updating` | Opened while the newest frame on SD is ≥ 15 min old and the first fetch is in flight: MUTED chip, `updating` on the right. **Not WARN: nothing has failed yet** |
| `radar-stale` | A fetch failed and the newest frame is 25 min old: WARN chip, `25 min old` on the right, loop stopped, **rain drawn in the dimmed ramp and the words MUTED** (nothing near full white, per 07 R2-3) |
| `radar-unreadable` | The newest frame failed the colour check. The previous good frame stays up with a WARN chip: `Bad radar data · retrying in 30 s` |
| `radar-partial` | Radar quorum < 95 %: `partial coverage` (WARN) on the right; the chip keeps the age rule |
| `radar-offline` | No good frame for ≥ 60 min: basemap only, `Radar offline · retrying in 30 s` |
| `radar-no-sd` | `Insert an SD card for radar`; the cue is off |
| `radar-night` | The monsoon frame at the 35 % night backlight. The preview uses ×0.62 on sRGB values (35 % linear light), not ×0.35 |
| `weather-rain-cue` (+ `-night`), `weather-cue-thunderstorm`, `-freezing`, `-hail`, `-here` | The cue in its own slot, next to the real `wxLabel()` worst cases. `Freezing drizzle` falls back to f2 |

The plane card popping over the radar is the unchanged v1 card (`final-plane-*`).

## A broken frame never reads as "No rain" (v3-R1-1)
`No rain within 50 mi` is only said when all four checks pass:
1. **Exact colours.** Every echo pixel must be an exact n0q colour. There is no nearest-colour
   guessing. If **> 0.5 %** of the echo pixels miss, the frame is **rejected** (`radar-unreadable`)
   and the previous frame stays up. On all 13 real frames in the mocks, misses = 0.
2. **Empty after wet.** If a frame with no echo follows a frame with rain less than 20 min
   earlier, it is **suspect**. It is re-fetched once, 60 s later, before being accepted.
3. **Quorum.** `n0q_0.json` has `radar_quorum` (e.g. `144/147`). Below **95 %** gives the WARN chip
   and `partial coverage`. **Caveat:** the national quorum doesn't prove that **KIWA** (Phoenix,
   about 8 mi SE of Gilbert) is up. A KIWA outage within quorum shows as missing echoes that we can't
   detect. That is a known limit, not a promise.
4. **Freshness.** The frame's own `valid` time is on screen, and the age rules below apply.

## Clutter measurement (v3-R1-6)
`python tools/radar/make_clutter_mask.py` produced these numbers. It surveys IEM n0q frames from
12 May–June 2025 dates at 7 PM and 6 AM (when inversions make clutter worst):

| | Frames | Result |
|---|---|---|
| Survey frames | 24 | 4 excluded: 6 May and 2 Jun 2025 had real weather (1,389–32,480 echo px). The rule: **< 200 echo px = dry** |
| Dry frames used | 20 | — |
| Static mask | — | pixels with an echo on ≥ 3 of the 20 (15 %), grown 1 px: **172 px** |
| Largest dry clutter blob, no mask | 20 | **10 px**. The round-1 claim "≤ 8 px" came from 3 frames and was wrong |
| Largest dry clutter blob, masked | 20 | **5 px**, hence the threshold of **6** |
| Small blobs on the 12 real wet frames (masked) | 12 | sizes 6–11 px occur 26 times. A threshold of 12 would have deleted them all |
| Held-out dry frames (not in the survey) | 4 | 3 clean to zero. **20 Apr 2025, 7 PM keeps one 9 px echo at 38 mi E** (unknown: clutter or a real shower). It is shown, but the virga rule keeps it from cueing the weather screen |

**Anomalous propagation** (inversion nights, outflow) isn't static and can exceed any size
threshold. The mask doesn't catch it; the colour and quorum checks don't either. This is a known
limit, and it is listed under device tests.

## Data
**Source:** the Iowa Environmental Mesonet NEXRAD n0q composite (keyless). Verified:
1. `GET https://mesonet.agron.iastate.edu/data/gis/images/4326/USCOMP/n0q_0.json` returns
   `meta.valid` (e.g. `2026-09-25T05:55:00Z`) and `meta.radar_quorum` (`144/147`). It's a few
   hundred bytes and gives the **exact newest frame time**.
2. `GET .../cgi-bin/wms/nexrad/n0q-t.cgi?...&SRS=EPSG:4326&BBOX=<view>&WIDTH=320&HEIGHT=240&FORMAT=image/tiff&TRANSPARENT=true&TIME=<valid>`
   returns an **uncompressed RGBA TIFF** (307,886 bytes). It is **planar**: 40 strips of 25 rows,
   all R, then G, B, A. The IFD is at offset 8, so it parses from the first 2 KB. The EPSG:4326
   box makes the plate-carrée pixels match the map projection exactly.

**Why TIFF and not PNG:** a PNG decoder needs a 32 KB inflate window plus about 11 KB of state,
which is right at the 43 KB largest block. The TIFF needs **no decoder**. It streams to SD in 1 KB
chunks (exactly `Content-Length` bytes), then converts row by row. The cost is about 300 KB per
frame instead of about 20 KB.

**Pipeline** (net task, core 0, after TLS closes; `radar_client.cpp`):
1. Stream to `/radar/raw.tif`. IEM sends **no Content-Length** over HTTP/1.0 (`Connection: close`,
   verified): the body runs to the close, and completeness is proven from the TIFF itself
   (`tiffDataEnd()`: the end of the last strip ≤ the file size). Parse the IFD (`tiffParse`) and reject anything that isn't
   320×240, 8-bit, 4 samples, uncompressed, little-endian.
2. Per row: 4 plane reads of 320 B; `radarLevel()` (exact n0q lookup, 255 entries, bsearch,
   `firmware/radar_table.cpp`); count misses. Write `/radar/lv.tmp` (1 byte per pixel). The mask
   is **not** applied per pixel; see step 3.
3. `radarClean()`: **two-pass run-length union-find, per blob**. Pass 1 also accumulates each blob's max level and its pixels near the clutter mask. Pass 1 unions runs and counts blob sizes.
   Pass 2 recomputes the *same* labels and zeroes small blobs in place. RAM is about 12 KB,
   transient, and nothing spills to SD. **Overflow** (> 3,000 runs, far beyond any real frame) keeps
   the frame uncleaned and logs `[radar] too many rain runs to clean`.
   The table has 2,000 labels (about 14 KB transient); the worst real frame, the 14 Jul 2024 monsoon, has 827 runs.
4. `radarNearest()` over the visible rows (< 214), for light and for heavy. Build the 80×60 wet mask.
   Pack 4bpp into `/radar/<valid>.tmp` with a 628-byte header (magic, valid, words, wet mask), then
   **rename to `.bin`**. The UI never sees a half-written frame.
5. Keep the loop set {T, T−10 … T−50 min} of the newest frame. **A file is deleted only after the
   UI has acknowledged a frame list that no longer contains it**, so it is never deleted while open.

**Scheduling** (v3-R1-10, v3-R2-1). The radar must never blind the plane tracker, and must not starve:
- At most **one** radar frame per net-task pass, and **an ADS-B poll between any two frames** (`g_radarOwesPoll`).
- Deferred **only for what can delay a pop**: a plane card is up, any plane is will-pop, or any
  plane passes the ENTER test. It is **not** deferred for "a plane within 4.5 nm": Gilbert
  (between CHD, IWA and FFZ training traffic) has one in all 4 captured fixtures, which would
  starve the radar all day.
- **Starvation cap:** after `RADAR_DEFER_MAX_S` = 120 s of deferral, one frame runs anyway. A pop
  is then late by at most one fetch (about 3 s).
- The log prints `[radar] deferred N s in the last hour`. Device tests: this number, and the ADS-B
  gap during a first-open backfill (≤ one frame fetch).

**Cadence** (v3-R1-9):
- **The cue** (always on): check `n0q_0.json` every **10 min** while Open-Meteo shows
  precipitation ≥ 10 % in the next 3 h or a wet code; otherwise every **30 min**. Fetch the newest
  frame when `valid` changes. That's about 1.8 MB/h on wet days and 0.6 MB/h on dry ones.
  **The radar's own evidence counts too:** named rain within 60 mi in the newest frame also
  selects the 10-min cadence. Monsoon cells often pop up unforecast (v3-R3 NTH 1).
- **Radar open:** if the newest frame is ≥ 15 min old, fetch **at once** (`updating`). Poll
  `n0q_0.json` every **2 min**. Backfill T−10 … T−50 newest first, one per pass, **only if the
  newest frame has rain**.
- **Age:** WARN after a *failed* attempt with the newest frame ≥ 15 min old, using the dimmed ramp.
  At ≥ 60 min the rain is cleared (`radar-offline`).
- Healthy worst case with the radar open: IEM latency about 5 min, plus the 5-min product step,
  plus the 2-min poll, is about 12 min. That is under the 15 min threshold, so a healthy system
  never shows WARN.

**Playback** (UI, core 1): per band, `memcpy` the basemap rows, then read the frame's 48 rows
(7.7 KB) from SD in 160-byte rows, and overwrite each non-zero nibble with its rain slot. That's
about 38 KB of reads per frame every 0.4 s, about 100 KB/s, well within SPI SD.

## Palette (`RADAR_PALETTE`, a swap of `MAP_PALETTE` while the radar is up)
| Slot | Map meaning | Radar meaning |
|---|---|---|
| 0 | BG | same |
| 2, 3, 5 | trunk, motorway, water | **darker**: `#1E283A`, `#2A3448`, `#11243A` |
| 1, 4, 6, 10, 11 | primary road, runway, trails, plane, plane dim | rain levels 1–5. **Stale:** the dimmed ramp `#1A3A66 #2A5A94 #4E7A92 #7E8698 #8A4A80` |
| 12 | zone fill | rain-blue text `#4AA3FF` |
| 7, 8, 9, 13, 14, 15 | text, muted, dim, panel, you, warn | same |

There is no plane glyph on the radar, so re-using the amber slots is safe.

## Hardware and budget
- **SD card** (CYD slot: CS 5, SCK 18, MISO 19, MOSI 23) on its **own SPI bus (HSPI)**. The touch
  controller is **bit-banged** (`touch_input.cpp`, same sampling as XPT2046_Touchscreen, so
  calibrations stay valid). The display keeps VSPI. **Without a card:** `Insert an SD card for radar`
  and no cue.
- **RAM** (measured on-device, v3 field fix; the whole story is in *Memory: what the field taught us*):
  - SD via **SdFat** (one 512 B cache), mounted once at boot: about **1.5 KB**. Not the core's
    `SD.h`/FATFS, which costs 6,248 + 4,136 × max_files bytes (4 KB sectors, a 4 KB cache per
    allowed file) in one block
  - convert: the `Work` buffers (about 5 KB, heap, only while converting); the blob table (8 B ×
    ≤ 1,500 labels) **borrows the idle ADS-B parse buffer**, and the run lists overlay `Work`
  - playback: 160 B plus the existing band sprite
  - **No new sprite and no permanent radar buffers.**
- **Flash:**
  - basemap R 38.4 KB
  - n0q table 1.5 KB
  - clutter mask 9.6 KB
  - towns about 1 KB
- **Politeness:** one IEM request at a time: JSON first, then one frame when `valid` changes.
  Exponential backoff (60 s × 2ⁿ, max 30 min). User-Agent `SkyDesk/<ver>`.

## Memory: what the field taught us (v3)
The first v3 flash broke **all** HTTPS: `tls -32512` (MBEDTLS_ERR_SSL_ALLOC_FAILED). Findings,
verified by two independent reviewers against the core's sources:
- `ESP.getFreeHeap()` / `getMaxAllocHeap()` count the ~40 KB **EXEC-only IRAM heap**, which malloc
  and mbedTLS can never use. Diagnostics now report `heap_caps_*(MALLOC_CAP_8BIT)`.
- A TLS session with `setInsecure()` still needs **~45–57 KB of 8-bit RAM**: two 16.7 KB record
  buffers plus the parsed peer chain (kept for the session). Open-Meteo and mesonet pick P-521 and
  send 4–6 KB chains, so they fail first. Pre-v3 had only ~3.5 KB to spare at the TLS peak.
- A mounted core FATFS volume costs ~15–23 KB (above). Mounting lazily failed on a fragmented
  heap, and `SD.end()`/`SD.begin()` in this core has thread-safety and use-after-free hazards.
- Fixes: SdFat (−13 KB), mDNS off (OTA by IP, −~5 KB and a task), one shared `Traffic` copy
  (−6 KB), radar blob table in the idle ADS-B buffer (no 24 KB transient).
- **3.0 field lessons** (a boot loop, then watchdog resets, all found on the serial log):
  - SdFat must run in **SHARED_SPI** mode. DEDICATED_SPI leaves the SPI transaction (the core's bus
    mutex) open after a read, owned by the reading task. Another task's next access then releases
    a mutex it doesn't hold, which fails a FreeRTOS assert.
  - The convert is about 5 s of core-0 work, so it **yields** 1 ms every 16 rows. Without that,
    IDLE0 starves and the task watchdog resets the board.
  - The download writes to SD in **2 KB, sector-aligned chunks** (multi-sector writes), and
    frames get a 15 s read timeout. With 512 B single-sector writes in SHARED_SPI mode the card
    drained slower than WiFi filled lwIP's buffers: heap min 2.1 KB, and every frame timed out
    (`http -11, 0/0 bytes`), so the radar sat on `updating`. Now it's 15-17 s a frame with a heap
    min of 9 KB.
  - The HTTP read loops **yield and time out** when `readBytes()` returns 0 while `available()` > 0
    (a TLS error). They used to spin.
  - V2 measured on the device: free 63–66 KB, largest block ~35 KB, heap minimum 3.5 KB, which
    is right at the edge. The first 3.0 build (+2 KB static) failed every TLS connection
    (largest 32.7 KB, minimum 148 B). 3.0 now uses 4.4 KB less static RAM than V2: free
    ~70 KB, minimum ~5.8 KB during a radar convert, 0 TLS failures in a 6-min run.
- **Rule for future work:** measure 8-bit RAM before and after, and keep ≥ 60 KB 8-bit free at
  rest. Any feature that adds permanent RAM must pay it back elsewhere.

## Tests
- **Host (`test_radar.cpp`, all passing):**
  - a real IEM TIFF at 64×48 **and at full size** (320×240 planar, 40 strips): every level equals the Python reference, 0 misses
  - exact colours at each threshold; a shifted-ramp colour is a miss
  - the clutter mask is loaded
  - cleaning keeps 8-connected and U-shaped blobs; a 6 px shower survives, a 5 px speck doesn't
  - **per blob:** a big storm crossing a mask site keeps every pixel, and a blob mostly near the mask is dropped
  - **naming:** a 7 px moderate speck is drawn but unnamed while the storm is named; 30 px of light rain is unnamed
  - nearest distance and bearing; rain under the strip is ignored
- **Host (to add):** empty-after-wet triggers one retry (it needs a fake SD; covered on the device for now)
- **Device:**
  - SD mount and touch accuracy after the bus change
  - time per frame
  - heap during convert
  - the ADS-B gap during backfill
  - loop smoothness at 0.4 s
  - the cue matches the IEM website
  - behaviour on an inversion night
