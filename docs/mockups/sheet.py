"""Contact sheet: python sheet.py r2 name1 name2 ... -> png/<r>-sheet-<n>.png (2x nearest, 2 columns)."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw
OUT = Path(__file__).resolve().parent / "png"
r, names = sys.argv[1], sys.argv[2:]
imgs = [Image.open(OUT / f"{r}-{n}.png").resize((640, 480), Image.NEAREST) for n in names]
rows = (len(imgs) + 1) // 2
sheet = Image.new("RGB", (2 * 640 + 30, rows * 510 + 10), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for i, (im, n) in enumerate(zip(imgs, names)):
    x, y = 10 + (i % 2) * 650, 10 + (i // 2) * 510
    sheet.paste(im, (x, y + 20)); d.text((x, y + 4), n, fill=(230, 230, 230))
name = OUT / f"{r}-sheet-{'-'.join(n.split('-')[-1] for n in names)[:40]}.png"
sheet.save(name); print(name)
