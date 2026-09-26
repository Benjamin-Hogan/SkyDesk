"""Real radar frames for the rain-radar mocks (docs/10-rain-radar.md).

Fetches archived IEM NEXRAD n0q composite frames (keyless WMS-T) for the radar
view's exact bounding box, caches them in docs/mockups/radar/, and classifies
every pixel to a rain LEVEL (0 = none, 1..5) with the official n0q colour table
(tools/radar/composite_n0q.txt, from pyIEM). The firmware does the same lookup
on the raw TIFF it streams to SD, so mock and device agree pixel for pixel.

Run directly to (re)fetch the cached frames:  python docs/mockups/radar_data.py
"""
from __future__ import annotations

import io
import math
import urllib.request
from functools import lru_cache
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
CACHE = Path(__file__).resolve().parent / "radar"
RAMP = ROOT / "tools" / "radar" / "composite_n0q.txt"

OBS = (33.3528, -111.7890)             # the documented default (mocks); see use_observer()


def use_observer(lat, lon, cache_dir):
    """Point everything (bbox, frames, masks) at another observer with its own cache - used by
    tools/radar/make_clutter_mask.py for the FIRMWARE observer (secrets.h may override it)."""
    global OBS, CACHE, _MASK, _NEAR
    OBS = (lat, lon)
    CACHE = Path(cache_dir)
    _MASK = _NEAR = None
    _levels.cache_clear()
CX, CY = 160, 116                      # you (same as the plane map)
RADAR_RING_MI = 50                     # the labelled ring is 100 px = 50 mi
PPN = 100 / (RADAR_RING_MI / 1.150779)  # px per nm (2.30)
LEVEL_DBZ = [20, 30, 40, 50, 60]       # level k (1..5) = dBZ >= LEVEL_DBZ[k-1]
LEVEL_NAMES = ["", "light", "moderate", "heavy", "very heavy", "extreme"]

# (name, UTC frame times). Real archived data, 10 min apart, newest last.
SEQUENCES = {
    "monsoon": ["2024-07-15T01:10:00Z", "2024-07-15T01:20:00Z", "2024-07-15T01:30:00Z",
                "2024-07-15T01:40:00Z", "2024-07-15T01:50:00Z", "2024-07-15T02:00:00Z"],
    "scattered": ["2025-08-01T01:40:00Z", "2025-08-01T01:50:00Z", "2025-08-01T02:00:00Z",
                  "2025-08-01T02:10:00Z", "2025-08-01T02:20:00Z", "2025-08-01T02:30:00Z"],
    "dry": ["2025-06-26T02:00:00Z"],
    "small": ["2025-04-20T02:00:00Z"],  # held out: only a 9 px echo 38 mi E -> "Small echoes only"   # held out: NOT one of the clutter-mask survey frames
}


def bbox(w=320, h=240):
    """EPSG:4326 box whose plate-carree pixels match the map's equirectangular projection."""
    coslat = math.cos(math.radians(OBS[0]))
    sx = w / 320
    west = OBS[1] - CX / (PPN * 60 * coslat)
    east = OBS[1] + (320 - CX) / (PPN * 60 * coslat)
    north = OBS[0] + CY / (PPN * 60)
    south = OBS[0] - (240 - CY) / (PPN * 60)
    return west, south, east, north, int(320 * sx), int(240 * sx)


def wms_url(t, fmt="image/png", w=320, h=240):
    west, south, east, north, _, _ = bbox()
    return ("https://mesonet.agron.iastate.edu/cgi-bin/wms/nexrad/n0q-t.cgi?SERVICE=WMS&VERSION=1.1.1"
            f"&REQUEST=GetMap&LAYERS=nexrad-n0q-wmst&STYLES=&SRS=EPSG:4326"
            f"&BBOX={west:.4f},{south:.4f},{east:.4f},{north:.4f}&WIDTH={w}&HEIGHT={h}"
            f"&FORMAT={fmt}&TRANSPARENT=true&TIME={t}")


def frame_path(t):
    return CACHE / (t.replace(":", "").replace("-", "") + ".png")


def fetch(t):
    p = frame_path(t)
    if not p.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        req = urllib.request.Request(wms_url(t), headers={"User-Agent": "SkyDesk-mockups/1.0 (hobby)"})
        p.write_bytes(urllib.request.urlopen(req, timeout=60).read())
    return p


def _ramp():
    rows = [ln.split(",") for ln in RAMP.read_text().splitlines()[1:]]
    return {(int(r), int(g), int(b)): float(v) for _, v, r, g, b in rows}


_RAMP = None


