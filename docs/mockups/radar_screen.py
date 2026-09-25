"""Rain radar screen + weather rain cue (docs/10-rain-radar.md), drawn with tftsim.

Real data only: archived IEM NEXRAD n0q frames (radar_data.py) classified with the
official n0q colour table, over the wide OSM basemap that tools/basemap/make_basemap.py
renders as zoom "R" (the firmware's RADAR_BASEMAP). Imported by screens.py.

On the device the radar view is the same band pipeline as the plane map:
320x48 4-bit band sprite; basemap rows memcpy'd from flash, then the frame's
4-bit rain layer (streamed from SD) overwrites every non-zero pixel. So this
whole screen is drawn with t.sprite4 = True (no anti-aliased calls).
"""
from __future__ import annotations

import datetime as dt
import math
import sys
from pathlib import Path

from PIL import Image

import geo
from tftsim import TFT, rgb
import radar_data as R

S = sys.modules["__main__"] if hasattr(sys.modules.get("__main__"), "plane_glyph") else __import__("screens")
import map_screen as M   # chrome helpers shared with the plane map

ROOT = Path(__file__).resolve().parents[2]

# RADAR_PALETTE: the map palette with the plane / zone / runway / trail / primary
# slots re-used for rain. Basemap indices 0/2/3/5 are the same slots as the map, but
# DARKER here so light rain never melts into a road (review v3-R1 NTH 7). No amber or
# orange anywhere: amber stays "the plane that matters" (07-design-review).
RADAR_PAL = list(M.MAP_PAL)
RADAR_PAL[M.M_TRUNK] = "#1E283A"
RADAR_PAL[M.M_MOTORWAY] = "#2A3448"
RADAR_PAL[M.M_WATER] = "#11243A"
RAIN_SLOT = [None, 1, 4, 6, 10, 11]           # level k -> palette slot
RAIN_COL = [None, "#2358A8", "#3C8EF0", "#8AD8FF", "#F4F7FF", "#FF4FD2"]
# Stale frames swap the rain slots to this dimmed ramp: same order, nothing near full
# white (07: stale data never renders in full white, R2-3). Zero RAM: a palette swap.
RAIN_STALE = [None, "#1A3A66", "#2A5A94", "#4E7A92", "#7E8698", "#8A4A80"]
for k in range(1, 6):
    RADAR_PAL[RAIN_SLOT[k]] = RAIN_COL[k]
P_RAINTEXT = 12
RADAR_PAL[P_RAINTEXT] = S.RAIN                # rain-blue text (== weather hourly precip blue)

FRAME_HOLD_MS, FRAME_STEP_MS = 2000, 400      # newest frame holds 2 s, others 0.4 s
STALE_MIN, CLEAR_MIN = 15, 60                 # frame age: warn / stop showing rain
OCTANT_MIN_MI = 15                            # labels: one per octant beyond this, then population
MAX_LABELS = 10


# ---------------------------------------------------------------------------
#  Basemap: the SAME image the firmware embeds (make_basemap.py -> basemap-radar.png)
# ---------------------------------------------------------------------------
def proj(lat, lon):
    coslat = math.cos(math.radians(R.OBS[0]))
    return R.CX + (lon - R.OBS[1]) * 60 * coslat * R.PPN, R.CY - (lat - R.OBS[0]) * 60 * R.PPN


_BM = None


def basemap():
    """(indexed basemap image, towns [(pop, name, x, y)] biggest first) == RADAR_TOWNS."""
    global _BM
    if _BM:
        return _BM
    img = Image.open(S.OUT / "basemap-radar.png")
    towns = []
    src = (ROOT / "firmware" / "basemap_data.cpp").read_text(encoding="utf-8")
    block = src[src.index("const MapLabel RADAR_TOWNS[] = {"):]
    block = block[:block.index("};")]
    import re
    pops = _populations()
    for name, lat, lon in re.findall(r'\{"([^"]+)", (-?[\d.]+), (-?[\d.]+),', block):
        if name == "Gilbert":                                    # your own town is the dot
            continue
        x, y = proj(float(lat), float(lon))
        towns.append((pops.get(name, 0), name, x, y))
    _BM = (img, towns)
    return _BM


def _populations():
    import json
    osm = json.loads((ROOT / "tools/basemap/cache/osm95_33.35_-111.79.json").read_text(encoding="utf-8"))
    out = {}
    for e in osm["elements"]:
        tg = e.get("tags", {})
        if "name" in tg:
            try:
                out[tg["name"].split(" / ")[0]] = int(tg.get("population", "0").replace(",", ""))
            except ValueError:
                pass
    return out


