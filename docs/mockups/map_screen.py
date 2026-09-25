"""Plane map screen (docs/08-plane-map.md), drawn with tftsim.

Uses the real pre-rendered basemaps (png/basemap-z*.png from
tools/basemap/make_basemap.py) and real captured traffic
(test/host/fixtures/*.json). Imported by screens.py.

Everything on the map lives in a 4-bit band sprite on the device, so the
whole screen is drawn with t.sprite4 = True (no anti-aliased calls).
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

from PIL import Image

import geo

S = sys.modules["__main__"] if hasattr(sys.modules.get("__main__"), "plane_glyph") else __import__("screens")

ROOT = Path(__file__).resolve().parents[2]
FIX = ROOT / "test" / "host" / "fixtures"

# == PALETTE in tools/basemap/make_basemap.py (MapPal enum order)
MAP_PAL = ["#0A1120", "#1A2436", "#2A3750", "#3C4A66", "#6F7E9C", "#173452", "#4A5670", "#EEF2F8",
           "#9AA6BE", "#7886A2", "#FFB02E", "#8A6424", "#2B2720", "#131C2E", "#5CC8FF", "#FF8A3D"]
(M_BG, M_PRIMARY, M_TRUNK, M_MOTORWAY, M_RUNWAY, M_WATER, M_TRAIL, M_TEXT, M_MUTED, M_DIM,
 M_PLANE, M_PLANE_DIM, M_ZONE, M_PANEL, M_YOU, M_WARN) = range(16)
C = MAP_PAL

MAP_CX, MAP_CY = 160, 116
RING_PX = 100
ZOOM_MI = [5, 10, 20]
ZOOMS = [RING_PX / (mi / 1.150779) for mi in ZOOM_MI]     # px per nm
STRIP_Y = 214
CLUSTER_PX = 12

TYPES = {"B737": "737-700", "B738": "737-800", "B38M": "737 MAX 8", "B39M": "737 MAX 9", "B739": "737-900",
         "A21N": "A321neo", "A321": "A321", "A320": "A320", "A20N": "A320neo", "A319": "A319",
         "B736": "737-600", "BCS3": "A220-300", "B752": "757-200", "AS50": "H125", "C172": "Cessna 172",
         "E75L": "E175", "CRJ9": "CRJ-900", "PC12": "PC-12", "A35K": "A350-1000", "H500": "MD 500",
         "HDJT": "HondaJet", "R22": "Robinson R22", "SR20": "Cirrus SR20", "PA44": "Seminole"}
AIRLINES = {"SWA": "Southwest", "AAL": "American", "FFT": "Frontier", "DAL": "Delta", "UAL": "United",
            "ASA": "Alaska", "SKW": "SkyWest", "JBU": "JetBlue", "WJA": "WestJet", "NKS": "Spirit",
            "ENY": "Envoy", "ASH": "Mesa", "BAW": "British", "SCX": "Sun Country"}
AIRPORTS = [("PHX", 33.4328, -112.0070), ("AZA", 33.3055, -111.6569), ("CHD", 33.2691, -111.8111),
            ("MSC", 33.4591, -111.7274), ("SCF", 33.6222, -111.9112), ("DVT", 33.6899, -112.0827),
            ("CGZ", 32.9549, -111.7668)]
TOWNS = [("Phoenix", 33.4484, -112.0740, True), ("Mesa", 33.4152, -111.8315, True),
         ("Chandler", 33.3062, -111.8413, True), ("Gilbert", 33.3528, -111.7890, False),
         ("Tempe", 33.4255, -111.9400, True), ("Scottsdale", 33.4942, -111.9261, True),
         ("Queen Creek", 33.2487, -111.6343, False), ("Apache Junction", 33.4150, -111.5496, False),
         ("San Tan Valley", 33.1911, -111.5280, False), ("Sun Lakes", 33.2112, -111.8754, False),
         ("Maricopa", 33.0581, -112.0476, False), ("Glendale", 33.5387, -112.1860, True),
         ("Casa Grande", 32.8795, -111.7574, True), ("Fountain Hills", 33.6117, -111.7174, False)]


def proj(lat, lon, ppn):
    """Equirectangular around the observer - identical to make_basemap.py and firmware."""
    coslat = math.cos(math.radians(S.OBS[0]))
    return MAP_CX + (lon - S.OBS[1]) * 60 * coslat * ppn, MAP_CY - (lat - S.OBS[0]) * 60 * ppn


def alt_tag(ft):
    """Plain-language altitude (review v2-R1-6): 900 / 7.2k / 12k."""
    if ft < 1000:
        return f"{int(round(ft, -2))}"
    if ft < 10000:
        return f"{ft / 1000:.1f}k"
    return f"{round(ft / 1000)}k"


def friendly(a):
    cs = a["cs"]
    al = AIRLINES.get(cs[:3]) if len(cs) > 3 and cs[:3].isalpha() and cs[3].isdigit() else None
    return (f"{al} {cs[3:]}" if al else cs), TYPES.get(a["type"], a["type"])


def qualifies(nm, el, alt):
    """v1 ENTER test (docs/01): <= 3 nm AND >= 25 deg up AND >= 300 ft above ground."""
    return nm <= 3.0 and el >= 25 and alt - S.OBS[2] >= 300


def mk(a, stale=False):
    alt = a.get("alt_geom", a.get("alt_baro", 0))
    az, el, dm = geo.look(S.OBS, {"lat": a["lat"], "lon": a["lon"], "alt": alt})
    vr = a.get("geom_rate", a.get("baro_rate", 0))
    now_q = qualifies(dm / geo.NM, el, alt)
    here = {"lat": a["lat"], "lon": a["lon"], "alt": alt}
    pop_s = pop_pt = None               # == mapWillPopSecs() v3: the PATH every 5 s (12 samples), first hit
    if not now_q and a.get("gs", 0) >= 30:
        for secs in range(5, 61, 5):
            fut = geo.ahead(here, a.get("track", 0), a.get("gs", 0), secs, vr)
            _, fel, fdm = geo.look(S.OBS, fut)
            if qualifies(fdm / geo.NM, fel, fut["alt"]):
                pop_s, pop_pt = secs, (fut["lat"], fut["lon"])
                break
    f60 = geo.ahead(here, a.get("track", 0), a.get("gs", 0), 60, vr)
    return dict(hex=a.get("hex", ""), cs=a.get("flight", "").strip() or a.get("r", a.get("hex", "")),
                type=a.get("t", ""), lat=a["lat"], lon=a["lon"], alt=alt, track=a.get("track", 0),
                gs=a.get("gs", 0), vr=vr, az=az, el=el, nm=dm / geo.NM, stale=stale or a.get("seen_pos", 0) > 15,
                inbound=pop_s is not None, pop_s=pop_s, pop_pt=pop_pt, f60=(f60["lat"], f60["lon"]), raw=a)


def load_traffic(name, stale_cs=()):
    d = json.loads((FIX / name).read_text(encoding="utf-8"))
    out = [mk(a, a.get("flight", "").strip() in stale_cs) for a in d.get("aircraft", d.get("ac", []))
           if "lat" in a and a.get("alt_baro") != "ground"]           # never draw ground traffic
    return sorted(out, key=lambda x: x["nm"])


def busy_traffic():
    """Real 35 nm sample + 12 SYNTHETIC arrivals stacked on the PHX approach (stress case)."""
    tr = load_traffic("adsbfi_gilbert_35nm.json")
    for i in range(12):
        lat, lon = geo.destination(33.4328, -112.0070, 95, (4 + i * 2.2) * geo.NM)
        alt = 2500 + i * 700
        tr.append(mk({"hex": f"syn{i:03d}", "flight": ["SWA", "AAL", "UAL", "SKW"][i % 4] + str(1100 + i * 37),
                      "t": ["B738", "A321", "B739", "E75L"][i % 4], "lat": lat + (i % 2) * 0.01, "lon": lon,
                      "alt_baro": alt, "track": 275, "gs": 170 + i * 6, "baro_rate": -600 - (i % 5) * 100}))
    return sorted(tr, key=lambda x: x["nm"])


def inbound_traffic():
    """10 nm real sample + SWA2208 at 12,000 ft heading in: it WILL qualify within 60 s."""
    tr = load_traffic("adsbfi_gilbert.json")
    ac = geo.place(S.OBS, 225, 3.6, 12000)
    tr.insert(0, mk({"hex": "a8c1d2", "flight": "SWA2208", "t": "B738", "lat": ac["lat"], "lon": ac["lon"],
                     "alt_baro": 12000, "track": 40, "gs": 250}))
    return sorted(tr, key=lambda x: x["nm"])


def advance(traffic, secs):
    """The same sky `secs` later: every aircraft flown along track / gs / vertical rate."""
    out = []
    for p in traffic:
        a = dict(p["raw"])
        f = geo.ahead({"lat": p["lat"], "lon": p["lon"], "alt": p["alt"]}, p["track"], p["gs"], secs, p["vr"])
        a.update(lat=f["lat"], lon=f["lon"], alt_baro=f["alt"])
        a.pop("alt_geom", None)
        out.append(mk(a, p["stale"]))
    return sorted(out, key=lambda x: x["nm"])


def approach_traffic():
    """10 nm real sample + SWA3319 (SYNTHETIC) 5.4 nm SW at 9,000 ft, descending, tracking NE:
    it is NOT the nearest plane, but it will pop the card in under a minute (M1 focuses it)."""
    tr = load_traffic("adsbfi_gilbert.json")
    ac = geo.place(S.OBS, 222, 5.4, 9000)
    tr.append(mk({"hex": "a4b2c9", "flight": "SWA3319", "t": "B38M", "lat": ac["lat"], "lon": ac["lon"],
                  "alt_baro": 9000, "track": 38, "gs": 240, "baro_rate": -700}))
    return sorted(tr, key=lambda x: x["nm"])


def worst_strip_traffic():
    """Worst-case strip: the longest operator in firmware's table (Sun Country) + a countdown."""
    tr = [a for a in approach_traffic() if a["cs"] != "SWA3319"]
    ac = geo.place(S.OBS, 222, 5.4, 9000)
    tr.append(mk({"hex": "a0c7e1", "flight": "SCX2417", "t": "B38M", "lat": ac["lat"], "lon": ac["lon"],
                  "alt_baro": 9000, "track": 38, "gs": 240, "baro_rate": -700}))
    return sorted(tr, key=lambda x: x["nm"])


