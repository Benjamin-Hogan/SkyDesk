"""tftsim - a tiny TFT_eSPI look-alike for honest SkyDesk mockups.

Draws a 320x240 canvas using the *real* bitmap fonts shipped with TFT_eSPI
(GLCD, Font 2, Adafruit GFX FreeFonts) and non-anti-aliased primitives, then
quantises every pixel to RGB565 - i.e. what the CYD can actually show.

Method names mirror TFT_eSPI so a layout written here ports almost 1:1 to
firmware. Text y coordinates are BASELINES (TFT_eSPI L/C/R_BASELINE datums).

Fonts are read from .pio/libdeps/usb/TFT_eSPI (run `pio pkg install` once).
"""
from __future__ import annotations

import math
import re
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / ".pio" / "libdeps" / "usb" / "TFT_eSPI" / "Fonts"

W, H = 320, 240


def rgb(hexstr: str) -> tuple[int, int, int]:
    h = hexstr.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def to565(c: tuple[int, int, int]) -> tuple[int, int, int]:
    r, g, b = c
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)


# ---------------------------------------------------------------------------
#  Font loading
# ---------------------------------------------------------------------------
def _hex_bytes(s: str) -> list[int]:
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", s)]


class GfxFont:
    """Adafruit GFX font (FreeSans etc.) parsed from its .h file."""

    def __init__(self, name: str):
        src = (FONTS / "GFXFF" / f"{name}.h").read_text()
        bm = re.search(r"Bitmaps\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
        self.bitmap = _hex_bytes(bm)
        gl = re.search(r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\}\s*;", src, re.S).group(1)
        self.glyphs = [tuple(int(v) for v in m) for m in
                       re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", gl)]
        tail = re.search(r"0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*(\d+)\s*\}\s*;", src[src.rfind("GFXfont"):])
        self.first, self.last, self.y_advance = int(tail.group(1), 16), int(tail.group(2), 16), int(tail.group(3))
        self.ascent = max(-g[5] for g in self.glyphs)          # glyph_ab
        self.cap = -self.glyphs[ord("H") - self.first][5]

    def glyph(self, ch: str):
        c = ord(ch)
        if c < self.first or c > self.last:
            raise ValueError(f"glyph {ch!r} (0x{c:02X}) not in GFX font - draw it as a primitive")
        return self.glyphs[c - self.first]

    def width(self, text: str) -> int:
        return sum(self.glyph(ch)[3] for ch in text)

    def draw(self, px, x: int, y: int, text: str, col):
        for ch in text:
            off, w, h, adv, xo, yo = self.glyph(ch)
            bit = 0
            for yy in range(h):
                for xx in range(w):
                    byte = self.bitmap[off + (bit >> 3)]
                    if byte & (0x80 >> (bit & 7)):
                        _put(px, x + xo + xx, y + yo + yy, col)
                    bit += 1
            x += adv


class Font2:
    """TFT_eSPI Font 2 (16 px cell, baseline 13). '`' renders as a degree sign."""
    cap = 9
    ascent = 13

    def __init__(self):
        src = (FONTS / "Font16.c").read_text()
        # keep only the default (#ifdef) branches, e.g. chr_f16_60 = degree, not grave
        src = re.sub(r"#else.*?#endif", "", src, flags=re.S)
        src = re.sub(r"#ifdef[^\n]*\n", "", src)
        wt = re.search(r"widtbl_f16\[96\]\s*=.*?\{(.*?)\};", src, re.S).group(1)
        wt = re.sub(r"#else.*?#endif", "", wt, flags=re.S)       # keep GRAVE_IS_DEGREE branch
        wt = re.sub(r"//[^\n]*", "", wt)
        self.widths = [int(v) for v in re.findall(r"\d+", wt)]
        self.chars = {}
        for m in re.finditer(r"chr_f16_([0-9A-F]{2})\[(\d+)\]\s*=.*?\{(.*?)\};", src, re.S):
            self.chars[int(m.group(1), 16)] = _hex_bytes(re.sub(r"//[^\n]*", "", m.group(3)))

    def width(self, text: str) -> int:
        return sum(self.widths[ord(c) - 32] for c in self._chk(text))

    @staticmethod
    def _chk(text):
        for c in text:
            if not 32 <= ord(c) <= 127:
                raise ValueError(f"glyph {c!r} not in Font 2 - draw it as a primitive")
        return text

    def draw(self, px, x, y, text, col):
        top = y - self.ascent
        for c in self._chk(text):
            data = self.chars[ord(c)]
            bpr = len(data) // 16
            for row in range(16):
                for b in range(bpr):
                    byte = data[row * bpr + b]
                    for bit in range(8):
                        if byte & (0x80 >> bit):
                            _put(px, x + b * 8 + bit, top + row, col)
            x += self.widths[ord(c) - 32]


class Glcd:
    """TFT_eSPI Font 1 - classic 5x7 in a 6x8 cell, baseline 7."""
    cap = 7
    ascent = 7

    def __init__(self, size: int = 1):
        src = (FONTS / "glcdfont.c").read_text()
        body = re.search(r"font\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
        self.data = _hex_bytes(re.sub(r"//[^\n]*", "", body))
        self.size = size
        self.cap = 7 * size
        self.ascent = 7 * size

    def width(self, text: str) -> int:
        return 6 * self.size * len(text)

    def draw(self, px, x, y, text, col):
        top = y - self.ascent
        s = self.size
        for c in text:
            base = ord(c) * 5
            for cx in range(5):
                line = self.data[base + cx]
                for cy in range(8):
                    if line & (1 << cy):
                        for sx in range(s):
                            for sy in range(s):
                                _put(px, x + cx * s + sx, top + cy * s + sy, col)
            x += 6 * s


def _put(px, x, y, col):
    if 0 <= x < W and 0 <= y < H:
        px[x, y] = col


@lru_cache(None)
def font(token: str):
    """Typography tokens from docs/06-ui-spec.md."""
    return {
        "glcd": lambda: Glcd(1),
        "f2": Font2,
        "fs9": lambda: GfxFont("FreeSans9pt7b"),
        "fsb9": lambda: GfxFont("FreeSansBold9pt7b"),
        "fs12": lambda: GfxFont("FreeSans12pt7b"),
        "fsb12": lambda: GfxFont("FreeSansBold12pt7b"),
        "fsb18": lambda: GfxFont("FreeSansBold18pt7b"),
        "fsb24": lambda: GfxFont("FreeSansBold24pt7b"),
    }[token]()


# ---------------------------------------------------------------------------
#  Canvas
# ---------------------------------------------------------------------------
class TFT:
    def __init__(self, bg="#000000"):
        self.img = Image.new("RGB", (W, H), rgb(bg))
        self.d = ImageDraw.Draw(self.img)
        self.px = self.img.load()
        # True while drawing content that lives in a 4-bit sprite on the device.
        # TFT_eSPI's anti-aliased calls blend RGB565 values, which a 4-bit sprite
        # would misread as palette indices -> refuse them (review v2-R1-8).
        self.sprite4 = False

    def _no_aa(self, fn):
        if self.sprite4:
            raise RuntimeError(f"{fn}() is anti-aliased on TFT_eSPI - not allowed in a 4-bit sprite")

    # -- fills / shapes (TFT_eSPI names) --
    def fillScreen(self, c):
        self.fillRect(0, 0, W, H, c)

    def fillRect(self, x, y, w, h, c):
        if w > 0 and h > 0:
            self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb(c))

    def drawRect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=rgb(c))

    def fillRoundRect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, fill=rgb(c))

    def drawRoundRect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, outline=rgb(c))

    def drawFastHLine(self, x, y, w, c):
        self.fillRect(x, y, w, 1, c)

    def drawFastVLine(self, x, y, h, c):
        self.fillRect(x, y, 1, h, c)

    def drawLine(self, x0, y0, x1, y1, c):
        self.d.line([x0, y0, x1, y1], fill=rgb(c))

    def drawWideLine(self, x0, y0, x1, y1, wd, c):
        self._no_aa("drawWideLine")
        self.d.line([x0, y0, x1, y1], fill=rgb(c), width=max(1, round(wd)))
        r = wd / 2
        for (x, y) in ((x0, y0), (x1, y1)):   # round caps like TFT_eSPI
            self.d.ellipse([x - r + 0.5, y - r + 0.5, x + r - 0.5, y + r - 0.5], fill=rgb(c))

    def drawCircle(self, x, y, r, c):
        self.d.ellipse([x - r, y - r, x + r, y + r], outline=rgb(c))

    def fillCircle(self, x, y, r, c):
        self.d.ellipse([x - r, y - r, x + r, y + r], fill=rgb(c))

    def fillTriangle(self, x0, y0, x1, y1, x2, y2, c):
        self.d.polygon([(x0, y0), (x1, y1), (x2, y2)], fill=rgb(c))

    def fillPolygon(self, pts, c):      # firmware: fan of fillTriangle
        self.d.polygon(pts, fill=rgb(c))

    def drawArc(self, x, y, r, ir, a0, a1, c):
        """TFT_eSPI drawArc: angles in degrees clockwise from 6 o'clock (bottom)."""
        self._no_aa("drawArc")
        # convert to PIL (clockwise from 3 o'clock)
        s, e = a0 + 90, a1 + 90
        if ir <= 0:
            self.d.pieslice([x - r, y - r, x + r, y + r], s, e, fill=rgb(c))
        else:
            self.d.arc([x - r, y - r, x + r, y + r], s, e, fill=rgb(c), width=r - ir)

    def drawDashedCircle(self, x, y, r, c, on=2, off=3):
        """Firmware: loop drawPixel around the circle - no TFT_eSPI builtin."""
        n = int(2 * math.pi * r)
        for i in range(n):
            if i % (on + off) < on:
                a = 2 * math.pi * i / n
                _put(self.px, round(x + r * math.cos(a)), round(y + r * math.sin(a)), rgb(c))

    # -- text --
    def textWidth(self, text, tok, track=0):
        return font(tok).width(text) + track * max(0, len(text) - 1)

    def drawString(self, text, x, y, tok, c, datum="L", track=0):
        """datum: 'L' | 'C' | 'R' (all on the baseline).
        track: extra px between glyphs (firmware: drawNumber() draws per char)."""
        f = font(tok)
        w = self.textWidth(text, tok, track)
        if datum == "C":
            x -= w // 2
        elif datum == "R":
            x -= w
        if not track:
            f.draw(self.px, x, y, text, rgb(c))
        else:
            for ch in text:
                f.draw(self.px, x, y, ch, rgb(c))
                x += f.width(ch) + track
        return w

    def drawDegree(self, x, y_top, r, c, thick=2):
        """Degree ring (FreeFonts have no U+00B0). x = left edge, y_top = top."""
        for t in range(thick):
            self.drawCircle(x + r, y_top + r, r - t, c)
        return 2 * r + 2

    # -- output --
    def save(self, path_1x: Path, path_3x: Path | None = None, scale=3):
        q = self.img.copy()
        qp = q.load()
        for yy in range(H):
            for xx in range(W):
                qp[xx, yy] = to565(qp[xx, yy])
        q.save(path_1x)
        if path_3x:
            q.resize((W * scale, H * scale), Image.NEAREST).save(path_3x)
