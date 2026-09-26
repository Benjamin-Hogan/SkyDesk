"""Today's Sky (3.0 feature 1, docs/11-today.md): the Today page and the weather chip's
"passed" state, drawn with tftsim. Imported by screens.py.

The Today page draws straight to the TFT like the weather screen (no sprite), so the
route arrow may use the same primitive as the plane card.

Mock data: aircraft, operators and types are REAL - taken from the captured adsb.fi
fixtures in test/host/fixtures (BAW3GB A35K, SWA B38M, AAL A21N, NDU736 P28A,
KYOTE048 H60 ...). Routes are illustrative (adsbdb isn't captured). The hourly counts are an
illustrative day shaped like PHX west flow, not a recorded one - there is no log yet.

Round 2 (answers design-review/3.0-today-round-1-mr-stacks.md):
  M1 the chip opens the MAP in every state (passed -> the map focuses that plane)
  M2 row time "7:21" + glcd "PM"; airline-code fallback keeps the route; plausible routes only
  M3 the header row (y 0-36) is the Today entry: "31 overhead >" after the date
  M4 Traffic offline / unsynced clock / lost set ("212+") are said, outage hours are dotted
  M5 LAST PASSES survives midnight; only the counts and bars reset
"""
from __future__ import annotations

import sys

S = sys.modules["__main__"] if hasattr(sys.modules.get("__main__"), "plane_glyph") else __import__("screens")
import map_screen as M   # chevrons (fillTriangle only)

PASSED_MAX_MIN = 10          # TODAY_PASSED_S / 60, counted from the pass's CLOSE (S3)
BAR_X0, BAR_W, BAR_STEP = 16, 11, 12
BAR_BASE, BAR_MAX = 128, 30
ROW_Y = (177, 201, 225)
RIGHT_X = 310
GAP = 12                     # S2: at least this between the words and the right column


# ---------------------------------------------------------------------------
#  Weather chip, "passed" state (chipMessage() row 2). Tap -> the map (M1).
# ---------------------------------------------------------------------------
def passed_left(mins: int) -> str:
    return "Passed just now" if mins < 1 else f"Passed {mins} min ago"


def fit_right(t, avail: int, op: str, typ: str, icao: str, code: str = "") -> str:
    """Operator + type, code + type, type, ICAO (docs/11 -> chip; == chipMessage())."""
    for s in (f"{op} {typ}" if op else typ, f"{code} {typ}" if code else "", typ, icao):
        if s and t.textWidth(s, "f2") <= avail:
            return s
    return icao


def passed_chip(t, p: dict):
    """p: mins, op, type, icao, code.  Chip panel already filled by weather()."""
    S.plane_glyph(t, 21, 226, 45, 0.55, S.PLANE)
    left = passed_left(p["mins"])
    lw = t.drawString(left, 36, 231, "f2", S.TEXT)
    avail = 296 - (36 + lw + 12)
    right = fit_right(t, avail, p.get("op", ""), p["type"], p["icao"], p.get("code", ""))
    t.drawString(right, 296, 231, "f2", S.MUTED, "R")
    S.chevron(t, 302, 226, S.MUTED)


# ---------------------------------------------------------------------------
#  Today page
# ---------------------------------------------------------------------------
def fit_font(t, text, max_w, toks):
    for tok in toks:
        if t.textWidth(text, tok) <= max_w:
            return tok
    return toks[-1]


def route_w(t, o, d):
    return t.textWidth(o, "f2") + 4 + 14 + 4 + t.textWidth(d, "f2")


def draw_route(t, x_right, y, o, d):
    """'PHX -> DEN' right-aligned, MUTED (S2), arrow drawn (U+2192 is not in the fonts)."""
    x = x_right - route_w(t, o, d)
    x += t.drawString(o, x, y, "f2", S.MUTED) + 4
    S.arrow_right(t, x, y - 5, 14, S.MUTED, head=4, wd=1)
    x += 14 + 4
    t.drawString(d, x, y, "f2", S.MUTED)


TIME_COL = None


