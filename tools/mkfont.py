# Sartax OS - generator czcionek (DejaVu -> tablice C z wygladzonymi glifami)
import sys
from PIL import Image, ImageDraw, ImageFont

FD = "/usr/share/fonts/truetype/dejavu/"
FONTS = [  # kolejnosc = enum FONT_* w gfx.h
    ("small", "DejaVuSans.ttf", 11),
    ("ui", "DejaVuSans.ttf", 13),
    ("bold", "DejaVuSans-Bold.ttf", 13),
    ("title", "DejaVuSans-Bold.ttf", 17),
    ("big", "DejaVuSans-Bold.ttf", 28),
    ("mono", "DejaVuSansMono.ttf", 14),
    ("huge", "DejaVuSans-Bold.ttf", 52),
]

cps = list(range(32, 127)) + list(range(0xA0, 0x100))
cps += [0x104, 0x105, 0x106, 0x107, 0x118, 0x119, 0x141, 0x142, 0x143, 0x144, 0x15A, 0x15B, 0x179, 0x17A, 0x17B, 0x17C]
cps += [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x20AC,
        0x2190, 0x2191, 0x2192, 0x2193, 0x2713, 0x2715, 0x25B2, 0x25B6, 0x25BC, 0x25C0, 0x2605, 0x2665]
cps = sorted(set(cps))

out = ["// wygenerowane przez tools/mkfont.py - nie edytowac", '#include "../gfx.h"', ""]
defs = []
for name, file, size in FONTS:
    font = ImageFont.truetype(FD + file, size)
    fallback = ImageFont.truetype(FD + "DejaVuSans.ttf", size)
    asc, desc = font.getmetrics()
    glyphs = []
    bitmap = bytearray()
    for cp in cps:
        ch = chr(cp)
        f = font
        try:
            if font.getmask(ch).getbbox() is None and cp != 32 and cp != 0xA0:
                f = fallback
        except Exception:
            f = fallback
        adv = int(round(f.getlength(ch)))
        if name == "mono":
            adv = int(round(font.getlength("M")))
        bbox = f.getbbox(ch)
        x0, y0, x1, y1 = bbox
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        off = len(bitmap)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=f, fill=255)
            data = img.tobytes()
            # gamma - troche grubsze krawedzie przy malych rozmiarach
            if size <= 14:
                data = bytes(min(255, int((v / 255.0) ** 0.8 * 255 + 0.5)) for v in data)
            bitmap += data
        glyphs.append((cp, x0, y0, w, h, max(0, min(255, adv)), off))
    out.append("static const glyph_t g_%s[] = {" % name)
    for g in glyphs:
        out.append("    {%d, %d, %d, %d, %d, %d, 0, %d}," % g)
    out.append("};")
    out.append("static const u8 b_%s[] = {" % name)
    for i in range(0, len(bitmap), 32):
        out.append("    " + ",".join(str(v) for v in bitmap[i:i + 32]) + ",")
    if not bitmap:
        out.append("0")
    out.append("};")
    defs.append("    {g_%s, %d, b_%s, %d, %d}," % (name, len(glyphs), name, asc + desc, asc))

out.append("const font_t g_fonts[FONT_COUNT] = {")
out += defs
out.append("};")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