# ---------------------------------------------------------------------------
#  Words - ALWAYS from the newest frame (review v3-R1-3), whatever frame is shown
# ---------------------------------------------------------------------------
def visible(lv):
    return [row if y < M.STRIP_Y else [0] * 320 for y, row in enumerate(lv)]


def rain_words(lv):
    """Strip wording from the CLEANED newest layer. Returns (first, second) or None."""
    near = R.nearest(lv, 1)
    if not near:
        return None
    heavy = R.nearest(lv, 3)
    first = "Raining here" if near[0] < 2.0 else f"Rain {near[0]:.0f} mi {geo.compass8(near[1])}"
    second = None
    if heavy and (heavy[0] - near[0] > 3.0):
        second = f"heavy {heavy[0]:.0f} mi {geo.compass8(heavy[1])}"
    elif heavy and near[0] >= 2.0:
        first = "Heavy rain" + first[4:]
    return first, second


def local(t_utc):
    t = dt.datetime.strptime(t_utc, "%Y-%m-%dT%H:%M:%SZ") - dt.timedelta(hours=7)    # MST, no DST
    return t.strftime("%I:%M %p").lstrip("0")


# ---------------------------------------------------------------------------
#  Radar screen
# ---------------------------------------------------------------------------
def halo_text(t, s, x, y, tok, c, datum="L"):
    """Label legible over any rain level: 1 px BG outline (8 offsets) then the text."""
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            if dx or dy:
                t.drawString(s, x + dx, y + dy, tok, RADAR_PAL[M.M_BG], datum)
    return t.drawString(s, x, y, tok, c, datum)


