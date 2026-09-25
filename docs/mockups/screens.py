"""SkyDesk screen layouts, drawn with tftsim (real TFT_eSPI fonts, RGB565).

This file is the *executable* UI spec: coordinates and draw calls here are
what firmware/ui_*.cpp implements. Render with:

    python docs/mockups/screens.py r2          # writes png/r2-*.png

Every aircraft number shown is computed by geo.py from a real position.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import geo
from tftsim import TFT

OUT = Path(__file__).resolve().parent / "png"

# ---------------------------------------------------------------------------
#  Palette (== COL_* in include/config.h)
# ---------------------------------------------------------------------------
BG = "#0A1120"
PANEL = "#131C2E"
PANEL2 = "#1C2740"
HAIR = "#2A3752"
TEXT = "#EEF2F8"
MUTED = "#9AA6BE"
DIM = "#7886A2"
PLANE = "#FFB02E"
PLANE_DIM = "#8A6424"
PLANE_FAINT = "#3A3326"
OK = "#3DDC84"
WARN = "#FF8A3D"
ERR = "#FF5A5F"
SUN = "#FFD24A"
CLOUD = "#C9D3E3"
RAIN = "#4AA3FF"
MOON = "#E8E3C8"

OBS = (33.3528, -111.7890, 1240)      # Gilbert, AZ (OBS_LAT/LON/ELEV_FT)


# ---------------------------------------------------------------------------
#  Shared helpers
# ---------------------------------------------------------------------------
def degree(t: TFT, x, y_base, tok, c):
    """Degree ring sized per font, top aligned to cap height."""
    r, th, cap = {"fsb24": (5, 3, 33), "fsb18": (4, 2, 25), "fsb12": (3, 2, 17),
                  "fsb9": (2, 1, 12), "fs9": (2, 1, 12)}[tok]
    return t.drawDegree(x + 1, y_base - cap, r, c, th)


TRACK = {"fsb9": 1}   # FreeSansBold9 '8' has zero side-bearing: "288" merges


def num(t, s, x, y, tok, c, datum="L"):
    """drawNumber(): digits get +1 px tracking in fonts that need it."""
    return t.drawString(s, x, y, tok, c, datum, track=TRACK.get(tok, 0))


def temp(t, x, y, value, tok, c, datum="L"):
    s = str(value)
    w = t.textWidth(s, tok, TRACK.get(tok, 0))
    ring = {"fsb24": 13, "fsb18": 11, "fsb12": 9, "fsb9": 7, "fs9": 7}[tok]
    if datum == "C":
        x -= (w + ring) // 2
    num(t, s, x, y, tok, c)
    degree(t, x + w, y, tok, c)
    return w + ring


def dot(t, x, y, c, r=1):
    """Middle dot separator (not in any bitmap font)."""
    t.fillCircle(x, y, r, c)


def arrow_right(t, x, y_mid, length, c, head=5, wd=2):
    """Route arrow (U+2192 is not in the fonts)."""
    t.drawWideLine(x, y_mid, x + length - head, y_mid, wd, c)
    t.fillTriangle(x + length - head - 1, y_mid - head, x + length, y_mid, x + length - head - 1, y_mid + head, c)
    return length


def tri_up(t, x, y_base, s, c):
    t.fillTriangle(x, y_base, x + s, y_base, x + s // 2, y_base - s, c)


def tri_down(t, x, y_top, s, c):
    t.fillTriangle(x, y_top, x + s, y_top, x + s // 2, y_top + s, c)


def check(t, x, y, c):
    t.drawWideLine(x, y + 4, x + 3, y + 7, 2, c)
    t.drawWideLine(x + 3, y + 7, x + 9, y, 2, c)


def cross(t, x, y, c):
    t.drawWideLine(x, y, x + 8, y + 8, 2, c)
    t.drawWideLine(x + 8, y, x, y + 8, 2, c)


def chevron(t, x, y_mid, c):
    t.drawWideLine(x, y_mid - 4, x + 4, y_mid, 2, c)
    t.drawWideLine(x + 4, y_mid, x, y_mid + 4, 2, c)


def fit(t, text, max_w, toks=("fsb12", "fsb9")):
    """UI rule: step the font down once, then truncate with 3 dots."""
    for tok in toks:
        if t.textWidth(text, tok) <= max_w:
            return tok, text, False
    tok = toks[-1]
    while text and t.textWidth(text, tok) + 8 > max_w:
        text = text[:-1]
    return tok, text.rstrip(), True


def draw_fit(t, text, x, y, max_w, c, toks=("fsb12", "fsb9")):
    tok, s, trunc = fit(t, text, max_w, toks)
    w = t.drawString(s, x, y, tok, c)
    if trunc:
        for i in range(3):
            t.fillRect(x + w + 1 + i * 3, y - 2, 2, 2, c)
        w += 9
    return w


# ---------------------------------------------------------------------------
#  Aircraft glyph: convex parts -> fillTriangle pairs in firmware
# ---------------------------------------------------------------------------
PLANE_PARTS = [
    [(-1.6, -9.5), (0, -12), (1.6, -9.5)],                 # nose
    [(-1.6, -9.5), (1.6, -9.5), (1.2, 9), (-1.2, 9)],      # fuselage
    [(1.4, -3), (10.5, 3), (10.5, 5.5), (1.4, 2)],         # right wing
    [(-1.4, -3), (-10.5, 3), (-10.5, 5.5), (-1.4, 2)],     # left wing
    [(1, 6.5), (4.5, 9.5), (4.5, 11), (0.8, 10)],          # right tailplane
    [(-1, 6.5), (-4.5, 9.5), (-4.5, 11), (-0.8, 10)],      # left tailplane
]


def plane_glyph(t, cx, cy, heading_screen_deg, scale, c, outline_only=False, bg=None):
    a = math.radians(heading_screen_deg)
    ca, sa = math.cos(a), math.sin(a)
    for part in PLANE_PARTS:
        pts = [(cx + (x * ca - y * sa) * scale, cy + (x * sa + y * ca) * scale) for x, y in part]
        t.fillPolygon(pts, c)
    if outline_only:   # stale: hollow it out
        for part in PLANE_PARTS:
            pts = [(cx + (x * ca - y * sa) * scale * 0.55, cy + (x * sa + y * ca) * scale * 0.55) for x, y in part]
            t.fillPolygon(pts, bg)


# ---------------------------------------------------------------------------
#  Weather icons (vector, firmware: ui_common.cpp drawWxIcon)
# ---------------------------------------------------------------------------
def sun(t, cx, cy, s):
    t.fillCircle(cx, cy, round(9 * s), SUN)
    for i in range(8):
        a = math.radians(i * 45)
        t.drawWideLine(cx + math.cos(a) * 13 * s, cy + math.sin(a) * 13 * s,
                       cx + math.cos(a) * 17 * s, cy + math.sin(a) * 17 * s, max(1.5, 2.5 * s), SUN)


def moon(t, cx, cy, s, bg):
    t.fillCircle(cx, cy, round(9 * s), MOON)
    t.fillCircle(round(cx + 5 * s), round(cy - 4 * s), round(8 * s), bg)


def cloud(t, cx, cy, s, c=CLOUD):
    t.fillCircle(round(cx - 6 * s), round(cy + 2 * s), round(6 * s), c)
    t.fillCircle(round(cx + 2 * s), round(cy - 2 * s), round(8 * s), c)
    t.fillCircle(round(cx + 9 * s), round(cy + 3 * s), round(5 * s), c)
    t.fillRoundRect(round(cx - 12 * s), round(cy + 2 * s), round(26 * s), round(6 * s) + 1, round(3 * s), c)


def wx_icon(t, cx, cy, s, kind, day, bg):
    if kind == "clear":
        sun(t, cx, cy, s) if day else moon(t, cx, cy, s, bg)
    elif kind == "partly":
        if day:
            sun(t, round(cx - 4 * s), round(cy - 5 * s), s * 0.9)
        else:
            moon(t, round(cx - 5 * s), round(cy - 5 * s), s * 0.9, bg)
        cloud(t, round(cx + 5 * s), round(cy + 5 * s), s * 0.8)
    elif kind == "cloud":
        cloud(t, cx, cy, s)
    elif kind == "rain":
        cloud(t, cx, round(cy - 3 * s), s)
        for dx in (-5, 1, 7):
            t.drawWideLine(cx + dx * s, cy + 8 * s, cx + (dx - 2) * s, cy + 13 * s, max(1.5, 2 * s), RAIN)


# ---------------------------------------------------------------------------
#  WEATHER screen
# ---------------------------------------------------------------------------
def weather(t: TFT, d: dict):
    t.fillScreen(BG)
    # Header: date left; status only when degraded (no permanent clutter)
    t.drawString(d["date"], 10, 17, "f2", MUTED)
    if d.get("stale"):
        w = t.drawString(d["stale"], 296, 17, "f2", WARN, "R")
        t.fillCircle(305, 12, 3, WARN)

    # Hero: icon + temperature + condition
    hero_c = MUTED if d.get("stale") else TEXT
    wx_icon(t, 40, 62, 1.5, d["kind"], d["day"], BG)
    temp(t, 80, 82, d["temp"], "fsb24", hero_c)
    cw = t.drawString(d["cond"], 82, 104, "fs9", MUTED)   # the condition is NEVER dropped (v3-R1-2)
    cue = d.get("rain_cue")   # docs/10: its own slot, right-aligned under the sunset line
    if cue:
        for tok in ("fs9", "f2"):                          # fs9, else f2 if it would come within 8 px
            cue_w = t.textWidth(cue, tok) + 11
            if 306 - cue_w >= 82 + cw + 8:
                break
        rw = t.drawString(cue, 306 - 11, 104, tok, RAIN, "R")
        chevron(t, 306 - 6, 99, RAIN)

    # Clock
    w = t.textWidth("PM", "f2")
    t.drawString(d["time"], 306 - w - 4, 62, "fsb18", TEXT, "R")
    t.drawString(d["ampm"], 306 - w, 62, "f2", MUTED)
    t.drawString(d["sun_evt"], 306, 84, "f2", MUTED, "R")

    # Detail cells: label (glcd) + value (fsb9) + unit (f2)
    cells = [("FEELS", d["feels"], None, True), ("HI / LO", None, None, False),
             ("HUMIDITY", f'{d["hum"]}%', None, False), ("WIND", d["wind"], "mph", False)]
    xs = [10, 84, 170, 238]
    for (lab, val, unit, is_temp), x in zip(cells, xs):
        t.drawString(lab, x, 126, "glcd", DIM)
        if lab == "HI / LO":
            w = temp(t, x, 145, d["hi"], "fsb9", TEXT)
            w += t.drawString("/", x + w + 1, 145, "fsb9", DIM) + 2
            temp(t, x + w + 1, 145, d["lo"], "fsb9", MUTED)
        elif is_temp:
            temp(t, x, 145, val, "fsb9", TEXT)
        else:
            w = num(t, val, x, 145, "fsb9", TEXT)
            if unit:
                t.drawString(unit, x + w + 3, 145, "f2", MUTED)

    # Hourly strip
    t.fillRoundRect(6, 154, 308, 56, 6, PANEL)
    for i, h in enumerate(d["hourly"]):
        cx = 31 + i * 51
        t.drawString(h["h"], cx, 168, "f2", MUTED, "C")
        if h["pop"] >= 20:     # icon + precip% share the row, centred as a pair
            wx_icon(t, cx - 9, 181, 0.55, h["kind"], h.get("day", True), PANEL)
            t.drawString(f'{h["pop"]}%', cx + 3, 186, "glcd", RAIN)
        else:
            wx_icon(t, cx, 181, 0.55, h["kind"], h.get("day", True), PANEL)
        temp(t, cx, 205, h["t"], "fsb9", TEXT, "C")

    # Traffic chip (tappable -> chevron)
    t.fillRoundRect(6, 215, 308, 23, 11, PANEL2)
    radar_ok = d["traffic"] is not None
    plane_glyph(t, 21, 226, 45, 0.55, PLANE if radar_ok else DIM)
    if radar_ok:
        n, nearest = d["traffic"]
        t.drawString(f"{n} nearby", 36, 231, "f2", TEXT)
        t.drawString(nearest, 296, 231, "f2", MUTED, "R")
        chevron(t, 302, 226, MUTED)
    else:
        t.drawString("Traffic offline", 36, 231, "f2", WARN)
        t.drawString(d["radar_retry"], 304, 231, "f2", MUTED, "R")


# ---------------------------------------------------------------------------
#  PLANE screen
# ---------------------------------------------------------------------------
DOME_CX, DOME_CY, DOME_R = 82, 124, 70
COL_X = 166          # right column left edge
COL_W = 314 - COL_X  # 148 px


def dome_xy(az, el, view_up, clamp=False):
    rr = DOME_R * (90 - max(0.0, el)) / 90
    if clamp:                       # keep the glyph inside the rim
        rr = min(rr, DOME_R - 19)
    th = math.radians(az - view_up)
    return DOME_CX + rr * math.sin(th), DOME_CY - rr * math.cos(th)


def dome(t, p):
    cx, cy, r = DOME_CX, DOME_CY, DOME_R
    vu = p.get("view_up", 0)
    state = p.get("state", "live")
    accent = PLANE if state in ("live", "forced") else PLANE_DIM

    overhead = p["el"] >= 75
    t.fillCircle(cx, cy, r, PANEL)
    # faint "slice of sky" sector + bright rim arc at the look bearing.
    # Hidden when straight up: azimuth is meaningless near the zenith.
    a = (p["az"] - vu) + 180                       # TFT_eSPI arc angle: 0 = 6 o'clock
    if not overhead:
        t.drawArc(cx, cy, r, 0, a - 14, a + 14, PLANE_FAINT)
    t.drawDashedCircle(cx, cy, round(r * 2 / 3), HAIR)
    t.drawDashedCircle(cx, cy, round(r / 3), HAIR)
    t.drawCircle(cx, cy, r, HAIR)
    if not overhead:
        t.drawArc(cx, cy, r + 1, r - 4, a - 14, a + 14, accent)
    else:
        t.drawCircle(cx, cy, 21, accent)           # "look here" ring at zenith,
        t.drawCircle(cx, cy, 20, accent)           # wide enough to frame the glyph
    gx, gy = dome_xy(p["az"], p["el"], vu, clamp=True)
    # horizon label just outside the rim, lower-left
    t.drawString("HORIZON", 4, 199, "glcd", DIM)

    # compass letters inside the rim (rotate with VIEW_UP_DEG)
    for lab, az in (("N", 0), ("E", 90), ("S", 180), ("W", 270)):
        th = math.radians(az - vu)
        lx, ly = cx + (r - 11) * math.sin(th), cy - (r - 11) * math.cos(th)
        if math.hypot(lx - gx, ly - gy) < 16:      # never draw a letter under the glyph
            continue
        t.drawString(lab, round(lx), round(ly) + 5, "f2", TEXT if lab == "N" else DIM, "C")
    if vu:
        # "ahead" caret + label at the top of the rim when the dome is you-relative
        t.fillTriangle(cx - 5, cy - r - 8, cx + 5, cy - r - 8, cx, cy - r - 1, TEXT)
        t.drawString("AHEAD", cx + 9, cy - r - 1, "glcd", MUTED)
    # zenith marker + label (both hidden when the glyph is on top of them)
    if math.hypot(gx - cx, gy - cy) > 20:
        t.fillCircle(cx, cy, 2, MUTED)
        t.drawString("UP", cx + 5, cy + 11, "glcd", DIM)

    # other qualifying aircraft: hollow dots
    for o in p.get("others", []):
        ox, oy = dome_xy(o["az"], o["el"], vu)
        t.drawCircle(round(ox), round(oy), 4, MUTED)
        t.drawCircle(round(ox), round(oy), 3, MUTED)

    # Direction arrow: FIXED length (12 px gap, 20 px shaft, 8 px head) pointing
    # at the +60 s sky position. Direction matters, length doesn't. Hidden only
    # if the plane moves < 3 px on the dome in 60 s (no readable direction).
    x0, y0 = gx, gy
    heading = p["track"] - vu
    if p.get("trail") and state != "stale":
        # direction from the TRUE (unclamped) sky positions, now -> +60 s
        rx, ry = dome_xy(p["az"], p["el"], vu)
        x1, y1 = dome_xy(p["trail"]["az"], p["trail"]["el"], vu)
        L = math.hypot(x1 - rx, y1 - ry)
        if L >= 3:
            ux, uy = (x1 - rx) / L, (y1 - ry) / L
            heading = math.degrees(math.atan2(ux, -uy))   # glyph points where it moves on the dome
            # distance along u until the rim (minus 3 px): solve |g + u*s - c| = r - 3
            bx, by = x0 - cx, y0 - cy
            bdu = bx * ux + by * uy
            s_rim = -bdu + math.sqrt(max(0.0, bdu * bdu - (bx * bx + by * by - (r - 3) ** 2)))
            tip = min(40.0, s_rim)
        if L >= 3 and tip >= 14:
            hx, hy = x0 + ux * (tip - 8), y0 + uy * (tip - 8)
            tx, ty = x0 + ux * tip, y0 + uy * tip
            if tip >= 24:                                    # shaft only if there's room
                t.drawWideLine(x0 + ux * 12, y0 + uy * 12, hx, hy, 2, accent)
            t.fillTriangle(tx, ty, hx - uy * 5, hy + ux * 5, hx + uy * 5, hy - ux * 5, accent)
    plane_glyph(t, x0, y0, heading, 1.0, accent)   # stale = solid PLANE_DIM


def header(t, p):
    t.fillRect(0, 0, 320, 44, PANEL)
    t.fillRect(0, 0, 4, 44, PLANE if p.get("state") in (None, "live", "forced") else PLANE_DIM)
    # right-side pill
    pill = p.get("pill")
    right = 314
    if pill:
        txt, col = pill
        tok = "f2" if txt[0] == "+" else "glcd"
        w = t.textWidth(txt, tok) + 16
        t.drawRoundRect(right - w, 8, w, 20, 10, col)
        t.drawString(txt, right - w // 2, 22 if tok == "f2" else 21, tok, col, "C")
        right -= w + 8
    # line 1: <operator> . <type>, fitted as ONE line (type is priority #2).
    # fsb12 -> fsb9 for the whole line; still too wide -> truncate the operator.
    x, avail = 12, right - 12
    op, typ = p.get("op", ""), p["type"]
    sep = 14 if op else 0
    tok = "fsb9"
    for cand in ("fsb12", "fsb9"):
        if t.textWidth(op, cand) + sep + t.textWidth(typ, cand) <= avail:
            tok = cand
            break
    if op:
        w = draw_fit(t, op, x, 24, avail - sep - t.textWidth(typ, tok), TEXT, (tok,))
        x += w + 6
        dot(t, x, 24 - (6 if tok == "fsb12" else 4), MUTED, 2)
        x += 8
    draw_fit(t, typ, x, 24, right - x, TEXT, (tok,))
    if p.get("state") == "forced":
        t.drawString(f'weather in {p["forced_left"]} s', 314, 39, "f2", MUTED, "R")
    # line 2: flight / registration (secondary)
    x = 12
    for i, s in enumerate(p["sub"]):
        if i:
            dot(t, x + 4, 34, DIM)
            x += 10
        x += t.drawString(s, x, 39, "f2", MUTED)


def look_block(t, p):
    x = COL_X
    state = p.get("state", "live")
    confident = state in ("live", "forced")
    accent = PLANE if confident else PLANE_DIM
    body = TEXT if confident else MUTED        # stale/departing never look certain
    rel = bool(p.get("view_up"))
    y0, y1 = (93, 123) if rel else (97, 130)      # you-relative mode needs a 4th line
    t.drawString("LOOK", x + 1, 58 if rel else 60, "glcd", DIM)
    el = p["el"]
    if el >= 75:
        t.drawString("UP", x - 1, y0, "fsb24", accent)
        t.drawString("overhead", x + 1, y1 - 3, "fsb12", body)
        line3 = "lean back"
    else:
        t.drawString(geo.compass8(p["az"]), x - 1, y0, "fsb24", accent)
        w = temp(t, x, y1, round(el), "fsb18", body)
        t.drawString("up", x + w + 5, y1, "fsb18", body)
        if el < 10:
            line3 = "near horizon"
        else:
            f = round(el / 10)
            line3 = f"about {f} fist{'s' if f > 1 else ''}"
    dist = f'{p["dist_mi"]:.1f} mi'
    if state == "stale":
        t.drawString(f'last seen {p["seen"]} s ago', x + 1, 149, "f2", WARN)
    elif rel:
        # you-relative: the answer a normal person uses, big and bright.
        # Distance moves to the dome's top-left corner (outside the rim).
        t.drawString(geo.relative_words(p["az"], p["view_up"]), x, 150, "fsb12", body)
        t.drawString(dist, 4, 58, "f2", MUTED)
    else:
        w = t.drawString(line3, x + 1, 149, "f2", MUTED)
        dot(t, x + w + 6, 144, DIM)
        t.drawString(dist, x + w + 11, 149, "f2", MUTED)
    t.drawFastHLine(x, 157, COL_W, HAIR)


CITY_SHORT = {"Dallas-Fort Worth": "Dallas", "Salt Lake City": "Salt Lake", "Minneapolis": "Mpls",
              "San Francisco": "San Fran", "Washington": "Wash.", "Philadelphia": "Philly"}


def fit_cities(t, oc, dc, avail):
    """Short-city table first, then trim the longer name and end it with '.'."""
    if t.textWidth(oc, "f2") + t.textWidth(dc, "f2") <= avail:
        return oc, dc
    oc, dc = CITY_SHORT.get(oc, oc), CITY_SHORT.get(dc, dc)
    while t.textWidth(oc, "f2") + t.textWidth(dc, "f2") > avail:
        if max(len(oc), len(dc)) <= 2:     # guard: can't shrink further
            break
        if len(oc) >= len(dc):
            oc = oc[:-2].rstrip(" .") + "."
        else:
            dc = dc[:-2].rstrip(" .") + "."
    return oc, dc


def route_block(t, p):
    x = COL_X
    r = p.get("route")
    kind = r["kind"] if r else "none"
    if kind in ("ok", "unverified"):
        c = TEXT if kind == "ok" else DIM
        w = t.drawString(r["o"], x, 181, "fsb12", c)
        arrow_right(t, x + w + 5, 175, 18, c)
        t.drawString(r["d"], x + w + 28, 181, "fsb12", c)
        if kind == "ok":
            oc, dc = fit_cities(t, r["oc"], r["dc"], COL_W - 1 - 18)
            w = t.drawString(oc, x + 1, 197, "f2", MUTED)
            arrow_right(t, x + w + 4, 192, 10, MUTED, head=3, wd=1)
            t.drawString(dc, x + w + 18, 197, "f2", MUTED)
        else:
            lab = "ROUTE UNVERIFIED"
            tw = t.textWidth(lab, "glcd") + 10
            t.drawRoundRect(x, 187, tw, 13, 3, WARN)
            t.drawString(lab, x + 5, 197, "glcd", WARN)
    elif kind == "unknown":
        t.drawString("Route unknown", x, 180, "fs9", MUTED)
        t.drawString("no schedule found", x + 1, 197, "f2", DIM)
    else:  # GA / private
        t.drawString("Private flight", x, 180, "fs9", MUTED)
        t.drawString("no route filed", x + 1, 197, "f2", DIM)


def stats_bar(t, p):
    t.fillRect(0, 202, 320, 38, PANEL)
    state = p.get("state", "live")
    if state == "departing":
        # grace countdown: bar shrinking along the top edge (pill says LEAVING)
        frac = p["grace_left"] / 4.0
        t.fillRect(0, 202, round(320 * frac), 3, PLANE_DIM)
    if state == "forced":
        frac = p["forced_left"] / 20.0
        t.fillRect(0, 202, round(320 * frac), 3, MUTED)
    cells = [("ALTITUDE", f'{p["alt"]:,}', "ft", 10), ("SPEED", f'{p["mph"]}', "mph", 112)]
    for lab, val, unit, x in cells:
        t.drawString(lab, x, 215, "glcd", DIM)
        w = num(t, val, x, 234, "fsb9", TEXT)
        t.drawString(unit, x + w + 3, 234, "f2", MUTED)
    # vertical trend (replaces HEADING - only one compass word on screen)
    x = 196
    v = p["vrate"]
    if v > 300:
        tri_up(t, x, 233, 12, TEXT); word = "Climbing"
    elif v < -300:
        tri_down(t, x, 222, 12, TEXT); word = "Descending"
    else:
        t.fillRect(x, 226, 12, 3, TEXT); word = "Level"
    t.drawString(word, x + 17, 234, "fs9", TEXT)


def plane(t: TFT, p: dict):
    t.fillScreen(BG)
    header(t, p)
    dome(t, p)
    look_block(t, p)
    route_block(t, p)
    stats_bar(t, p)


# ---------------------------------------------------------------------------
#  BOOT screen
# ---------------------------------------------------------------------------
def boot(t: TFT, d: dict):
    t.fillScreen(BG)
    w = t.textWidth("SkyDesk", "fsb18")
    x = 160 - (w + 30) // 2
    plane_glyph(t, x + 11, 45, 45, 1.2, PLANE)
    t.drawString("SkyDesk", x + 30, 56, "fsb18", TEXT)
    t.drawString(d["ver"], 160, 76, "glcd", DIM, "C")
    t.fillRoundRect(30, 90, 260, 104, 8, PANEL)
    y = 110
    for status, label, detail in d["steps"]:
        if status == "ok":
            check(t, 46, y - 9, OK)
        elif status == "fail":
            cross(t, 46, y - 9, ERR)
        elif status == "busy":
            for i in range(3):
                t.fillCircle(47 + i * 4, y - 4, 1, PLANE)
        else:
            t.drawCircle(50, y - 5, 4, DIM)
        col = {"ok": TEXT, "fail": TEXT, "busy": TEXT, "todo": DIM}[status]
        w = t.drawString(label, 64, y, "f2", col)
        if detail:
            t.drawString(detail, 280, y, "f2", ERR if status == "fail" else MUTED, "R")
        y += 22 if status != "fail" else 18
        if status == "fail" and d.get("retry"):
            t.drawString(d["retry"], 64, y, "f2", MUTED)
            y += 22
    for i, line in enumerate(d.get("hint", [])):
        t.drawString(line, 160, 214 + i * 18, "f2", MUTED if i == 0 else DIM, "C")


# ---------------------------------------------------------------------------
#  SETUP: which way do you face?
# ---------------------------------------------------------------------------
def setup_facing(t: TFT, d: dict):
    t.fillScreen(BG)
    t.drawString("Which way do you face?", 160, 22, "fsb9", TEXT, "C")
    t.drawString("Stand where you'd watch SkyDesk.", 160, 40, "f2", MUTED, "C")
    cx, cy, r = 160, 132, 58
    vu = d["view_up"]
    t.fillCircle(cx, cy, r, PANEL)
    t.drawCircle(cx, cy, r, HAIR)
    for lab, az in (("N", 0), ("E", 90), ("S", 180), ("W", 270)):
        th = math.radians(az - vu)
        t.drawString(lab, round(cx + (r - 12) * math.sin(th)), round(cy - (r - 12) * math.cos(th)) + 5,
                     "fsb9", PLANE if lab == "N" else MUTED, "C")
    t.fillTriangle(cx - 9, cy - r - 10, cx + 9, cy - r - 10, cx, cy - r + 2, TEXT)
    t.drawString("YOU FACE", cx + 14, cy - r - 2, "glcd", MUTED)
    temp(t, cx, cy + 7, vu, "fsb12", TEXT, "C")
    t.drawString(geo.compass16(vu), cx, cy + 24, "f2", MUTED, "C")
    # big rotate buttons
    for bx, sgn in ((14, -1), (250, 1)):
        t.fillRoundRect(bx, 104, 56, 48, 8, PANEL2)
        t.drawString("-15" if sgn < 0 else "+15", bx + 28, 134, "fsb9", TEXT, "C")
    t.fillRoundRect(110, 202, 100, 30, 15, PLANE)
    t.drawString("Done", 160, 223, "fsb9", BG, "C")
    t.drawString("Use a phone", 42, 172, "glcd", DIM, "C")
    t.drawString("compass", 42, 182, "glcd", DIM, "C")


# ---------------------------------------------------------------------------
#  Scenario data
# ---------------------------------------------------------------------------
def aircraft(az, ground_nm, alt, track, gs, vrate, **kw):
    ac = geo.place(OBS, az, ground_nm, alt)
    a, el, d = geo.look(OBS, ac)
    fut = geo.ahead(ac, track, gs, 60, vrate)
    fa, fel, _ = geo.look(OBS, fut)
    p = dict(az=a, el=el, dist_mi=d / 1609.34, alt=int(round(alt, -2)), mph=round(gs * 1.15078),
             track=track, vrate=vrate, trail=dict(az=fa, el=fel))
    p.update(kw)
    return p


def other(az, ground_nm, alt):
    a, el, _ = geo.look(OBS, geo.place(OBS, az, ground_nm, alt))
    return dict(az=a, el=el)


HOURLY_DAY = [dict(h="4PM", kind="clear", t=95, pop=0), dict(h="5PM", kind="clear", t=93, pop=5),
              dict(h="6PM", kind="partly", t=90, pop=10), dict(h="7PM", kind="cloud", t=86, pop=20),
              dict(h="8PM", kind="rain", t=83, pop=40, day=False), dict(h="9PM", kind="cloud", t=81, pop=25)]
HOURLY_NIGHT = [dict(h=h, kind="clear", t=tt, pop=0, day=False) for h, tt in
                (("10PM", 74), ("11PM", 72), ("12AM", 71), ("1AM", 70), ("2AM", 70), ("3AM", 69))]

SCENARIOS = {
    "weather": (weather, dict(date="THU  SEP 24", kind="partly", day=True, temp=94, cond="Mostly sunny",
                              time="3:42", ampm="PM", sun_evt="Sunset 6:20", feels=97, hi=99, lo=76, hum=18,
                              wind="NW 7", hourly=HOURLY_DAY, traffic=("4", "737-800  5.2 mi W"))),
    "weather-degraded": (weather, dict(date="THU  SEP 24", kind="partly", day=False, temp=77, cond="Partly cloudy",
                                       time="9:18", ampm="PM", sun_evt="Sunrise 6:17", feels=77, hi=86, lo=72, hum=49,
                                       wind="N 4", hourly=HOURLY_NIGHT, traffic=None, radar_retry="retrying in 30 s",
                                       stale="Updated 47 min ago")),
    "plane-airliner-multi": (plane, aircraft(45, 2.05, 12400, 20, 250, 1800, op="Southwest", type="737-800",
                                             sub=["WN 2208", "N8563Z"], pill=("+1 more", PLANE),
                                             route=dict(kind="ok", o="PHX", d="DEN", oc="Phoenix", dc="Denver"),
                                             others=[other(205, 2.6, 9000)])),
    "plane-unverified": (plane, aircraft(290, 1.0, 4200, 270, 160, -900, op="Southwest", type="737-700",
                                         sub=["WN 1637", "N429WN"],
                                         route=dict(kind="unverified", o="PIT", d="TPA"))),
    "plane-overhead": (plane, aircraft(120, 0.35, 33000, 250, 460, 0, op="American", type="A321",
                                       sub=["AA 1822", "N135NN"], route=dict(kind="unknown"))),
    "plane-departing": (plane, aircraft(30, 4.7, 15800, 20, 260, 1500, op="Southwest", type="737-800",
                                        sub=["WN 2208", "N8563Z"], state="departing", grace_left=3,
                                        pill=("LEAVING", MUTED),
                                        route=dict(kind="ok", o="PHX", d="DEN", oc="Phoenix", dc="Denver"))),
    "plane-forced": (plane, aircraft(265, 4.5, 3800, 80, 150, -700, op="Frontier", type="A320neo",
                                     sub=["F9 1453", "N364FR"], state="forced", forced_left=18,
                                     pill=("NOT OVERHEAD", MUTED),
                                     route=dict(kind="ok", o="LAS", d="PHX", oc="Las Vegas", dc="Phoenix"))),
    "plane-stale": (plane, aircraft(215, 0.5, 2900, 80, 105, 0, type="Cessna 172", sub=["N172SP"],
                                    state="stale", seen=8, route=None)),
    "plane-ga-rotated": (plane, aircraft(20, 0.7, 3600, 150, 110, 400, type="Piper PA-28", sub=["N4312K"],
                                         view_up=200, route=None)),
    "boot-ok": (boot, dict(ver="v1.0.0", steps=[("ok", "WiFi", "HomeNet  -58 dBm"), ("ok", "Clock", "3:41 PM"),
                                                 ("busy", "Weather", ""), ("todo", "Traffic", "")],
                           hint=["Gilbert, AZ  33.35, -111.79"])),
    "boot-error": (boot, dict(ver="v1.0.0", steps=[("fail", 'Can\'t join "HomeNet"', ""), ("todo", "Clock", ""),
                                                    ("todo", "Weather, traffic", "")],
                              retry="Retrying in 12 s  (attempt 3)",
                              hint=["Check WIFI_SSID / WIFI_PASSWORD in secrets.h", "2.4 GHz networks only"])),
    "setup-facing": (setup_facing, dict(view_up=200)),
    "plane-worst-eagle": (plane, aircraft(160, 1.6, 7200, 300, 230, -1200, op="American Eagle", type="ERJ-175",
                                          sub=["AA 5821", "N401YX"], pill=("+2 more", PLANE),
                                          route=dict(kind="ok", o="DFW", d="PHX", oc="Dallas-Fort Worth",
                                                     dc="Phoenix"),
                                          others=[other(250, 2.4, 11000), other(10, 2.8, 16000)])),
    "plane-worst-alaska": (plane, aircraft(305, 5.8, 5200, 120, 180, -800, op="Alaska", type="737 MAX 9",
                                           sub=["AS 612", "N913AK"], state="forced", forced_left=11,
                                           pill=("NOT OVERHEAD", MUTED),
                                           route=dict(kind="ok", o="SLC", d="PHX", oc="Salt Lake City",
                                                      dc="Phoenix"))),
}

HOURLY_STORM = [dict(h="8PM", kind="rain", t=93, pop=40, day=False), dict(h="9PM", kind="rain", t=88, pop=50, day=False),
                dict(h="10PM", kind="cloud", t=86, pop=30, day=False), dict(h="11PM", kind="cloud", t=85, pop=20, day=False),
                dict(h="12AM", kind="clear", t=84, pop=10, day=False), dict(h="1AM", kind="clear", t=83, pop=5, day=False)]
def _cue(cond, kind, cue, day=False):
    return lambda: dict(SCENARIOS["weather"][1], date="THU  JUL 31", temp=97, hourly=HOURLY_STORM, hum=31,
                        wind="W 14", hi=104, lo=84, feels=99, cond=cond, time="7:30", sun_evt="Sunset 7:34",
                        kind=kind, day=day, rain_cue=cue() if callable(cue) else cue)


_REAL_CUE = lambda: __import__("radar_screen").cue_from("scattered")   # noqa: E731  real frame: 'rain 10 mi W'
WEATHER_V3 = {   # cue text computed from the real archived frames (radar_screen.cue_from)
    "weather-rain-cue": (weather, _cue("Mostly cloudy", "cloud", _REAL_CUE, True)),
    "weather-cue-thunderstorm": (weather, _cue("Thunderstorm", "rain", _REAL_CUE)),
    "weather-cue-freezing": (weather, _cue("Freezing drizzle", "rain", _REAL_CUE)),
    "weather-cue-hail": (weather, _cue("Storm, hail", "rain", _REAL_CUE)),
    "weather-cue-here": (weather, _cue("Mostly cloudy", "cloud", "Raining here")),
}


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else "r2"
    flt = sys.argv[2] if len(sys.argv) > 2 else ""
    OUT.mkdir(exist_ok=True)
    import map_screen   # v2 plane map (docs/08-plane-map.md); data built lazily
    scen = dict(SCENARIOS)
    scen.update({k: (fn, mk) for k, (fn, mk) in map_screen.SCENARIOS.items()})
    scen.update(map_screen.SCENARIOS_V3)
    import radar_screen  # v3 rain radar (docs/10-rain-radar.md)
    scen.update(radar_screen.SCENARIOS)
    scen.update(WEATHER_V3)
    scen["weather-rain-cue-night"] = (radar_screen.night(weather), WEATHER_V3["weather-rain-cue"][1])
    for name, (fn, data) in scen.items():
        if flt and flt not in name:
            continue
        if callable(data):
            data = data()
        t = TFT(BG)
        fn(t, data)
        t.save(OUT / f"{prefix}-{name}.png", OUT / f"{prefix}-{name}@3x.png")
        extra = ""
        if "el" in data:
            extra = f'  az={data["az"]:.0f} el={data["el"]:.1f} d={data["dist_mi"]:.2f}mi'
        print("rendered", f"{prefix}-{name}{extra}")


if __name__ == "__main__":
    main()