def time_col(t):
    """Widest time cell: '12:59' f2 + 2 + 'PM' glcd."""
    return t.textWidth("12:59", "f2") + 2 + t.textWidth("PM", "glcd")


def header_entry_fit(t, overhead: int, date_end: int, status_left: int) -> str:
    """== headerEntryFit() (chip.cpp): '31 overhead', else '31', else '' (target stays)."""
    num = (f"{overhead // 1000}k" if overhead >= 10000 else
           f"{overhead // 1000}.{overhead % 1000 // 100}k" if overhead >= 1000 else str(overhead))
    x = date_end + 16
    limit = status_left - (12 if status_left < 320 else 4)
    for c in (f"{num} overhead", num):
        if x + t.textWidth(c, "f2") + 11 <= limit:
            return c
    return ""


def row(t, y, r):
    """One pass: time | words | route (plausible only), else registration/callsign.
    One font (f2). Chain (M2): 'Op Type'+right, 'Code Type'+right, 'Op Type',
    'Type'+right, 'Type'."""
    hm, ap = r["time"].split()
    tw = t.drawString(hm, 10, y, "f2", S.DIM if r.get("yday") else S.MUTED)   # an earlier day: DIM
    t.drawString(ap, 10 + tw + 2, y, "glcd", S.DIM)
    x = 10 + time_col(t) + 8
    if r.get("route"):
        rw = route_w(t, *r["route"])
    else:
        rw = t.textWidth(r["reg"], "f2")
    typ = r["type"]
    cands = []
    if r.get("op"):
        cands.append((f'{r["op"]} {typ}', True))
        if r.get("code"):
            cands.append((f'{r["code"]} {typ}', True))
        cands.append((f'{r["op"]} {typ}', False))
    cands += [(typ, True), (typ, False)]
    text, right = typ, False
    for cand, with_right in cands:
        if t.textWidth(cand, "f2") <= RIGHT_X - x - (rw + GAP if with_right else 0):
            text, right = cand, with_right
            break
    t.drawString(text, x, y, "f2", S.TEXT)
    if right and r.get("route"):
        draw_route(t, RIGHT_X, y, *r["route"])
    elif right:
        t.drawString(r["reg"], RIGHT_X, y, "f2", S.MUTED, "R")


def dotted(t, x, y, w, c):
    for i in range(0, w, 2):
        t.fillRect(x + i, y, 1, 1, c)


def stipple(t, x, y, w, h, c):
    """3 px checkerboard: an outage hour (round 2 S1) - visible at 2 m, unlike a dotted line."""
    for yy in range(h):
        for xx in range((yy % 2), w, 2):
            t.fillRect(x + xx, y + yy, 1, 1, c)


def hourly(t, bars: list[int], now_h: int, outage=()):
    t.fillRoundRect(6, 80, 308, 64, 6, S.PANEL)
    peak = max(bars) if bars else 0
    scale = max(peak, 4)
    # fixed title with the peak folded in (round 2 N1): no floating label, no collision
    title = f"PASSES BY HOUR  -  PEAK {peak}" if peak else "PASSES BY HOUR"
    t.drawString(title, 12, 90, "glcd", S.DIM)
    for h in range(24):
        x = BAR_X0 + h * BAR_STEP
        n = bars[h] if h < len(bars) and h <= now_h else 0
        if n == 0:
            t.drawFastHLine(x, BAR_BASE, BAR_W, S.HAIR)
        else:
            hgt = max(3, round(n / scale * BAR_MAX))
            t.fillRect(x, BAR_BASE - hgt + 1, BAR_W, hgt, S.TEXT if h == now_h else S.MUTED)
        if h in outage:                                  # not watching != quiet: the bar stays
            stipple(t, x, BAR_BASE + 2, BAR_W, 3, S.DIM)
        if h == now_h:                                   # S1: "now", even at zero
            t.fillRect(x, BAR_BASE + 2, BAR_W, 2, S.TEXT)
    for h, lab in ((0, "12A"), (6, "6A"), (12, "12P"), (18, "6P")):
        t.drawString(lab, BAR_X0 + h * BAR_STEP, 141, "glcd", S.MUTED)


