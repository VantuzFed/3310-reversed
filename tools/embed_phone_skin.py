#!/usr/bin/env python3
"""
Converts phone.png (500x1139 RGBA Nokia 3310 photo) into src/s20_phone_skin.h/.c
using PackBits-style RGB565 RLE so no PNG/image library is needed at runtime.
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG_PATH = os.path.join(ROOT, "phone.png")
OUT_H = os.path.join(ROOT, "src", "s20_phone_skin.h")
OUT_C = os.path.join(ROOT, "src", "s20_phone_skin.c")

TRANSPARENT_KEY = 0xF81F  # Pure magenta (transparent outside phone / inside LCD glass)

# Glass opening in phone.png (inside the dark bezel)
CUT_X0, CUT_Y0 = 86, 308
CUT_X1, CUT_Y1 = 415, 480

# Active 84x48 pixel viewport centered inside the glass opening (aspect ~1.9)
LCD_X = 92
LCD_Y = 312
LCD_W = 318
LCD_H = 165


def to_rgb565(r, g, b):
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    if v == TRANSPARENT_KEY:
        v ^= 1
    return v


def packbits_u16(pixels):
    out = []
    n = len(pixels)
    i = 0
    while i < n:
        # Check for run of >= 3 identical pixels
        run = 1
        while i + run < n and run < 0x7FFF and pixels[i + run] == pixels[i]:
            run += 1
        if run >= 3:
            out.append(0x8000 | run)
            out.append(pixels[i])
            i += run
        else:
            lit_start = i
            lit_len = 0
            while i < n and lit_len < 0x7FFF:
                r = 1
                while i + r < n and r < 3 and pixels[i + r] == pixels[i]:
                    r += 1
                if r >= 3:
                    break
                i += 1
                lit_len += 1
            out.append(lit_len)
            out.extend(pixels[lit_start : lit_start + lit_len])
    return out


def main():
    im = Image.open(PNG_PATH).convert("RGBA")
    w, h = im.size
    px = im.load()

    pixels = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            in_cut = (CUT_X0 <= x <= CUT_X1 and CUT_Y0 <= y <= CUT_Y1)
            # Inside glass cutout, keep only dark bezel corners if r+g+b is dark near corner,
            # or simply cut the inner rectangle cleanly
            if in_cut or a < 128:
                pixels.append(TRANSPARENT_KEY)
            else:
                pixels.append(to_rgb565(r, g, b))

    packed = packbits_u16(pixels)

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write(
            "#ifndef S20_PHONE_SKIN_H\n"
            "#define S20_PHONE_SKIN_H\n\n"
            "#include <stdint.h>\n\n"
            f"#define S20_SKIN_W {w}\n"
            f"#define S20_SKIN_H {h}\n"
            f"#define S20_SKIN_CUT_X {CUT_X0}\n"
            f"#define S20_SKIN_CUT_Y {CUT_Y0}\n"
            f"#define S20_SKIN_CUT_W {CUT_X1 - CUT_X0 + 1}\n"
            f"#define S20_SKIN_CUT_H {CUT_Y1 - CUT_Y0 + 1}\n"
            f"#define S20_SKIN_LCD_X {LCD_X}\n"
            f"#define S20_SKIN_LCD_Y {LCD_Y}\n"
            f"#define S20_SKIN_LCD_W {LCD_W}\n"
            f"#define S20_SKIN_LCD_H {LCD_H}\n"
            f"#define S20_SKIN_TRANSPARENT 0x{TRANSPARENT_KEY:04X}u\n"
            f"#define S20_SKIN_PACKED_LEN {len(packed)}\n\n"
            "extern const uint16_t s20_phone_skin_packed[S20_SKIN_PACKED_LEN];\n\n"
            "#endif /* S20_PHONE_SKIN_H */\n"
        )

    lines = [
        '#include "s20_phone_skin.h"',
        "",
        "const uint16_t s20_phone_skin_packed[S20_SKIN_PACKED_LEN] = {",
    ]
    for i in range(0, len(packed), 12):
        chunk = packed[i : i + 12]
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in chunk) + ",")
    lines.append("};")
    lines.append("")
    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Generated {OUT_H} and {OUT_C}: {w}x{h}, {len(packed)} u16 words ({len(packed)*2} bytes)")


if __name__ == "__main__":
    main()
