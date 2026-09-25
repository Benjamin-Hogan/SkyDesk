"""Build-time basemap for the SkyDesk plane map (docs/08-plane-map.md).

Downloads (once, cached) OpenStreetMap roads / runways / canals / towns /
airports around the observer, renders one 320x240 4-bit indexed image per zoom
level, and writes:

  firmware/basemap_data.cpp   packed 4bpp images (TFT_eSprite 4-bit layout:
                              row-major, even x in the HIGH nibble) + label tables
  docs/mockups/png/basemap-z<N>.png   previews (used by the mockups too)

Run:  python tools/basemap/make_basemap.py [--refresh]
Map data (c) OpenStreetMap contributors, ODbL.
"""
from __future__ import annotations

import json
import math
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
CACHE = Path(__file__).resolve().parent / "cache"
OUT_CPP = ROOT / "firmware" / "basemap_data.cpp"
OUT_PNG = ROOT / "docs" / "mockups" / "png"

W, H = 320, 240
CX, CY = 160, 116           # observer pixel (MAP_CX/MAP_CY in include/basemap.h)
RING_PX = 100               # range ring radius in px == the zoom chip's distance
ZOOM_MI = [5, 10, 20]       # chip labels "5 MI" / "10 MI" / "20 MI" (review v2-R1-5: miles)
ZOOMS = [RING_PX / (mi / 1.150779) for mi in ZOOM_MI]   # px per nm
RADIUS_NM = 40              # motorways / trunks / runways / canals / towns (fills 20 mi corners)
PRIMARY_NM = 25             # primary roads (only drawn at 5 and 10 mi)
RADAR_NM = 95               # radar basemap coverage (the view's corners are ~87 nm)
RADAR_PPN = 100 / (50 / 1.150779)   # 50 mi ring = 100 px (docs/10)


def observer(files=None):
    """Read OBS_LAT / OBS_LON from secrets.h if overridden there, else config.h."""
    for f in files or (ROOT / "include" / "secrets.h", ROOT / "include" / "config.h"):
        if not f.exists():
            continue
        src = re.sub(r"//[^\n]*", "", f.read_text())
        lat = re.search(r"#define\s+OBS_LAT\s+\(?(-?[\d.]+)f?\)?", src)
        lon = re.search(r"#define\s+OBS_LON\s+\(?(-?[\d.]+)f?\)?", src)
        if lat and lon:
            return float(lat.group(1)), float(lon.group(1))
    sys.exit("OBS_LAT/OBS_LON not found")


# 4-bit palette - index order is the MapPal enum in include/basemap.h.
PALETTE = [
    "#0A1120",  # 0 BG
    "#1A2436",  # 1 primary road
    "#2A3750",  # 2 trunk road
    "#3C4A66",  # 3 motorway
    "#6F7E9C",  # 4 runway
    "#173452",  # 5 canal / river
    "#4A5670",  # 6 trails (was rings: clashed with trunk in RGB565, review v2-R1-4)
    "#EEF2F8",  # 7 text
    "#9AA6BE",  # 8 muted
    "#7886A2",  # 9 dim
    "#FFB02E",  # 10 plane
    "#8A6424",  # 11 plane dim
    "#2B2720",  # 12 overhead zone fill
    "#131C2E",  # 13 panel
    "#5CC8FF",  # 14 you (observer)
    "#FF8A3D",  # 15 warn
]


def overpass(query: str, name: str, refresh: bool) -> dict:
    path = CACHE / name
    if path.exists() and not refresh:
        return json.loads(path.read_text(encoding="utf-8"))
    CACHE.mkdir(exist_ok=True)
    data = urllib.parse.urlencode({"data": query}).encode()
    req = urllib.request.Request("https://overpass-api.de/api/interpreter", data=data,
                                 headers={"User-Agent": "SkyDesk-basemap/1.0 (hobby build script)"})
    with urllib.request.urlopen(req, timeout=180) as r:
        path.write_bytes(r.read())
    return json.loads(path.read_text(encoding="utf-8"))


