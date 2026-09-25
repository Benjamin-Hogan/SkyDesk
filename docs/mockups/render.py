"""Render SkyDesk mockups to PNG.

For each docs/mockups/*.html this writes into docs/mockups/png/:
  <name>@3x.png  - crisp 960x720 render (layout review)
  <name>_px.png  - 320x240 render upscaled 3x with nearest-neighbour
                   (honest preview of the real device pixel density)

Usage:  python docs/mockups/render.py [name-filter]
Requires Google Chrome (or Edge) and Pillow.
"""
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE / "png"
BROWSERS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
]


def browser() -> str:
    for b in BROWSERS:
        if os.path.exists(b):
            return b
    sys.exit("No Chrome/Edge found")


def shot(html: Path, png: Path, scale: int) -> None:
    subprocess.run(
        [browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars",
         f"--force-device-scale-factor={scale}", "--window-size=320,240",
         "--default-background-color=00000000",
         f"--screenshot={png}", html.as_uri()],
        check=True, capture_output=True, timeout=60)


def main() -> None:
    OUT.mkdir(exist_ok=True)
    flt = sys.argv[1] if len(sys.argv) > 1 else ""
    for html in sorted(HERE.glob("*.html")):
        if flt not in html.stem:
            continue
        hi = OUT / f"{html.stem}@3x.png"
        lo = OUT / f"{html.stem}_1x.png"
        px = OUT / f"{html.stem}_px.png"
        shot(html, hi, 3)
        shot(html, lo, 1)
        Image.open(lo).convert("RGB").resize((960, 720), Image.NEAREST).save(px)
        lo.unlink()
        print("rendered", html.stem)


if __name__ == "__main__":
    main()