def short_count(n: int) -> str:
    return f"{n // 1000}k" if n >= 10000 else f"{n // 1000}.{n % 1000 // 100}k" if n >= 1000 else str(n)


def today(t, d: dict):
    t.fillScreen(S.BG)
    # chrome: back pill + title; right = date, or a degraded status (R1-15), worst first
    t.fillRoundRect(4, 4, 36, 24, 12, S.PANEL2)
    M.chevron_left(t, 17, 16, S.TEXT)
    t.drawString("Today", 48, 23, "fsb12", S.TEXT)
    status = d.get("status")
    if status:
        t.drawString(status, 296, 21, "f2", S.WARN, "R")
        t.fillCircle(305, 16, 3, S.WARN)
    else:
        t.drawString(d["date"], 312, 21, "f2", S.MUTED, "R")

    # stats row (label glcd DIM above the value, as the weather detail cells)
    unsynced = d.get("unsynced")                         # nothing is counted before the clock
    t.drawString("OVERHEAD", 10, 42, "glcd", S.DIM)
    S.num(t, "--" if unsynced else str(d["overhead"]), 10, 70, "fsb18", S.MUTED if unsynced else S.TEXT)
    t.drawString("WITHIN 14 MI", 96, 42, "glcd", S.DIM)
    nb = "--" if unsynced else ("~" if d.get("nearby_lost") else "") + short_count(d["nearby"])
    S.num(t, nb, 96, 70, "fsb18", S.MUTED if d.get("nearby_lost") or unsynced else S.TEXT)
    if d.get("nearby_lost"):                             # at most this: re-seen planes may count twice
        t.drawString("since reboot", 96, 78, "glcd", S.DIM)
    t.drawString("RAREST", 190, 42, "glcd", S.DIM)
    r = d["rarest"]
    if r.get("state"):                                    # learning / needs SD / none new
        t.drawString(r["state"], 190, 64, "fs9", S.MUTED)
        if r.get("sub"):
            t.drawString(r["sub"], 190, 76, "glcd", S.DIM)   # clear of the panel (y 80)
    else:
        tok = fit_font(t, r["value"], 124, ("fsb18", "fsb12", "fsb9"))
        S.num(t, r["value"], 190, 70, tok, S.TEXT)

    hourly(t, d["bars"], d["now_h"], d.get("outage", ()))

    yday = any(r.get("yday") for r in d["passes"])
    t.drawString("LAST PASSES  -  SINCE YESTERDAY" if yday else "LAST PASSES", 10, 158, "glcd", S.DIM)
    if d.get("unsynced"):
        t.drawString("Waiting for clock", 160, 196, "f2", S.MUTED, "C")
    elif not d["passes"]:
        t.drawString("No overhead passes yet today", 160, 196, "f2", S.MUTED, "C")
    for y, r in zip(ROW_Y, d["passes"]):
        row(t, y, r)


def touch_overlay(fn):
    """Review aid: the weather screen's tap zones (docs/11 -> Surfaces)."""
    def draw(t, d):
        fn(t, d)
        for (x, y, w, h), c in (((0, 0, 320, 36), S.OK), ((0, 36, 320, 76), S.RAIN), ((6, 215, 308, 23), S.PLANE)):
            t.drawRect(x, y, w, h, c)
            t.drawRect(x + 1, y + 1, w - 2, h - 2, c)
    return draw


