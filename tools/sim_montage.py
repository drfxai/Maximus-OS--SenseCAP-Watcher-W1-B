#!/usr/bin/env python3
"""Turn the simulator's PPMs into a circular-masked contact sheet.
usage: sim_montage.py <ppm-dir> <out.png> [cols]"""
import glob, sys
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 5
files = sorted(glob.glob(d + "/*.ppm"))
S = 412
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).ellipse((0, 0, S - 1, S - 1), fill=255)
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (S + 12) + 12, rows * (S + 12) + 12), (30, 30, 36))
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB")
    t = Image.new("RGB", (S, S), (0, 0, 0))
    t.paste(im, (0, 0), mask)
    sheet.paste(t, (12 + (i % cols) * (S + 12), 12 + (i // cols) * (S + 12)))
sheet.save(out)
print("wrote", out, len(files), "screens")