def popped_traffic():
    """SWA3319's card has just closed: the same sky flown forward 120 s, so it has passed
    overhead and is heading away NE. The map keeps it focused for 30 s (M7)."""
    return advance(approach_traffic(), 120)


def offscreen_traffic():
    """5 mi zoom, the selected FFT1453 (SYNTHETIC) is 9 nm W - off the map, still in the poll radius."""
    tr = load_traffic("adsbfi_gilbert.json")
    ac = geo.place(S.OBS, 284, 9.0, 5200)
    tr.append(mk({"hex": "a3f1e0", "flight": "FFT1453", "t": "A20N", "lat": ac["lat"], "lon": ac["lon"],
                  "alt_baro": 5200, "track": 120, "gs": 180, "baro_rate": -800}))
    return sorted(tr, key=lambda x: x["nm"])


def tap_candidates(traffic, zi, tap, radius=28):
    """M3: planes within the tap radius, ordered by distance from the TAP (not from you)."""
    ppn = ZOOMS[zi]
    c = []
    for a in traffic:
        x, y = proj(a["lat"], a["lon"], ppn)
        dd = math.hypot(x - tap[0], y - tap[1])
        if dd <= radius:
            c.append((dd, a["cs"]))
    return [cs for _, cs in sorted(c)]


def after_pop_inbound_traffic():
    """map-after-pop plus a second inbound plane (SYNTHETIC AAL2651): will-pop outranks
    the just-passed plane (M7 precedence, review v3-R1-11)."""
    tr = popped_traffic()
    ac = geo.place(S.OBS, 150, 5.0, 8200)
    tr.append(mk({"hex": "ab12cd", "flight": "AAL2651", "t": "A321", "lat": ac["lat"], "lon": ac["lon"],
                  "alt_baro": 8200, "track": 330, "gs": 230, "baro_rate": -600}))
    return sorted(tr, key=lambda x: x["nm"])