def place_labels(t, towns, wet):
    """One label per compass octant beyond OCTANT_MIN_MI (nearest-biggest first), then
    fill by population; max 10. A label never covers rain in ANY frame of the loop."""
    taken = [(0, 0, 48, 36), (200, 0, 320, 36), (146, 0, 174, 30), (0, M.STRIP_Y - 14, 110, 240),
             (R.CX - 8, R.CY - 8, R.CX + 8, R.CY + 8), (R.CX + 70, R.CY + 64, R.CX + 106, R.CY + 78)]
    out = []

    def free(r):
        return r[0] >= 2 and r[2] <= 318 and r[1] >= 0 and r[3] < M.STRIP_Y - 2 and all(
            r[2] <= q[0] or r[0] >= q[2] or r[3] <= q[1] or r[1] >= q[3] for q in taken)

    def dry(r):
        return not any(wet[yy][xx] for yy in range(max(0, int(r[1])), min(240, int(r[3]) + 1))
                       for xx in range(max(0, int(r[0])), min(320, int(r[2]) + 1)))

    def try_place(name, x, y):
        w = t.textWidth(name, "glcd")
        for lx, ly, datum in ((x, y - 3, "C"), (x + 4, y + 4, "L"), (x - 4, y + 4, "R"), (x, y + 11, "C")):
            x0 = lx - w / 2 if datum == "C" else lx if datum == "L" else lx - w
            r = (x0 - 1, ly - 8, x0 + w + 1, ly + 1)
            if free(r) and dry(r) and dry((x - 2, y - 2, x + 2, y + 2)):
                taken.extend([r, (x - 2, y - 2, x + 2, y + 2)])
                out.append((name, x, y, lx, ly, datum))
                return True
        return False

    done = set()
    for octant in range(8):                           # spread first: every direction gets a reference
        cands = []
        for pop, name, x, y in towns:
            dmi = math.hypot(x - R.CX, y - R.CY) / R.PPN * 1.150779
            az = (math.degrees(math.atan2(x - R.CX, R.CY - y)) + 360) % 360
            if dmi >= OCTANT_MIN_MI and int(((az + 22.5) % 360) // 45) == octant:
                cands.append((pop, name, x, y))
        for pop, name, x, y in sorted(cands, reverse=True):
            if try_place(name, x, y):
                done.add(name)
                break
    for pop, name, x, y in towns:                     # then fill by population
        if len(out) >= MAX_LABELS:
            break
        if name not in done and try_place(name, x, y):
            done.add(name)
    for name, x, y, lx, ly, datum in out:
        t.fillRect(round(x) - 1, round(y) - 1, 2, 2, RADAR_PAL[M.M_MUTED])
        halo_text(t, name, round(lx), round(ly), "glcd", RADAR_PAL[M.M_MUTED], datum)


def radar_screen(t, d):
    """d: seq, frame (index into seq), state, plus overrides.
    States: live | loading | updating | stale | unreadable | partial | offline | nosd."""
    t.sprite4 = True
    state = d.get("state", "live")
    times = R.SEQUENCES[d["seq"]]
    newest_i = d.get("newest", len(times) - 1)        # 'unreadable': the newest GOOD frame is older
    have = d.get("have", len(times))                   # frames on SD (newest first)
    fi = d.get("frame", newest_i)
    img, towns = basemap()
    pal = list(RADAR_PAL)
    if state == "stale":
        for k in range(1, 6):
            pal[RAIN_SLOT[k]] = RAIN_STALE[k]
    for y in range(240):
        for x in range(320):
            t.px[x, y] = rgb(pal[img.getpixel((x, y))])

    show_rain = state not in ("offline", "nosd")
    newest_lv = visible(R.clean(R.levels(times[newest_i]))) if show_rain else None
    words = rain_words(newest_lv) if show_rain else None
    drawn_newest = show_rain and any(v for row in newest_lv for v in row)
    looping = show_rain and drawn_newest and state != "stale"       # nothing drawn: one frame, no loop
    lv = R.clean(R.levels(times[fi if looping else newest_i])) if show_rain else None
    if lv:
        for y in range(M.STRIP_Y):
            for x in range(320):
                if lv[y][x]:
                    t.px[x, y] = rgb(pal[RAIN_SLOT[lv[y][x] & 7]])

    M.dotted_circle(t, R.CX, R.CY, 100, pal[M.M_DIM], 7)
    halo_text(t, "50 mi", R.CX + 74, R.CY + 74, "glcd", pal[M.M_DIM])

    wet = [[0] * 320 for _ in range(240)]
    if show_rain:
        for tm in (times[max(0, newest_i - have + 1):newest_i + 1] if looping else [times[newest_i]]):
            for yy, row in enumerate(R.clean(R.levels(tm))):
                for xx, v in enumerate(row):
                    if v:
                        wet[yy][xx] = 1
    place_labels(t, towns, wet)

    t.fillCircle(R.CX, R.CY, 7, pal[M.M_BG])
    t.fillCircle(R.CX, R.CY, 5, pal[M.M_TEXT])
    t.fillCircle(R.CX, R.CY, 3, pal[M.M_YOU])

    t.fillRoundRect(4, 4, 36, 24, 12, pal[M.M_PANEL])
    M.chevron_left(t, 17, 16, pal[M.M_TEXT])
    t.fillTriangle(160, 3, 155, 12, 165, 12, pal[M.M_MUTED])
    t.drawString("N", 160, 24, "glcd", pal[M.M_MUTED], "C")
    if show_rain:                                     # every frame carries its time
        shown_i = fi if looping else newest_i
        warn = state in ("stale", "unreadable")                     # partial: the words carry it
        col = pal[M.M_WARN] if warn else pal[M.M_TEXT] if (shown_i == newest_i and state != "updating") \
            else pal[M.M_MUTED]
        lab = local(times[shown_i])
        zw = t.textWidth(lab, "f2") + 18
        t.fillRoundRect(316 - zw, 4, zw, 24, 12, pal[M.M_PANEL])
        t.drawString(lab, 316 - zw // 2, 21, "f2", col, "C")
    halo_text(t, "(c) OSM  IEM", 4, M.STRIP_Y - 4, "glcd", pal[M.M_DIM])

    # ---- strip ----
    t.fillRect(0, M.STRIP_Y, 320, 240 - M.STRIP_Y, pal[M.M_PANEL])
    if state == "nosd":
        t.drawString("Insert an SD card for radar", 10, 231, "f2", pal[M.M_MUTED])
        return
    if state == "offline":
        t.drawString("Radar offline", 10, 231, "f2", pal[M.M_WARN])
        t.drawString("retrying in 30 s", 310, 231, "f2", pal[M.M_MUTED], "R")
        return
    x = 10
    if looping or state == "stale":
        # frame bar: filled DIM = downloaded, TEXT = the frame on screen, hollow = not yet
        # downloaded (the ONLY meaning of hollow). Stale: all DIM, loop stopped.
        for i in range(6):
            present = 6 - i <= have
            if state == "stale":
                t.fillRect(x + i * 8, 222, 6, 10, pal[M.M_DIM])
            elif not present:
                t.drawRect(x + i * 8, 222, 6, 10, pal[M.M_DIM])
            else:
                on = i == 5 - (newest_i - fi)
                t.fillRect(x + i * 8, 222, 6, 10, pal[M.M_TEXT] if on else pal[M.M_DIM])
        x += 6 * 8 + 6
    right = {"updating": ("updating", pal[M.M_MUTED]), "partial": ("partial coverage", pal[M.M_WARN]),
             "stale": (f"{d.get('age_min', 25)} min old", pal[M.M_WARN])}.get(state)
    if state == "unreadable":
        w = t.drawString("Bad radar data", x, 231, "f2", pal[M.M_WARN])
        S.dot(t, x + w + 5, 226, pal[M.M_DIM])
        t.drawString("retrying in 30 s", x + w + 10, 231, "f2", pal[M.M_MUTED])
        return
    rx = 310
    if right:
        rx -= t.drawString(right[0], 310, 231, "f2", right[1], "R") + 8
    elif words:                                       # one continuous intensity bar (not when dry)
        for k in range(1, 6):
            t.fillRect(280 + (k - 1) * 6, 224, 6, 6, pal[RAIN_SLOT[k]])
        rx = 272
    if not words:   # "No rain" only when NOTHING is drawn (v3-R3-1)
        drawn = lv is not None and any(v for row in lv[:M.STRIP_Y] for v in row)
        t.drawString("Small echoes only" if drawn else "No rain within 50 mi", x, 231, "f2", pal[M.M_MUTED])
        return
    w = t.drawString(words[0], x, 231, "f2", pal[M.M_MUTED] if state == "stale" else pal[M.M_TEXT])
    if words[1] and x + w + 10 + t.textWidth(words[1], "f2") <= rx:
        S.dot(t, x + w + 5, 226, pal[M.M_DIM])
        t.drawString(words[1], x + w + 10, 231, "f2", pal[M.M_MUTED])


def night(fn):
    """Preview at the night backlight (35 %). Backlight is linear light: 0.35 in linear
    light is about x0.6 on sRGB values (review v3-R1 NTH 8), not x0.35."""
    def wrapped(t, d):
        fn(t, d)
        f = 0.35 ** (1 / 2.2)
        t.img = t.img.point(lambda v: round(v * f))
        t.px = t.img.load()
    return wrapped


# ---------------------------------------------------------------------------
#  Weather screen rain cue - from the newest frame header (docs/10 -> Rain cue)
# ---------------------------------------------------------------------------
def cue_from(seq, fi=-1):
    """The weather-screen cue for this frame, from the SAME named-blob rule as the strip:
    'Rain 9 mi W' (2-40 mi), 'Raining here' (< 2 mi), or None."""
    near = R.nearest(visible(R.clean(R.levels(R.SEQUENCES[seq][fi]))), 1)
    if not near or near[0] > 40:
        return None
    if near[0] < 2:
        return "Raining here"
    return f"Rain {near[0]:.0f} mi {geo.compass8(near[1])}"


def sheet(t, d):
    """Contact sheet of the 6-frame loop at 1/2 scale (documentation only)."""
    t.sprite4 = False
    t.fillScreen(S.BG)
    for i, tm in enumerate(R.SEQUENCES[d["seq"]]):
        sub = TFT(S.BG)
        radar_screen(sub, dict(seq=d["seq"], frame=i))
        small = sub.img.resize((106, 80), Image.NEAREST)
        t.img.paste(small, ((i % 3) * 107, (i // 3) * 120))
        t.px = t.img.load()
        t.sprite4 = False
        t.drawString(f"{i + 1}  {local(tm)}", (i % 3) * 107 + 2, (i // 3) * 120 + 94, "glcd", S.MUTED)
    t.drawString("hold 2 s on 6, 0.4 s on 1-5. Words: newest frame", 2, 236, "glcd", S.DIM)


SCENARIOS = {
    "radar-monsoon": (radar_screen, lambda: dict(seq="monsoon")),
    "radar-monsoon-f2": (radar_screen, lambda: dict(seq="monsoon", frame=1)),
    "radar-monsoon-loop": (sheet, lambda: dict(seq="monsoon")),
    "radar-scattered": (radar_screen, lambda: dict(seq="scattered")),
    "radar-none": (radar_screen, lambda: dict(seq="dry", frame=0)),
    "radar-small-echoes": (radar_screen, lambda: dict(seq="small", frame=0)),
    "radar-loading": (radar_screen, lambda: dict(seq="scattered", have=2)),
    "radar-updating": (radar_screen, lambda: dict(seq="scattered", state="updating")),
    "radar-stale": (radar_screen, lambda: dict(seq="scattered", state="stale", age_min=25)),
    "radar-unreadable": (radar_screen, lambda: dict(seq="scattered", state="unreadable", newest=4)),
    "radar-partial": (radar_screen, lambda: dict(seq="monsoon", state="partial")),
    "radar-offline": (radar_screen, lambda: dict(seq="scattered", state="offline")),
    "radar-night": (night(radar_screen), lambda: dict(seq="monsoon")),
    "radar-no-sd": (radar_screen, lambda: dict(seq="scattered", state="nosd")),
}