# ---------------------------------------------------------------------------
#  Scenarios
# ---------------------------------------------------------------------------
# 31 passes by 7:42 PM; the evening cluster is the westbound departure push (illustrative)
BUSY_BARS = [0, 0, 0, 0, 0, 1, 2, 3, 1, 0, 1, 2, 1, 0, 0, 2, 4, 6, 5, 3]
PASSES = [
    dict(time="7:38 PM", op="Southwest", code="WN", type="737 MAX 8", icao="B38M", route=("PHX", "OAK")),
    dict(time="7:21 PM", op="British Airways", code="BA", type="A350-1000", icao="A35K", route=("PHX", "LHR")),
    dict(time="7:02 PM", op="", type="Piper PA-28", icao="P28A", reg="N4312K"),
]
WORST_PASSES = [
    dict(time="12:59 PM", op="American Eagle", code="AA", type="ERJ-175", icao="E75L", route=("DFW", "PHX")),
    dict(time="12:47 PM", op="British Airways", code="BA", type="A350-1000", icao="A35K", route=("PHX", "LHR")),
    dict(time="12:40 PM", op="", type="Sikorsky Black Hawk", icao="H60", reg="KYOTE048"),
]
MIDNIGHT_PASSES = [
    dict(time="12:06 AM", op="Southwest", code="WN", type="737-800", icao="B738", route=("LAS", "PHX")),
    dict(time="11:58 PM", op="Frontier", code="F9", type="A321neo", icao="A21N", route=("PHX", "DEN"), yday=True),
    dict(time="11:31 PM", op="American", code="AA", type="A321", icao="A321", route=("PHX", "JFK"), yday=True),
]

BASE = dict(date="THU  SEP 24", overhead=31, nearby=212, now_h=19, bars=BUSY_BARS, passes=PASSES,
            rarest=dict(value="A350-1000"))

HOURLY_EVENING = [dict(h=h, kind="clear", t=tt, pop=0, day=False) for h, tt in
                  (("8PM", 86), ("9PM", 84), ("10PM", 82), ("11PM", 80), ("12AM", 78), ("1AM", 77))]
WEATHER_PASSED = dict(S.SCENARIOS["weather"][1], time="7:40", sun_evt="Sunrise 6:17", day=False, kind="clear",
                      temp=88, cond="Clear", hourly=HOURLY_EVENING)

SCENARIOS = {
    "weather-passed": (S.weather, dict(WEATHER_PASSED, passed=dict(mins=2, op="Southwest", type="737 MAX 8",
                                                                   icao="B38M"))),
    "weather-passed-now-ga": (S.weather, dict(WEATHER_PASSED, passed=dict(mins=0, op="", type="Piper PA-28",
                                                                          icao="P28A"))),
    "weather-passed-worst": (S.weather, dict(WEATHER_PASSED, overhead=104,
                                             passed=dict(mins=9, op="British Airways", type="A350-1000",
                                                         icao="A35K", code="BA"))),
    "weather-degraded-entry": (S.weather, dict(S.SCENARIOS["weather-degraded"][1], overhead=104)),
    "weather-degraded-entry-1k": (S.weather, dict(S.SCENARIOS["weather-degraded"][1], overhead=1204,
                                                  stale="Updated 3 h ago")),
    "weather-header-entry-0": (S.weather, dict(S.SCENARIOS["weather"][1], overhead=0)),
    "weather-header-entry-104": (S.weather, dict(S.SCENARIOS["weather"][1], overhead=104)),
    "today-busy": (today, BASE),
    "today-empty": (today, dict(BASE, date="FRI  SEP 25", overhead=0, nearby=9, now_h=6, bars=[0] * 7,
                                passes=[], rarest=dict(state="learning", sub="day 2 of 3"))),
    "today-no-sd": (today, dict(BASE, status="No SD card: no log", rarest=dict(state="needs SD"))),
    "today-no-sd-rebooted": (today, dict(BASE, status="No SD card: no log", nearby_lost=True,
                                         rarest=dict(state="needs SD"))),
    "today-offline": (today, dict(BASE, status="Traffic offline", outage=(14, 15))),
    "today-unsynced": (today, dict(BASE, overhead=0, nearby=0, now_h=-1, bars=[], passes=[], unsynced=True,  # header target stays live
                                   date="", rarest=dict(state="waiting"))),
    "today-after-midnight": (today, dict(BASE, date="FRI  SEP 25", overhead=1, nearby=14, now_h=0, bars=[1],
                                         passes=MIDNIGHT_PASSES, rarest=dict(state="none new"))),
    "today-worst": (today, dict(BASE, overhead=104, nearby=1204, now_h=12, passes=WORST_PASSES,
                                bars=[0, 0, 0, 1, 0, 3, 9, 12, 11, 10, 8, 9, 7],
                                rarest=dict(value="King Air 350"))),
}