def offscreen_inbound_traffic():
    """5 mi zoom: the soonest will-pop plane (SYNTHETIC SWA1144) is still beyond the map's
    south edge. Pointer + the on-screen part of its leader + pop square (NTH 1)."""
    tr = load_traffic("adsbfi_gilbert.json")
    ac = geo.place(S.OBS, 185, 5.6, 9500)
    tr.append(mk({"hex": "a7c3e2", "flight": "SWA1144", "t": "B38M", "lat": ac["lat"], "lon": ac["lon"],
                  "alt_baro": 9500, "track": 2, "gs": 250, "baro_rate": -500}))
    return sorted(tr, key=lambda x: x["nm"])


def cycle_scene(k):
    """z2-busy, a tap on the PHX arrival stream; tap k (1-based) selects the k-th candidate."""
    tr = busy_traffic()
    stream = [a for a in tr if a["hex"].startswith("syn")]
    x, y = proj(stream[4]["lat"], stream[4]["lon"], ZOOMS[2])
    cands = tap_candidates(tr, 2, (x + 3, y + 2))
    return dict(zoom=2, traffic=tr, selected=cands[k - 1], cycle=(k, len(cands)))


def chevron_left(t, x, y, c):
    """Back chevron from fillTriangles (no anti-aliased lines in a 4-bit sprite)."""
    t.fillTriangle(x + 7, y - 7, x + 7, y - 4, x, y, c)
    t.fillTriangle(x + 7, y - 4, x + 3, y, x, y, c)
    t.fillTriangle(x + 7, y + 7, x + 7, y + 4, x, y, c)
    t.fillTriangle(x + 7, y + 4, x + 3, y, x, y, c)


