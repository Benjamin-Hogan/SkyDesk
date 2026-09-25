"""Static ground-clutter mask for the rain radar (docs/10-rain-radar.md -> Clutter).

Surveys archived IEM n0q frames from DRY days (May-June 2025, evening and early
morning, when inversions make clutter worst), finds pixels that show an echo on
many of them (terrain / buildings / wind farms seen by the radar), and writes:

  firmware/radar_clutter.cpp        320x240 bit mask (1 = static clutter), 9.6 KB flash
  docs/mockups/png/radar-clutter.png  preview (mask pixels in red)

It also prints the measurement table that doc 10 quotes: clutter blob sizes
with and without the mask, and small-blob sizes on real wet frames.

Run:  python tools/radar/make_clutter_mask.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "docs" / "mockups"))
import radar_data as R  # noqa: E402

sys.path.insert(0, str(ROOT / "tools" / "basemap"))
from make_basemap import observer  # noqa: E402  (secrets.h override, else config.h)

from PIL import Image  # noqa: E402

DRY_DATES = ["2025-05-06", "2025-05-11", "2025-05-15", "2025-05-19", "2025-05-24", "2025-05-28",
             "2025-06-02", "2025-06-05", "2025-06-10", "2025-06-14", "2025-06-18", "2025-06-22"]
DRY_TIMES = ["02:00:00Z", "13:00:00Z"]          # 7 PM and 6 AM Arizona
MASK_FRACTION = 0.15                            # echo on >= 15 % of dry frames = static
DRY_MAX_WET_PX = 200                            # a frame with more echo than this is not "dry"


def blobs(lv, mask=None):
    """8-connected blob sizes of level >= 1, ignoring masked pixels."""
    seen = [[False] * 320 for _ in range(240)]
    out = []
    for y in range(240):
        for x in range(320):
            if not lv[y][x] or seen[y][x] or (mask and mask[y][x]):
                continue
            stack, n = [(x, y)], 0
            seen[y][x] = True
            while stack:
                a, b = stack.pop()
                n += 1
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        u, v = a + dx, b + dy
                        if 0 <= u < 320 and 0 <= v < 240 and lv[v][u] and not seen[v][u] and not (mask and mask[v][u]):
                            seen[v][u] = True
                            stack.append((u, v))
            out.append(n)
    return out


def main():
    lat, lon = observer()
    if (round(lat, 4), round(lon, 4)) != (round(R.OBS[0], 4), round(R.OBS[1], 4)):
        # the firmware observer differs from the mock default: survey THERE, own cache
        R.use_observer(lat, lon, ROOT / "tools" / "radar" / "cache" / f"{lat:.4f}_{lon:.4f}")
    print(f"observer {lat:.4f},{lon:.4f}  cache {R.CACHE}")
    frames, lvs = [], []
    for t in (f"{d}T{t}" for d in DRY_DATES for t in DRY_TIMES):
        lv = R.levels(t)
        wet = sum(1 for r in lv for v in r if v)
        if wet >= DRY_MAX_WET_PX:                    # real weather that day: not a clutter sample
            print(f"excluded {t}: {wet} echo px (real weather)")
            continue
        frames.append(t)
        lvs.append(lv)
    count = [[0] * 320 for _ in range(240)]
    raw_sizes = []
    for lv in lvs:
        raw_sizes += blobs(lv)
        for y in range(240):
            for x in range(320):
                if lv[y][x]:
                    count[y][x] += 1
    need = max(2, round(MASK_FRACTION * len(frames)))
    mask = [[count[y][x] >= need for x in range(320)] for y in range(240)]
    # grow by 1 px: the echo edge of a static target jitters between scans
    grown = [[any(mask[v][u] for v in range(max(0, y - 1), min(240, y + 2)) for u in range(max(0, x - 1), min(320, x + 2)))
              for x in range(320)] for y in range(240)]
    masked_sizes = []
    for lv in lvs:
        masked_sizes += blobs(lv, grown)

    wet_small = []
    for seq in ("monsoon", "scattered"):
        for t in R.SEQUENCES[seq]:
            wet_small += [s for s in blobs(R.levels(t), grown) if s <= 30]

    npx = sum(r.count(True) for r in grown)
    print(f"dry frames: {len(frames)}  mask pixels: {npx}  (wet on >= {need} frames, grown 1 px)")
    print(f"dry clutter blobs, no mask : n={len(raw_sizes)} max={max(raw_sizes, default=0)} px")
    print(f"dry clutter blobs, masked  : n={len(masked_sizes)} max={max(masked_sizes, default=0)} px "
          f"sizes={sorted(masked_sizes, reverse=True)[:12]}")
    print(f"wet-frame blobs <= 30 px (12 real frames, masked): {sorted(wet_small)}")

    # "near the mask" = within 2 px: a blob with >= half its pixels here is clutter (per BLOB,
    # v3-R2-2 - a real storm crossing a mask site keeps every pixel)
    near = [[any(grown[v][u] for v in range(max(0, y - 2), min(240, y + 3)) for u in range(max(0, x - 2), min(320, x + 3)))
             for x in range(320)] for y in range(240)]

    def pack(m):
        bits = bytearray(320 * 240 // 8)
        for y in range(240):
            for x in range(320):
                if m[y][x]:
                    i = y * 320 + x
                    bits[i >> 3] |= 1 << (i & 7)
        return bits

    bits, nbits = pack(grown), pack(near)
    lines = ["// GENERATED by tools/radar/make_clutter_mask.py - do not edit.",
             f"// Static ground clutter: pixels with an echo on >= {need} of {len(frames)} dry frames",
             "// (May-June 2025, 7 PM + 6 AM), grown by 1 px. 1 bit per pixel, row-major, LSB = lower x.",
             '#include "radar_model.h"', "", "#include <stdint.h>", "",
             f"const uint8_t RADAR_CLUTTER[{len(bits)}] = {{"]
    for i in range(0, len(bits), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in bits[i:i + 24]) + ",")
    lines += ["};", "", "// within 2 px of the mask", f"const uint8_t RADAR_CLUTTER_NEAR[{len(nbits)}] = {{"]
    for i in range(0, len(nbits), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in nbits[i:i + 24]) + ",")
    lines += ["};", ""]
    (ROOT / "firmware" / "radar_clutter.cpp").write_text("\n".join(lines), encoding="utf-8")

    mimg = Image.new("1", (320, 240), 0)
    for y in range(240):
        for x in range(320):
            if grown[y][x]:
                mimg.putpixel((x, y), 1)
    R.CACHE.mkdir(parents=True, exist_ok=True)
    mimg.save(R.CACHE / "clutter_mask.png")                            # read by radar_data.py
    nimg = Image.new("1", (320, 240), 0)
    for y in range(240):
        for x in range(320):
            if near[y][x]:
                nimg.putpixel((x, y), 1)
    nimg.save(R.CACHE / "clutter_near.png")
    default = R.CACHE == ROOT / "docs" / "mockups" / "radar"
    prev_dir = ROOT / "docs" / "mockups" / "png" if default else R.CACHE
    bm_src = ROOT / "docs/mockups/png/basemap-radar.png" if default else         ROOT / "tools" / "basemap" / "cache" / f"preview_{R.OBS[0]:.4f}_{R.OBS[1]:.4f}" / "basemap-radar.png"
    bm = Image.open(bm_src).convert("RGB")
    px = bm.load()
    for y in range(240):
        for x in range(320):
            if grown[y][x]:
                px[x, y] = (255, 60, 60)
    bm.resize((960, 720), Image.NEAREST).save(prev_dir / "radar-clutter@3x.png")
    print("-> firmware/radar_clutter.cpp, docs/mockups/png/radar-clutter@3x.png")


if __name__ == "__main__":
    main()
