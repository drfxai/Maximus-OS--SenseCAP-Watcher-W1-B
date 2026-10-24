#!/usr/bin/env python3
"""Render the Maximus UI artwork (helmet, shield, mic) and emit it as LVGL
RAW_ALPHA PNG images in firmware/main/ui/assets.c. Needs Pillow + numpy.

    python3 tools/gen_assets.py
"""
import io
import math
import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SS = 4  # supersampling


def shade(size, mask, stops, angle=90.0):
    """Fill `mask` (L image) with a multi-stop linear gradient."""
    w, h = size
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    a = math.radians(angle)
    t = (xx * math.cos(a) + yy * math.sin(a))
    t = (t - t.min()) / max(1e-6, (t.max() - t.min()))
    pos = [s[0] for s in stops]
    out = np.zeros((h, w, 4), np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, pos, [s[1][c] for s in stops])
    out[..., 3] = np.asarray(mask, np.float32)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def helmet(px=190):
    n = px * SS
    sil = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(sil)
    s = n / 100.0

    def P(pts):
        return [(x * s, y * s) for x, y in pts]

    # dome
    d.ellipse(P([(18, 10), (82, 74)]), fill=255)
    # cheek guards + chin
    d.polygon(P([(18, 44), (82, 44), (80, 70), (70, 88), (58, 96), (42, 96), (30, 88), (20, 70)]), fill=255)
    # brow ridge flare
    d.polygon(P([(14, 40), (86, 40), (84, 50), (16, 50)]), fill=255)
    body = sil.filter(ImageFilter.GaussianBlur(0.6 * SS))

    gold = shade((n, n), body, [(0, (255, 226, 130)), (0.35, (236, 170, 40)), (0.7, (176, 108, 18)), (1, (96, 56, 8))], 70)
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    img.alpha_composite(gold)

    ov = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    # visor: T-shaped opening
    dark = (14, 10, 6, 255)
    od.polygon(P([(30, 46), (70, 46), (70, 56), (56, 56), (56, 84), (44, 84), (44, 56), (30, 56)]), fill=dark)
    # nose guard highlight
    od.polygon(P([(48, 56), (52, 56), (52, 80), (48, 80)]), fill=(120, 80, 20, 255))
    # brow line + dome highlight
    od.arc(P([(20, 12), (80, 72)]), 200, 340, fill=(255, 240, 180, 200), width=int(1.6 * s))
    od.arc(P([(26, 18), (74, 66)]), 215, 300, fill=(255, 255, 220, 120), width=int(1.0 * s))
    # cheek shading
    od.polygon(P([(18, 56), (30, 60), (34, 84), (28, 82), (20, 68)]), fill=(90, 50, 8, 120))
    od.polygon(P([(82, 56), (70, 60), (66, 84), (72, 82), (80, 68)]), fill=(90, 50, 8, 120))
    ov = ov.filter(ImageFilter.GaussianBlur(0.3 * SS))
    mask = Image.new("L", (n, n), 0)
    mask.paste(body)
    clipped = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    clipped.paste(ov, (0, 0), mask)
    img.alpha_composite(clipped)

    # crest (dark steel with blue edge) drawn behind-top
    crest = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    cd = ImageDraw.Draw(crest)
    cd.polygon(P([(36, 16), (50, 3), (64, 16), (60, 24), (40, 24)]), fill=(26, 40, 74, 255), outline=(70, 130, 255, 255))
    crest = crest.filter(ImageFilter.GaussianBlur(0.4 * SS))
    img = Image.alpha_composite(crest, img)
    return img.resize((px, px), Image.LANCZOS)


def shield(px=130):
    n = px * SS
    s = n / 100.0
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def P(pts):
        return [(x * s, y * s) for x, y in pts]

    outline = P([(50, 4), (90, 18), (88, 54), (76, 76), (50, 96), (24, 76), (12, 54), (10, 18)])
    glow = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(glow).polygon(outline, fill=(47, 123, 255, 150))
    glow = glow.filter(ImageFilter.GaussianBlur(3 * SS))
    img = Image.alpha_composite(glow, img)
    d = ImageDraw.Draw(img)
    d.polygon(outline, fill=(10, 24, 56, 255), outline=(60, 140, 255, 255), width=int(3 * s))
    inner = P([(50, 12), (82, 23), (80, 52), (71, 70), (50, 86), (29, 70), (20, 52), (18, 23)])
    d.polygon(inner, outline=(40, 100, 220, 255), width=int(1.4 * s))
    # padlock
    d.arc(P([(38, 28), (62, 56)]), 180, 360, fill=(120, 190, 255, 255), width=int(4.5 * s))
    d.rounded_rectangle(P([(34, 46), (66, 72)]), radius=int(5 * s), fill=(80, 160, 255, 255))
    d.ellipse(P([(46, 54), (54, 62)]), fill=(10, 24, 56, 255))
    d.rectangle(P([(48.6, 58), (51.4, 67)]), fill=(10, 24, 56, 255))
    return img.resize((px, px), Image.LANCZOS)


def mic(px=64):
    n = px * SS
    s = n / 100.0
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    blue = (120, 190, 255, 255)
    d.rounded_rectangle([(36 * s, 8 * s), (64 * s, 56 * s)], radius=int(14 * s), outline=blue, width=int(5 * s))
    d.arc([(22 * s, 30 * s), (78 * s, 74 * s)], 0, 180, fill=blue, width=int(5 * s))
    d.line([(50 * s, 74 * s), (50 * s, 90 * s)], fill=blue, width=int(5 * s))
    d.line([(36 * s, 90 * s), (64 * s, 90 * s)], fill=blue, width=int(5 * s))
    return img.resize((px, px), Image.LANCZOS)


def c_array(name, im):
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    data = buf.getvalue()
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return f"""static const uint8_t {name}_map[] = {{
{chr(10).join(rows)}
}};

const lv_img_dsc_t {name} = {{
    .header.cf = LV_IMG_CF_RAW_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {im.width},
    .header.h = {im.height},
    .data_size = sizeof({name}_map),
    .data = {name}_map,
}};
"""


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / "firmware/main/ui/assets.c"
    imgs = {"mx_img_helmet": helmet(), "mx_img_shield": shield(), "mx_img_mic": mic()}
    prev = root / "tools/_preview"
    prev.mkdir(exist_ok=True)
    body = ["/* Generated by tools/gen_assets.py - do not edit. */", '#include "lvgl.h"', ""]
    for k, im in imgs.items():
        im.save(prev / f"{k}.png")
        body.append(c_array(k, im))
    out.write_text("\n".join(body))
    print("wrote", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    sys.exit(main())