def chevron_right(t, x, y, c):
    t.fillTriangle(x, y - 5, x, y - 2, x + 5, y, c)
    t.fillTriangle(x, y - 2, x + 2, y, x + 5, y, c)
    t.fillTriangle(x, y + 5, x, y + 2, x + 5, y, c)
    t.fillTriangle(x, y + 2, x + 2, y, x + 5, y, c)


def dotted_circle(t, cx, cy, r, c, step):
    n = int(2 * math.pi * r / step)
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = round(cx + r * math.cos(a)), round(cy + r * math.sin(a))
        if 0 <= x < 320 and 0 <= y < STRIP_Y:
            t.fillRect(x, y, 1, 1, c)


def map_screen(t, d):
    zi = d["zoom"]
    ppn = ZOOMS[zi]
    state = d.get("state", "live")          # live | offline-recent | offline-cleared | loading
    t.sprite4 = True

    # 1. basemap + overhead-zone tint (index 0 -> M_ZONE inside 3 nm)
    bm = Image.open(S.OUT / f"basemap-z{zi}.png")
    px = bm.load()
    zr = 3.0 * ppn
    for y in range(max(0, int(MAP_CY - zr)), min(240, int(MAP_CY + zr) + 1)):
        half = math.sqrt(max(0.0, zr * zr - (y - MAP_CY) ** 2))
        for x in range(max(0, int(MAP_CX - half)), min(320, int(MAP_CX + half) + 1)):
            if px[x, y] == M_BG:
                px[x, y] = M_ZONE
    t.img.paste(bm.convert("RGB"))
    t.px = t.img.load()

    # 2. rings as dots in DIM (the old ring colour == trunk colour in RGB565)
    t.drawCircle(MAP_CX, MAP_CY, round(zr), C[M_PLANE_DIM])            # overhead zone rim
    dotted_circle(t, MAP_CX, MAP_CY, 4.5 * ppn, C[M_DIM], 4)           # exit ring
    dotted_circle(t, MAP_CX, MAP_CY, RING_PX, C[M_DIM], 7)             # range ring (= zoom chip)
    if state == "loading":
        dotted_circle(t, MAP_CX, MAP_CY, 12 * ppn, C[M_MUTED], 3)       # edge of data we have so far
        t.drawString("WIDENING...", round(MAP_CX + 12 * ppn * 0.72), round(MAP_CY + 12 * ppn * 0.72) + 10,
                     "glcd", C[M_MUTED])

    # 3. layout - reserve chrome, you, every glyph, then airports, tags, towns
    taken = [(0, 0, 48, 36), (246, 0, 320, 36), (146, 0, 174, 30), (0, STRIP_Y - 12, 60, 240),
             (0, STRIP_Y - 2, 320, 240), (MAP_CX - 7, MAP_CY - 7, MAP_CX + 8, MAP_CY + 8)]

    def free(r, chrome_only=False):
        rects = taken[:6] if chrome_only else taken
        return r[0] >= 0 and r[2] <= 320 and r[1] >= 0 and \
            all(r[2] <= q[0] or r[0] >= q[2] or r[3] <= q[1] or r[1] >= q[3] for q in rects)

    traffic = [] if state == "offline-cleared" else d["traffic"]
    if state == "loading":
        traffic = [a for a in traffic if a["nm"] <= 12]
    allp = []
    for a in traffic:
        x, y = proj(a["lat"], a["lon"], ppn)
        allp.append(dict(a, x=x, y=y, members=1))
    shown = [a for a in allp if -10 <= a["x"] <= 330 and -10 <= a["y"] <= STRIP_Y - 4]

    # focus (M1, M7): selected > soonest will-pop > just popped (30 s) > nearest. The strip
    # describes it; amber. It may be OFF the map (selected planes are kept to the poll
    # radius, a will-pop plane can still be beyond the 5 mi edge) -> edge pointer (M4).
    live_now = state in ("live", "loading")
    focus = next((a for a in allp if a["cs"] == d.get("selected")), None)
    if not focus and live_now:
        pops = sorted((a for a in allp if a["inbound"] and not a["stale"]), key=lambda a: a["pop_s"])
        focus = pops[0] if pops else None
    if not focus and live_now:
        focus = next((a for a in allp if a["cs"] == d.get("after_pop")), None)
    passed = focus is not None and focus["cs"] == d.get("after_pop") and not focus["inbound"]
    if not focus:
        focus = shown[0] if shown else None
    chrome = [(0, 0, 48, 36), (246, 0, 320, 36)]                       # a plane under a chip is "off the map"
    focus_on_map = focus is not None and 4 <= focus["x"] < 316 and 4 <= focus["y"] < STRIP_Y - 4 and not any(
        q[0] <= focus["x"] <= q[2] and q[1] <= focus["y"] <= q[3] for q in chrome)

    # cluster at the widest zoom: planes within CLUSTER_PX merge into one glyph + count
    if zi == 2:
        merged = []
        for a in shown:
            host = next((m for m in merged if m is not focus and
                         math.hypot(m["x"] - a["x"], m["y"] - a["y"]) < CLUSTER_PX), None)   # focus never clusters
            if host and a is not focus:
                host["members"] += 1
            else:
                merged.append(a)
        shown = merged

    gs = 0.45 if zi == 2 else 0.6
    for a in shown:
        taken.append((a["x"] - 8, a["y"] - 8, a["x"] + 8, a["y"] + 8))
    if focus and focus.get("pop_pt"):                                  # the pop point is never covered
        qx, qy = proj(*focus["pop_pt"], ppn)
        taken.append((qx - 5, qy - 5, qx + 5, qy + 5))
    for code, lat, lon in AIRPORTS:                                    # airports before tags
        x, y = proj(lat, lon, ppn)
        w = t.textWidth(code, "glcd")
        r = (x + 5, y - 4, x + 6 + w, y + 5)
        if free(r):
            taken.append(r)
            t.drawString(code, round(x + 5), round(y + 4), "glcd", C[M_MUTED])
    tags = []
    live = state in ("live", "loading")

    def glyph_box(b):                      # what a backing must cover whole: glyph (+ its count badge)
        if b["members"] > 1:
            bw = t.textWidth(str(b["members"]), "glcd") + 4
            return (b["x"] - 8 - bw - 5, b["y"] - 12, b["x"] + 10 + bw, b["y"] + 8)
        return (b["x"] - 8, b["y"] - 8, b["x"] + 8, b["y"] + 8)

    def tag_w(a):                          # ONE width for placement and backing (incl. the tick)
        return max(t.textWidth(a["cs"], "glcd"), t.textWidth(alt_tag(a["alt"]), "glcd") + (7 if abs(a["vr"]) >= 300 else 0))

    for a in ([focus] if focus and focus_on_map else []) + [a for a in shown if a is not focus]:
        if not live or a["stale"] or a["members"] > 1:
            continue
        w = tag_w(a)
        gap = 14 if a is focus else 10                                  # clear of the focus ring
        placed = False
        for x0 in (a["x"] + gap, a["x"] - gap - w):                    # right or left only
            r = (x0 - 1, a["y"] - 9, x0 + w + 1, a["y"] + 9)
            if free(r):
                taken.append(r)
                tags.append((a, x0, None, False))
                placed = True
                break
        if not placed and a is focus:
            # The focus tag always lands, on a backing. The backing GROWS to cover every
            # glyph it touches completely (no clipped fragments that read as characters);
            # the side covering the fewest planes wins (hidden planes can't be tapped).
            # v3-R2-4: the backing is TAG-SIZED (+2 px). Every glyph or badge it touches is
            # simply not drawn (no fragments), and counts as hidden: the side hiding the fewest
            # planes wins. If both sides touch the focus ring, the tag steps outward as a callout.
            best = None
            ring = (a["x"] - 12, a["y"] - 12, a["x"] + 12, a["y"] + 12)
            for g_out in (gap, 22, 30, 38, 46):
                for x0 in (a["x"] + g_out, a["x"] - g_out - w):
                    r = (x0 - 2, a["y"] - 10, x0 + w + 2, a["y"] + 10)
                    under = [b for b in shown if b is not a and not (
                        r[2] <= glyph_box(b)[0] or r[0] >= glyph_box(b)[2] or r[3] <= glyph_box(b)[1] or r[1] >= glyph_box(b)[3])]
                    covered = sum(b["members"] for b in under)
                    if (r[0] >= 0 and r[2] <= 320 and free(r, chrome_only=True)
                            and (r[2] <= ring[0] or r[0] >= ring[2] or r[3] <= ring[1] or r[1] >= ring[3])
                            and (best is None or covered < best[0])):
                        best = (covered, x0, r, g_out > gap, under)
                if best:
                    break
            if best:
                taken.append(best[2])
                tags.append((a, best[1], best[2], best[3]))
                for b in best[4]:
                    b["hidden"] = True
    for name, lat, lon, city in TOWNS:
        if not city and zi == 2:
            continue
        x, y = proj(lat, lon, ppn)
        if math.hypot(x - MAP_CX, y - MAP_CY) < 30:
            continue
        w = t.textWidth(name, "f2")
        r = (x - w / 2, y - 12, x + w / 2, y + 3)
        if y < STRIP_Y - 4 and free(r):
            taken.append(r)
            t.drawString(name, round(x), round(y), "f2", C[M_DIM], "C")

    # 4. trails (synthesised backwards for the mock; the firmware keeps real history)
    if live:
        for a in shown:
            for k in range(1, 8):
                lat, lon = geo.destination(a["lat"], a["lon"], (a["track"] + 180) % 360, a["gs"] * 0.514444 * 5 * k)
                x, y = proj(lat, lon, ppn)
                if 0 <= x < 320 and 0 <= y < STRIP_Y:
                    t.fillRect(round(x), round(y), 1, 1, C[M_TRAIL])

    # 4b. focus only (M2): 60 s leader (amber dots every 3 px) + hollow pop-point square
    def on_map(x, y):
        return 0 <= x < 320 and 0 <= y < STRIP_Y - 2 and not any(
            q[0] <= x <= q[2] and q[1] <= y <= q[3] for q in chrome)

    if live and focus and not focus["stale"] and not passed and (focus_on_map or focus["pop_pt"]):
        lx, ly = proj(*focus["f60"], ppn)
        n = max(1, int(math.hypot(lx - focus["x"], ly - focus["y"]) / 3))
        for i in range(3, n + 1):                                        # start clear of the ring
            px_, py_ = focus["x"] + (lx - focus["x"]) * i / n, focus["y"] + (ly - focus["y"]) * i / n
            if math.hypot(px_ - focus["x"], py_ - focus["y"]) > 12 and on_map(px_, py_):
                t.fillRect(round(px_), round(py_), 1, 1, C[M_PLANE])
        if focus["pop_pt"]:
            qx, qy = proj(*focus["pop_pt"], ppn)
            if math.hypot(qx - focus["x"], qy - focus["y"]) >= 16 and on_map(qx, qy):
                t.drawRect(round(qx) - 3, round(qy) - 3, 7, 7, C[M_PLANE])

    # 5. aircraft: amber = focus only; dim amber = heading into the zone within 60 s
    for a in shown:
        if a.get("hidden"):                                              # under the focus tag backing
            continue
        if not live or a["stale"]:
            col = C[M_DIM]
        elif a is focus:
            col = C[M_PLANE]
        elif a["inbound"]:
            col = C[M_PLANE_DIM]
        else:
            col = C[M_TEXT]
        if a is focus and live:
            t.drawCircle(round(a["x"]), round(a["y"]), 11, C[M_PLANE])
        S.plane_glyph(t, a["x"], a["y"], a["track"], gs, col)
        if a["members"] > 1:                                            # cluster count badge
            s = str(a["members"])
            bw = t.textWidth(s, "glcd") + 4
            bx = round(a["x"]) + 5
            if focus and live and math.hypot(focus["x"] - (bx + bw / 2), focus["y"] - (a["y"] - 7)) < 16:
                bx = round(a["x"]) - 5 - bw                             # keep clear of the focus ring
            t.fillRect(bx, round(a["y"]) - 12, bw, 10, C[M_PANEL])
            t.drawString(s, bx + 2, round(a["y"]) - 4, "glcd", C[M_TEXT])
    for a, x0, backing, callout in tags:
        if callout:                                                    # 1 px amber leader, ring -> tag
            x_ring = a["x"] + (11 if backing[0] > a["x"] else -11)
            x_tag = backing[0] if backing[0] > a["x"] else backing[2]
            t.drawFastHLine(round(min(x_ring, x_tag)), round(a["y"]), round(abs(x_tag - x_ring)), C[M_PLANE])
        if backing:
            t.fillRect(round(backing[0]), round(backing[1]), round(backing[2] - backing[0]),
                       round(backing[3] - backing[1]), C[M_PANEL])
        t.drawString(a["cs"], round(x0), round(a["y"]) - 1, "glcd", C[M_PLANE] if a is focus else C[M_TEXT])
        aw = t.drawString(alt_tag(a["alt"]), round(x0), round(a["y"]) + 8, "glcd", C[M_MUTED])
        if abs(a["vr"]) >= 300:                                         # M5: climbing / descending tick
            tx, ty = round(x0) + aw + 2, round(a["y"]) + 4
            if a["vr"] > 0:
                t.fillTriangle(tx, ty + 2, tx + 4, ty + 2, tx + 2, ty - 2, C[M_MUTED])
            else:
                t.fillTriangle(tx, ty - 2, tx + 4, ty - 2, tx + 2, ty + 2, C[M_MUTED])

    # M4: focus off the map -> amber notch on the map edge pointing at it (no text)
    if live and focus and not focus_on_map:
        dx, dy = focus["x"] - MAP_CX, focus["y"] - MAP_CY
        k = min(abs((154 if dx > 0 else -154) / dx) if dx else 9e9,
                abs(((STRIP_Y - 8 - MAP_CY) if dy > 0 else (6 - MAP_CY)) / dy) if dy else 9e9)
        ex, ey = MAP_CX + dx * k, MAP_CY + dy * k                       # tip on the inset edge
        # slide along the edge, clear of the chrome (back chip, N marker, zoom chip)
        on_top = abs(ey - 6) < 1
        for x0, x1, y1 in ((0, 50, 38), (140, 180, 32), (242, 320, 38)):
            if x0 <= ex <= x1 and ey <= y1:
                if on_top and x0 > 0 and x1 < 320:
                    ex = x0 - 4 if ex < (x0 + x1) / 2 else x1 + 4
                elif on_top:
                    ex = x1 + 4 if x0 == 0 else x0 - 4
                else:
                    ey = y1 + 20                         # well clear: never reads as a 2nd back button
        ln = math.hypot(dx, dy)
        ux, uy = dx / ln, dy / ln
        for L, half, col in ((17, 8.5, C[M_BG]), (14, 7, C[M_PLANE])):   # 1 px BG outline, then amber
            tx, ty = ex + ux * (1 if L == 17 else 0), ey + uy * (1 if L == 17 else 0)
            bx, by = tx - ux * L, ty - uy * L
            t.fillTriangle(round(tx), round(ty), round(bx - uy * half), round(by + ux * half),
                           round(bx + uy * half), round(by - ux * half), col)

    # 6. you
    t.fillCircle(MAP_CX, MAP_CY, 5, C[M_TEXT])
    t.fillCircle(MAP_CX, MAP_CY, 3, C[M_YOU])

    # 7. chrome
    t.fillRoundRect(4, 4, 36, 24, 12, C[M_PANEL])
    chevron_left(t, 17, 16, C[M_TEXT])
    lab = f"{ZOOM_MI[zi]} MI"
    zw = t.textWidth(lab, "f2") + 18
    t.fillRoundRect(316 - zw, 4, zw, 24, 12, C[M_PANEL])
    t.drawString(lab, 316 - zw // 2, 21, "f2", C[M_TEXT], "C")
    t.fillTriangle(160, 3, 155, 12, 165, 12, C[M_MUTED])
    t.drawString("N", 160, 24, "glcd", C[M_MUTED], "C")
    t.drawString("(c) OSM", 4, STRIP_Y - 4, "glcd", C[M_DIM])

    # 8. info strip (tap = open the card for the focus plane)
    t.fillRect(0, STRIP_Y, 320, 240 - STRIP_Y, C[M_PANEL])
    if state == "offline-recent":
        t.drawString("Traffic offline", 10, 231, "f2", C[M_WARN])
        t.drawString("positions 40 s old", 310, 231, "f2", C[M_MUTED], "R")
        return
    if state == "offline-cleared":
        t.drawString("No live traffic", 10, 231, "f2", C[M_WARN])
        t.drawString("retrying in 20 s", 310, 231, "f2", C[M_MUTED], "R")
        return
    if not focus:
        t.drawString(f"Nothing within {ZOOM_MI[zi]} mi", 10, 231, "f2", C[M_MUTED])
        if d.get("nearest"):
            t.drawString(f"nearest {d['nearest']}", 310, 231, "f2", C[M_MUTED], "R")
        return
    S.plane_glyph(t, 14, 227, focus["track"], 0.5, C[M_PLANE])
    # right side is fixed. Inside the disc it shows elevation - the actual trigger,
    # worded like the card - otherwise altitude.
    # right side, first match wins (docs/09): cycle > just popped > will pop > in disc > default.
    # Each state lists fallbacks, dropped from the right until the left keeps its type +
    # a 40 px operator stub (the type is never truncated or overlapped).
    dist = f'{focus["nm"] * 1.15078:.1f} mi {geo.compass8(focus["az"])}'
    M, A = C[M_MUTED], C[M_PLANE]
    if d.get("cycle") and d["cycle"][1] > 1:
        options = [[(f'{d["cycle"][0]} of {d["cycle"][1]} here', C[M_TEXT])]]
    elif passed:
        options = [[("passed", M), (dist, M)], [("passed", M)]]
    elif focus["inbound"]:
        options = [[(f'overhead in ~{focus["pop_s"]} s', A), (dist, M)], [(f'overhead in ~{focus["pop_s"]} s', A)],
                   [(f'in ~{focus["pop_s"]} s', A)]]
    elif focus["nm"] <= 3.0:                                           # Font 2: ` is a degree sign
        options = [[(f'{round(focus["el"])}` up', M), ("needs 25`", M)], [(f'{round(focus["el"])}` up', M)]]
    else:
        options = [[(alt_tag(focus["alt"]) + " ft", M), (dist, M)]]
    typ_w = t.textWidth(TYPES.get(focus["type"], focus["type"]) or "Unknown type", "f2")

    def width(segs):
        return sum(t.textWidth(x, "f2") for x, _ in segs) + 10 * (len(segs) - 1)

    cs_ = focus["cs"]
    al_ = AIRLINES.get(cs_[:3]) if len(cs_) > 3 and cs_[:3].isalpha() and cs_[3].isdigit() else None
    op_w = t.textWidth(al_ or cs_, "f2")
    # first fallback where the operator fits WHOLE; else the first with a 40 px stub
    segs = next((o for o in options if 300 - width(o) - 8 - 26 >= typ_w + 10 + op_w),
                next((o for o in options if 300 - width(o) - 8 - 26 >= typ_w + 10 + 40), options[-1]))
    xr = 300
    for i, (txt, col) in enumerate(reversed(segs)):
        if i:
            S.dot(t, xr - 5, 226, C[M_DIM])
            xr -= 10
        xr -= t.drawString(txt, xr, 231, "f2", col, "R")
    avail = xr - 8 - 26
    # left: "<operator> . <type>" (v1 priority: type > flight number). Add the flight
    # number only if it fits; if still too wide truncate the OPERATOR, never the type.
    cs = focus["cs"]
    al = AIRLINES.get(cs[:3]) if len(cs) > 3 and cs[:3].isalpha() and cs[3].isdigit() else None
    typ = TYPES.get(focus["type"], focus["type"]) or "Unknown type"
    op = al or cs
    tw = t.textWidth(typ, "f2")
    if al and segs is options[0] and t.textWidth(f"{al} {cs[3:]}", "f2") + 10 + tw <= avail:
        op = f"{al} {cs[3:]}"
    w = S.draw_fit(t, op, 26, 231, avail - 10 - tw, C[M_TEXT], ("f2",))
    S.dot(t, 26 + w + 5, 226, C[M_DIM])
    t.drawString(typ, 26 + w + 10, 231, "f2", C[M_TEXT])
    chevron_right(t, 308, 226, C[M_MUTED])


SCENARIOS_V3 = {
    "map-inbound": (map_screen, lambda: dict(zoom=1, traffic=approach_traffic())),
    "map-inbound-30s": (map_screen, lambda: dict(zoom=1, traffic=advance(approach_traffic(), 30))),
    "map-cycle-1": (map_screen, lambda: cycle_scene(1)),
    "map-cycle-2": (map_screen, lambda: cycle_scene(2)),
    "map-offscreen": (map_screen, lambda: dict(zoom=0, traffic=offscreen_traffic(), selected="FFT1453")),
    "map-worst-strip": (map_screen, lambda: dict(zoom=1, traffic=worst_strip_traffic())),
    "map-after-pop": (map_screen, lambda: dict(zoom=1, traffic=popped_traffic(), after_pop="SWA3319")),
    "map-after-pop-inbound": (map_screen, lambda: dict(zoom=1, traffic=after_pop_inbound_traffic(), after_pop="SWA3319")),
    "map-offscreen-inbound": (map_screen, lambda: dict(zoom=0, traffic=offscreen_inbound_traffic())),
}

SCENARIOS = {
    "map-z1": (map_screen, lambda: dict(zoom=1, traffic=load_traffic("adsbfi_gilbert_25nm.json", ("SWA1296",)))),
    "map-z2-busy": (map_screen, lambda: dict(zoom=2, traffic=busy_traffic())),
    "map-z0-selected": (map_screen, lambda: dict(zoom=0, traffic=inbound_traffic(), selected="SWA2073")),
    "map-empty": (map_screen, lambda: dict(zoom=0, traffic=[], nearest="7.2 mi NW")),
    "map-loading": (map_screen, lambda: dict(zoom=2, state="loading", traffic=load_traffic("adsbfi_gilbert_25nm.json"))),
    "map-offline-recent": (map_screen, lambda: dict(zoom=1, state="offline-recent", traffic=load_traffic("adsbfi_gilbert.json"))),
    "map-offline-cleared": (map_screen, lambda: dict(zoom=1, state="offline-cleared", traffic=[])),
}