def dbz(rgb):
    """Exact n0q colour -> dBZ, or None on a MISS. Misses are counted per frame and a
    frame with > 0.5 % misses is rejected (review v3-R1-1): never guess a colour."""
    global _RAMP
    _RAMP = _RAMP or _ramp()
    return _RAMP.get(rgb)


def level_of(d):
    lv = 0
    for k, th in enumerate(LEVEL_DBZ, 1):
        if d >= th:
            lv = k
    return lv


@lru_cache(None)
def _levels(t):
    """(rows of levels 0..5, echo pixel count, miss count) for frame time t."""
    im = Image.open(fetch(t)).convert("RGBA")
    px = im.load()
    out = [[0] * 320 for _ in range(240)]
    cache = {}
    echo = miss = 0
    for y in range(240):
        for x in range(320):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            echo += 1
            k = (r, g, b)
            if k not in cache:
                d = dbz(k)
                cache[k] = None if d is None else level_of(d)
            if cache[k] is None:
                miss += 1
            else:
                out[y][x] = cache[k]
    return out, echo, miss


def readable(t):
    """False if more than 0.5 % of the echo pixels are not exact n0q colours."""
    _, echo, miss = _levels(t)
    return miss <= 0.005 * max(echo, 1)


MIN_BLOB_PX = 6        # == RADAR_MIN_BLOB_PX (docs/10 -> Clutter)
NAME_MIN_PX = 12       # a blob is NAMED if >= this with moderate+ ...
NAME_LIGHT_PX = 40     # ... or >= this of any rain  (== RADAR_NAME_*_PX, v3-R2-2)
NAME_ALWAYS_MI = 5     # ... or ANY size this close to you (== RADAR_NAME_ALWAYS_MI)
UNNAMED = 0x80         # flag on the level byte: drawn, never named (== RADAR_UNNAMED)
_NEAR = None


def clutter_near():
    """Within 2 px of the static clutter mask (tools/radar/make_clutter_mask.py)."""
    global _NEAR
    if _NEAR is None:
        p = CACHE / "clutter_near.png"
        im = Image.open(p).convert("1") if p.exists() else None
        _NEAR = [[bool(im.getpixel((x, y))) if im else False for x in range(320)] for y in range(240)]
    return _NEAR


def clean(lv):
    """Per BLOB (8-connected), exactly like firmware radarClean(): drop a blob under
    MIN_BLOB_PX or with >= half its pixels near the clutter mask; flag UNNAMED on a
    surviving blob that is not significant. A storm crossing a mask site keeps every pixel."""
    near = clutter_near()
    seen = [[False] * 320 for _ in range(240)]
    for y in range(240):
        for x in range(320):
            if lv[y][x] and not seen[y][x]:
                stack, blob = [(x, y)], []
                seen[y][x] = True
                while stack:
                    a, b = stack.pop()
                    blob.append((a, b))
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            u, v = a + dx, b + dy
                            if 0 <= u < 320 and 0 <= v < 240 and lv[v][u] and not seen[v][u]:
                                seen[v][u] = True
                                stack.append((u, v))
                n_near = sum(1 for a, b in blob if near[b][a])
                d_you = min(math.hypot(a - CX, b - CY) for a, b in blob) / PPN * 1.150779
                max_lv = max(lv[b][a] & 7 for a, b in blob)
                drop = len(blob) < MIN_BLOB_PX or n_near * 2 >= len(blob)
                named = ((len(blob) >= NAME_MIN_PX and max_lv >= 2) or len(blob) >= NAME_LIGHT_PX
                         or d_you <= NAME_ALWAYS_MI)                   # rain near you: always (v3-R3-1)
                for a, b in blob:
                    lv[b][a] = 0 if drop else (lv[b][a] & 7) | (0 if named else UNNAMED)
    return lv


def levels(t):
    """320x240 rows of rain levels 0..5 (a fresh copy; callers may clean() it)."""
    return [row[:] for row in _levels(t)[0]]


def nearest(lv, min_level=1):
    """Nearest NAMED pixel with level >= min_level (a cleaned layer) -> (miles, azimuth) or None."""
    best = None
    for y in range(240):
        for x in range(320):
            v = lv[y][x]
            if v and not (v & UNNAMED) and (v & 7) >= min_level:
                d = math.hypot(x - CX, y - CY)
                if best is None or d < best[0]:
                    best = (d, (math.degrees(math.atan2(x - CX, -(y - CY))) + 360) % 360)
    if best is None:
        return None
    return best[0] / PPN * 1.150779, best[1]


if __name__ == "__main__":
    for name, times in SEQUENCES.items():
        for t in times:
            p = fetch(t)
            lv = clean(levels(t))
            cnt = [sum(r.count(k) for r in lv) for k in range(6)]
            print(name, t, p.stat().st_size, "levels", cnt, "nearest", nearest(lv))