def main():
    refresh = "--refresh" in sys.argv
    lat0, lon0 = observer()
    # The mockups are drawn at the documented default location (config.h). When secrets.h
    # moves the observer, previews go next to the cache instead of over the mocks' images.
    out_png = OUT_PNG
    if (round(lat0, 4), round(lon0, 4)) != tuple(round(v, 4) for v in observer([ROOT / "include" / "config.h"])):
        out_png = CACHE / f"preview_{lat0:.4f}_{lon0:.4f}"
        out_png.mkdir(parents=True, exist_ok=True)

    def bbox(nm):
        dlat = nm / 60
        dlon = nm / (60 * math.cos(math.radians(lat0)))
        return f"{lat0 - dlat:.3f},{lon0 - dlon:.3f},{lat0 + dlat:.3f},{lon0 + dlon:.3f}"

    bb, bbp = bbox(RADIUS_NM), bbox(PRIMARY_NM)
    osm = overpass(f'[out:json][timeout:120];(way["highway"~"^(motorway|trunk)$"]({bb});'
                   f'way["aeroway"="runway"]({bb});'
                   f'way["waterway"~"^(river|canal)$"]["name"~"Salt|Gila|Arizona|Western|Consolidated|Central Arizona"]({bb});'
                   f'node["place"~"^(city|town)$"]({bb}););out geom;', f"osm40_{lat0:.2f}_{lon0:.2f}.json", refresh)
    prim = overpass(f'[out:json][timeout:120];(way["highway"="primary"]({bbp}););out geom;',
                    f"primary_{lat0:.2f}_{lon0:.2f}.json", refresh)
    osm["elements"] += prim["elements"]
    apt = overpass(f'[out:json][timeout:60];(nwr["aeroway"="aerodrome"]["iata"]({bb});'
                   f'nwr["aeroway"="aerodrome"]["faa"]({bb}););out center tags;',
                   f"airports_{lat0:.2f}_{lon0:.2f}.json", refresh)

    coslat = math.cos(math.radians(lat0))

    def proj(lat, lon, ppn):
        return CX + (lon - lon0) * 60 * coslat * ppn, CY - (lat - lat0) * 60 * ppn

    # --- layers, drawn back to front: (filter, palette index, widths per zoom)
    layers = [
        (lambda t: t.get("waterway") in ("river", "canal"), 5, (2, 1, 1)),
        (lambda t: t.get("highway") == "primary", 1, (1, 1, 0)),
        (lambda t: t.get("highway") == "trunk", 2, (2, 1, 1)),
        (lambda t: t.get("highway") == "motorway", 3, (3, 2, 1)),
        (lambda t: t.get("aeroway") == "runway", 4, (3, 2, 2)),
    ]
    ways = [e for e in osm["elements"] if e["type"] == "way" and "geometry" in e]

    images = []
    for zi, ppn in enumerate(ZOOMS):
        img = Image.new("P", (W, H), 0)
        img.putpalette([c for h in PALETTE for c in bytes.fromhex(h[1:])])
        d = ImageDraw.Draw(img)
        for keep, idx, widths in layers:
            wd = widths[zi]
            if not wd:
                continue
            for e in ways:
                if not keep(e.get("tags", {})):
                    continue
                pts = [proj(g["lat"], g["lon"], ppn) for g in e["geometry"]]
                if all((x < -20 or x > W + 20 or y < -20 or y > H + 20) for x, y in pts):
                    continue
                d.line(pts, fill=idx, width=wd)
        images.append(img)
        img.save(out_png / f"basemap-z{zi}.png")

    # --- radar view (docs/10-rain-radar.md): one wide zoom, 50 mi ring = 100 px,
    #     motorways / trunks / named rivers + towns with population to 95 nm.
    rb = bbox(RADAR_NM)
    wide = overpass(f'[out:json][timeout:180];(way["highway"="motorway"]({rb});way["highway"="trunk"]({rb});'
                    f'way["waterway"="river"]["name"~"^(Salt|Gila|Verde|Agua Fria|Santa Cruz|San Pedro) River$"]({rb});'
                    f'node["place"~"^(city|town)$"]({rb}););out geom;', f"osm95_{lat0:.2f}_{lon0:.2f}.json", refresh)
    rimg = Image.new("P", (W, H), 0)
    rimg.putpalette([c for h in PALETTE for c in bytes.fromhex(h[1:])])
    rd = ImageDraw.Draw(rimg)
    for keep, idx in ((lambda t: t.get("waterway") == "river", 5), (lambda t: t.get("highway") == "trunk", 2),
                      (lambda t: t.get("highway") == "motorway", 3)):
        for e in wide["elements"]:
            if e["type"] == "way" and "geometry" in e and keep(e.get("tags", {})):
                rd.line([proj(g["lat"], g["lon"], RADAR_PPN) for g in e["geometry"]], fill=idx, width=1)
    rimg.save(out_png / "basemap-radar.png")
    rtowns = []
    for e in wide["elements"]:
        t = e.get("tags", {})
        if e["type"] != "node" or "name" not in t:
            continue
        name = t["name"].split(" / ")[0]
        if not name.isascii():
            continue
        try:
            pop = int(t.get("population", "0").replace(",", ""))
        except ValueError:
            pop = 0
        x, y = proj(e["lat"], e["lon"], RADAR_PPN)
        if 8 < x < W - 8 and 30 < y < 206:
            rtowns.append((pop, name, e["lat"], e["lon"]))
    rtowns = sorted(rtowns, reverse=True)[:48]          # biggest first; the device spreads by octant, max 10

    # --- labels (drawn on-device with real fonts, so crisp at every zoom)
    towns = sorted({(e["tags"]["name"], e["lat"], e["lon"], e["tags"]["place"] == "city")
                    for e in osm["elements"] if e["type"] == "node" and e.get("tags", {}).get("name")})
    airports = {}
    for e in apt["elements"]:
        t = e["tags"]
        code = t.get("iata") or t.get("faa")
        c = e.get("center", e)
        if code and len(code) == 3:
            airports[code] = (c["lat"], c["lon"])

    # --- emit C++
    lines = ["// GENERATED by tools/basemap/make_basemap.py - do not edit.",
             "// Map data (c) OpenStreetMap contributors, ODbL.",
             '#include "basemap.h"', "",
             f"// observer {lat0:.4f},{lon0:.4f}; 4bpp, row-major, even x in high nibble", ""]
    for zi, img in list(enumerate(images)) + [("R", rimg)]:
        px = list(img.tobytes())
        packed = bytes((px[i] << 4) | px[i + 1] for i in range(0, len(px), 2))
        lines.append(f"static const uint8_t Z{zi}[{len(packed)}] PROGMEM = {{")
        for i in range(0, len(packed), 24):
            lines.append("  " + ",".join(f"0x{b:02X}" for b in packed[i:i + 24]) + ",")
        lines.append("};")
    lines += ["", "const MapZoom MAP_ZOOMS[MAP_ZOOM_N] = {"]
    for zi, ppn in enumerate(ZOOMS):
        lines.append(f"  {{Z{zi}, {ppn:.1f}f}},")
    lines += ["};", "", "const MapPalette MAP_PALETTE = {{"]
    lines.append("  " + ", ".join(
        f"RGB565(0x{h[1:3]}, 0x{h[3:5]}, 0x{h[5:7]})" for h in PALETTE))
    lines += ["}};", "", "const MapLabel MAP_TOWNS[] = {"]
    for name, lat, lon, city in towns:
        lines.append(f'  {{"{name}", {lat:.4f}, {lon:.4f}, {"true" if city else "false"}}},')
    lines += ["};", "const uint8_t MAP_TOWN_N = sizeof(MAP_TOWNS) / sizeof(MAP_TOWNS[0]);", "",
              "const MapLabel MAP_AIRPORTS[] = {"]
    for code, (lat, lon) in sorted(airports.items()):
        lines.append(f'  {{"{code}", {lat:.4f}, {lon:.4f}, true}},')
    lines += ["};", "const uint8_t MAP_AIRPORT_N = sizeof(MAP_AIRPORTS) / sizeof(MAP_AIRPORTS[0]);", "",
              f"const MapZoom RADAR_BASEMAP = {{ZR, {RADAR_PPN:.4f}f}};", "",
              "// radar towns, population order (docs/10 -> Towns)", "const MapLabel RADAR_TOWNS[] = {"]
    for pop, name, lat, lon in rtowns:
        lines.append(f'  {{"{name}", {lat:.4f}, {lon:.4f}, {"true" if pop >= 100000 else "false"}}},')
    lines += ["};", "const uint8_t RADAR_TOWN_N = sizeof(RADAR_TOWNS) / sizeof(RADAR_TOWNS[0]);", ""]
    OUT_CPP.write_text("\n".join(lines))
    print(f"towns={len(towns)} airports={sorted(airports)} -> {OUT_CPP.relative_to(ROOT)} "
          f"({OUT_CPP.stat().st_size // 1024} KB source)")


if __name__ == "__main__":
    main()
