# Sartax OS - generator ikon (rysowane w PIL w 4x i zmniejszane)
import sys, math
from PIL import Image, ImageDraw, ImageFont

S = 192  # plotno robocze
FD = "/usr/share/fonts/truetype/dejavu/"

def canvas():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)

def grad_rect(im, box, c1, c2, radius):
    x0, y0, x1, y1 = [int(v) for v in box]
    g = Image.new("RGBA", (x1 - x0, y1 - y0))
    gd = ImageDraw.Draw(g)
    h = y1 - y0
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3)) + (255,)
        gd.line([(0, y), (x1 - x0, y)], fill=c)
    m = Image.new("L", g.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, g.size[0] - 1, g.size[1] - 1], radius=radius, fill=255)
    im.paste(g, (x0, y0), m)

def shadow(im, box, radius, alpha=70):
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([box[0], box[1] + 6, box[2], box[3] + 6], radius=radius, fill=(0, 0, 0, alpha))
    from PIL import ImageFilter
    sh = sh.filter(ImageFilter.GaussianBlur(6))
    base = Image.alpha_composite(sh, im)
    im.paste(base, (0, 0))

def page(color_band=None, fold=True):
    im, d = canvas()
    box = (40, 18, 152, 176)
    shadow(im, box, 14, 60)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box, radius=14, fill=(250, 251, 253, 255), outline=(170, 180, 192, 255), width=4)
    if fold:
        d.polygon([(118, 18), (152, 52), (118, 52)], fill=(214, 222, 232, 255))
        d.line([(118, 18), (118, 52), (152, 52)], fill=(170, 180, 192, 255), width=4)
    if color_band:
        d.rounded_rectangle((40, 136, 152, 176), radius=14, fill=color_band)
        d.rectangle((40, 136, 152, 150), fill=color_band)
    return im, d

def ic_folder(col=(255, 196, 60), col2=(245, 160, 30), emblem=None):
    im, d = canvas()
    shadow(im, (14, 40, 178, 168), 16)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((14, 36, 90, 70), radius=12, fill=tuple(int(v * 0.85) for v in col2) + (255,))
    grad_rect(im, (14, 54, 178, 168), col2, col2, 16)
    grad_rect(im, (14, 68, 178, 170), col, col2, 16)
    d = ImageDraw.Draw(im)
    d.line([(26, 74), (166, 74)], fill=(255, 255, 255, 110), width=4)
    if emblem:
        emblem(d)
    return im

def ic_file():
    im, d = page()
    return im

def ic_text():
    im, d = page()
    for i, y in enumerate(range(70, 160, 18)):
        d.rounded_rectangle((58, y, 134 - (i % 2) * 22, y + 7), radius=3, fill=(70, 130, 200, 255))
    return im

def ic_image():
    im, d = page(fold=False)
    grad_rect(im, (52, 34, 140, 150), (120, 200, 255), (210, 240, 255), 8)
    d = ImageDraw.Draw(im)
    d.ellipse((104, 50, 128, 74), fill=(255, 210, 60, 255))
    d.polygon([(52, 150), (86, 92), (114, 150)], fill=(60, 170, 90, 255))
    d.polygon([(88, 150), (116, 108), (140, 150)], fill=(40, 140, 70, 255))
    return im

def rounded_tile(c1, c2):
    im, d = canvas()
    shadow(im, (18, 18, 174, 174), 40)
    grad_rect(im, (18, 18, 174, 174), c1, c2, 40)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((18, 18, 174, 174), radius=40, outline=(255, 255, 255, 60), width=3)
    return im, d

def ic_app():
    im, d = rounded_tile((60, 170, 255), (20, 110, 210))
    d.rounded_rectangle((52, 52, 90, 90), radius=10, fill=(255, 255, 255, 240))
    d.rounded_rectangle((102, 52, 140, 90), radius=10, fill=(255, 255, 255, 170))
    d.rounded_rectangle((52, 102, 90, 140), radius=10, fill=(255, 255, 255, 170))
    d.rounded_rectangle((102, 102, 140, 140), radius=10, fill=(255, 255, 255, 240))
    return im

def ic_computer():
    im, d = canvas()
    shadow(im, (16, 28, 176, 136), 14)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((16, 26, 176, 138), radius=14, fill=(52, 62, 76, 255))
    grad_rect(im, (28, 38, 164, 126), (40, 180, 230), (30, 120, 200), 6)
    d = ImageDraw.Draw(im)
    d.polygon([(28, 38), (110, 38), (60, 126), (28, 126)], fill=(255, 255, 255, 40))
    d.rectangle((84, 138, 108, 160), fill=(120, 130, 145, 255))
    d.rounded_rectangle((54, 158, 138, 172), radius=6, fill=(150, 160, 175, 255))
    return im

def ic_download():
    im, d = rounded_tile((70, 210, 120), (20, 150, 80))
    d.rectangle((84, 44, 108, 104), fill=(255, 255, 255, 255))
    d.polygon([(60, 98), (132, 98), (96, 136)], fill=(255, 255, 255, 255))
    d.rounded_rectangle((50, 140, 142, 152), radius=6, fill=(255, 255, 255, 230))
    return im

def gear(d, cx, cy, r, teeth, col, hole_col):
    pts = []
    n = teeth * 4
    for i in range(n):
        a = 2 * math.pi * i / n
        rr = r if (i % 4) in (0, 1) else r * 0.78
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d.polygon(pts, fill=col)
    d.ellipse((cx - r * 0.62, cy - r * 0.62, cx + r * 0.62, cy + r * 0.62), fill=col)
    d.ellipse((cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3), fill=hole_col)

def ic_settings():
    im, d = rounded_tile((120, 135, 150), (70, 82, 98))
    gear(d, 96, 96, 58, 9, (255, 255, 255, 255), (90, 102, 118, 255))
    return im

def ic_notepad():
    im, d = canvas()
    shadow(im, (36, 24, 156, 176), 12)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((36, 24, 156, 176), radius=12, fill=(255, 250, 220, 255), outline=(200, 180, 120, 255), width=4)
    d.rounded_rectangle((36, 24, 156, 52), radius=12, fill=(250, 170, 40, 255))
    d.rectangle((36, 40, 156, 52), fill=(250, 170, 40, 255))
    for x in range(52, 150, 22):
        d.ellipse((x, 16, x + 12, 32), fill=(110, 120, 130, 255))
    for y in range(72, 168, 18):
        d.line([(52, y), (140, y)], fill=(150, 190, 230, 255), width=4)
    # olowek
    d.polygon([(150, 70), (170, 90), (110, 150), (90, 130)], fill=(255, 120, 60, 255))
    d.polygon([(90, 130), (110, 150), (82, 160)], fill=(250, 220, 170, 255))
    d.polygon([(82, 160), (88, 150), (92, 156)], fill=(60, 60, 60, 255))
    return im

def ic_viewer():
    im, d = rounded_tile((255, 170, 60), (240, 110, 40))
    d.rounded_rectangle((42, 50, 150, 142), radius=10, fill=(255, 255, 255, 255))
    grad_rect(im, (52, 60, 140, 132), (110, 200, 255), (200, 235, 255), 6)
    d = ImageDraw.Draw(im)
    d.ellipse((108, 68, 128, 88), fill=(255, 220, 70, 255))
    d.polygon([(52, 132), (84, 88), (112, 132)], fill=(50, 160, 80, 255))
    d.polygon([(90, 132), (114, 102), (140, 132)], fill=(30, 130, 60, 255))
    return im

def arrows_circle(d, cx, cy, r, w, col):
    d.arc((cx - r, cy - r, cx + r, cy + r), 200, 340, fill=col, width=w)
    d.arc((cx - r, cy - r, cx + r, cy + r), 20, 160, fill=col, width=w)
    def head(angle, cw):
        a = math.radians(angle)
        px, py = cx + r * math.cos(a), cy + r * math.sin(a)
        t = a + (math.pi / 2 if cw else -math.pi / 2)
        L = w * 1.6
        d.polygon([(px + L * math.cos(t), py + L * math.sin(t)),
                   (px + L * 0.9 * math.cos(a), py + L * 0.9 * math.sin(a)),
                   (px - L * 0.9 * math.cos(a), py - L * 0.9 * math.sin(a))], fill=col)
    head(340, True)
    head(160, True)

def ic_update():
    im, d = rounded_tile((255, 180, 40), (240, 120, 20))
    arrows_circle(d, 96, 96, 48, 16, (255, 255, 255, 255))
    return im

def ic_disk():
    im, d = canvas()
    shadow(im, (20, 56, 172, 140), 18)
    grad_rect(im, (20, 56, 172, 140), (205, 212, 222), (150, 160, 175), 18)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((20, 56, 172, 140), radius=18, outline=(120, 130, 145, 255), width=3)
    d.rounded_rectangle((36, 104, 120, 118), radius=6, fill=(90, 100, 115, 255))
    d.ellipse((140, 102, 156, 118), fill=(60, 220, 110, 255))
    return im

def ic_logo():
    im, d = canvas()
    shadow(im, (14, 14, 178, 178), 82)
    # kolorowe kolo - zielony, niebieski, pomaranczowy (bez fioletu)
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse((14, 14, 178, 178), fill=255)
    g = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(g)
    for y in range(S):
        for x in range(0, S, 2):
            t = (x + y) / (2 * S)
            if t < 0.5:
                u = t / 0.5
                c = (int(20 + (40 - 20) * u), int(200 + (150 - 200) * u), int(150 + (255 - 150) * u))
            else:
                u = (t - 0.5) / 0.5
                c = (int(40 + (255 - 40) * u), int(150 + (170 - 150) * u), int(255 + (40 - 255) * u))
            gd.line([(x, y), (x + 1, y)], fill=c + (255,))
    im.paste(g, (0, 0), m)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FD + "DejaVuSans-Bold.ttf", 120)
    bb = d.textbbox((0, 0), "S", font=f)
    d.text((96 - (bb[0] + bb[2]) / 2 + 2, 96 - (bb[1] + bb[3]) / 2 + 4), "S", font=f, fill=(0, 0, 0, 60))
    d.text((96 - (bb[0] + bb[2]) / 2, 96 - (bb[1] + bb[3]) / 2), "S", font=f, fill=(255, 255, 255, 255))
    return im

def ic_info():
    im, d = canvas()
    d.ellipse((18, 18, 174, 174), fill=(40, 140, 230, 255))
    d.ellipse((84, 46, 108, 70), fill=(255, 255, 255, 255))
    d.rounded_rectangle((84, 84, 108, 148), radius=8, fill=(255, 255, 255, 255))
    return im

def ic_power():
    im, d = canvas()
    d.ellipse((18, 18, 174, 174), fill=(230, 70, 60, 255))
    d.arc((52, 52, 140, 140), 300, 240, fill=(255, 255, 255, 255), width=14)
    d.rounded_rectangle((89, 36, 103, 98), radius=7, fill=(255, 255, 255, 255))
    return im

def ic_restart():
    im, d = canvas()
    d.ellipse((18, 18, 174, 174), fill=(40, 170, 120, 255))
    arrows_circle(d, 96, 96, 44, 14, (255, 255, 255, 255))
    return im

def ic_user():
    im, d = canvas()
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse((10, 10, 182, 182), fill=255)
    g = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad_rect(g, (10, 10, 182, 182), (70, 200, 230), (30, 130, 200), 86)
    gd = ImageDraw.Draw(g)
    gd.ellipse((64, 44, 128, 108), fill=(255, 255, 255, 255))
    gd.ellipse((36, 116, 156, 230), fill=(255, 255, 255, 255))
    im.paste(g, (0, 0), m)
    return im

def ic_desktop():
    im, d = canvas()
    shadow(im, (16, 30, 176, 140), 12)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((16, 28, 176, 142), radius=12, fill=(40, 50, 64, 255))
    grad_rect(im, (26, 38, 166, 130), (60, 200, 170), (40, 140, 220), 6)
    d = ImageDraw.Draw(im)
    d.rectangle((26, 118, 166, 130), fill=(30, 40, 52, 220))
    d.rectangle((84, 142, 108, 160), fill=(120, 130, 145, 255))
    d.rounded_rectangle((54, 158, 138, 170), radius=6, fill=(150, 160, 175, 255))
    return im

def ic_trash():
    im, d = canvas()
    d.rounded_rectangle((40, 40, 152, 60), radius=8, fill=(110, 125, 140, 255))
    d.rounded_rectangle((78, 26, 114, 44), radius=6, fill=(110, 125, 140, 255))
    grad_rect(im, (50, 62, 142, 176), (170, 185, 200), (120, 135, 150), 12)
    d = ImageDraw.Draw(im)
    for x in (74, 96, 118):
        d.rounded_rectangle((x - 4, 80, x + 4, 160), radius=4, fill=(255, 255, 255, 140))
    return im

def ic_warning():
    im, d = canvas()
    d.polygon([(96, 16), (182, 170), (10, 170)], fill=(255, 190, 40, 255))
    d.rounded_rectangle((86, 62, 106, 128), radius=8, fill=(60, 50, 30, 255))
    d.ellipse((85, 136, 107, 158), fill=(60, 50, 30, 255))
    return im

def toolbar_arrow(direction):
    im, d = canvas()
    col = (60, 72, 88, 255)
    if direction == "left":
        d.polygon([(40, 96), (100, 36), (100, 72), (156, 72), (156, 120), (100, 120), (100, 156)], fill=col)
    elif direction == "right":
        d.polygon([(152, 96), (92, 36), (92, 72), (36, 72), (36, 120), (92, 120), (92, 156)], fill=col)
    else:
        d.polygon([(96, 36), (156, 96), (120, 96), (120, 156), (72, 156), (72, 96), (36, 96)], fill=col)
    return im

def ic_newfolder():
    im = ic_folder()
    d = ImageDraw.Draw(im)
    d.ellipse((104, 96, 184, 176), fill=(40, 170, 90, 255))
    d.rectangle((136, 112, 152, 160), fill=(255, 255, 255, 255))
    d.rectangle((120, 128, 168, 144), fill=(255, 255, 255, 255))
    return im

def ic_newfile():
    im = ic_text()
    d = ImageDraw.Draw(im)
    d.ellipse((104, 104, 184, 184), fill=(40, 170, 90, 255))
    d.rectangle((136, 120, 152, 168), fill=(255, 255, 255, 255))
    d.rectangle((120, 136, 168, 152), fill=(255, 255, 255, 255))
    return im

def ic_refresh():
    im, d = canvas()
    arrows_circle(d, 96, 96, 56, 20, (60, 72, 88, 255))
    return im

def ic_search():
    im, d = canvas()
    d.ellipse((30, 30, 130, 130), outline=(60, 72, 88, 255), width=18)
    d.line([(116, 116), (166, 166)], fill=(60, 72, 88, 255), width=26)
    return im

def ic_globe():
    im, d = canvas()
    shadow(im, (14, 14, 178, 178), 82)
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).ellipse((14, 14, 178, 178), fill=255)
    g = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad_rect(g, (14, 14, 178, 178), (60, 190, 250), (20, 110, 210), 82)
    gd = ImageDraw.Draw(g)
    # kontynenty
    gd.polygon([(40, 60), (80, 44), (100, 62), (88, 90), (96, 120), (70, 140), (58, 110), (36, 96)], fill=(80, 200, 110, 255))
    gd.polygon([(112, 40), (150, 52), (164, 84), (140, 92), (124, 76)], fill=(80, 200, 110, 255))
    gd.polygon([(116, 112), (150, 104), (156, 140), (128, 160)], fill=(80, 200, 110, 255))
    im.paste(g, (0, 0), m)
    d = ImageDraw.Draw(im)
    d.ellipse((14, 14, 178, 178), outline=(255, 255, 255, 120), width=4)
    d.arc((54, 14, 138, 178), 0, 360, fill=(255, 255, 255, 90), width=3)
    d.line([(14, 96), (178, 96)], fill=(255, 255, 255, 90), width=3)
    # kursor-strzalka jak w przegladarce
    d.polygon([(118, 112), (178, 140), (152, 148), (168, 176), (156, 182), (140, 154), (122, 170)], fill=(255, 160, 40, 255),
              outline=(255, 255, 255, 255))
    return im

def ic_home():
    im, d = canvas()
    col = (60, 72, 88, 255)
    d.polygon([(96, 28), (176, 96), (156, 96), (156, 164), (36, 164), (36, 96), (16, 96)], fill=col)
    d.rectangle((80, 112, 112, 164), fill=(255, 255, 255, 255))
    return im

def emb_star(d):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = 30 if i % 2 == 0 else 13
        pts.append((96 + r * math.cos(a), 120 + r * math.sin(a)))
    d.polygon(pts, fill=(255, 255, 255, 230))

def emb_doc(d):
    d.rounded_rectangle((74, 92, 118, 150), radius=5, fill=(255, 255, 255, 235))
    for y in (104, 116, 128):
        d.line([(82, y), (110, y)], fill=(240, 160, 40, 255), width=4)

def emb_down(d):
    d.rectangle((88, 92, 104, 124), fill=(255, 255, 255, 235))
    d.polygon([(74, 120), (118, 120), (96, 146)], fill=(255, 255, 255, 235))

def emb_pic(d):
    d.rounded_rectangle((70, 94, 122, 146), radius=5, fill=(255, 255, 255, 235))
    d.polygon([(74, 142), (90, 116), (106, 142)], fill=(60, 160, 90, 255))
    d.ellipse((102, 100, 114, 112), fill=(255, 190, 40, 255))

def emb_apps(d):
    for (x, y) in ((76, 98), (100, 98), (76, 122), (100, 122)):
        d.rounded_rectangle((x, y, x + 18, y + 18), radius=4, fill=(255, 255, 255, 235))

def emb_desk(d):
    d.rounded_rectangle((70, 96, 122, 134), radius=4, fill=(255, 255, 255, 235))
    d.rectangle((90, 134, 102, 144), fill=(255, 255, 255, 235))

ICONS = [  # kolejnosc = enum ICON_* w gfx.h
    ("folder", lambda: ic_folder()),
    ("file", ic_file),
    ("text", ic_text),
    ("image", ic_image),
    ("app", ic_app),
    ("computer", ic_computer),
    ("download", ic_download),
    ("settings", ic_settings),
    ("notepad", ic_notepad),
    ("viewer", ic_viewer),
    ("update", ic_update),
    ("disk", ic_disk),
    ("logo", ic_logo),
    ("info", ic_info),
    ("power", ic_power),
    ("user", ic_user),
    ("desktop", lambda: ic_folder((90, 200, 240), (40, 150, 220), emb_desk)),
    ("docs", lambda: ic_folder(emblem=emb_doc)),
    ("pictures", lambda: ic_folder((120, 220, 130), (50, 170, 90), emb_pic)),
    ("programs", lambda: ic_folder((255, 160, 90), (240, 110, 50), emb_apps)),
    ("back", lambda: toolbar_arrow("left")),
    ("forward", lambda: toolbar_arrow("right")),
    ("up", lambda: toolbar_arrow("up")),
    ("newfolder", ic_newfolder),
    ("refresh", ic_refresh),
    ("restart", ic_restart),
    ("warning", ic_warning),
    ("trash", ic_trash),
    ("newfile", ic_newfile),
    ("search", ic_search),
    ("globe", ic_globe),
    ("home", ic_home),
]
SIZES = [16, 24, 32, 48]

out = ["// wygenerowane przez tools/mkicons.py - nie edytowac", '#include "../gfx.h"', ""]
table = []
for name, fn in ICONS:
    big = fn()
    row = []
    for sz in SIZES:
        im = big.resize((sz, sz), Image.LANCZOS)
        px = im.load()
        vals = []
        for y in range(sz):
            for x in range(sz):
                r, g, b, a = px[x, y]
                vals.append("0x%08x" % ((a << 24) | (r << 16) | (g << 8) | b))
        out.append("static const u32 px_%s_%d[] = {" % (name, sz))
        for i in range(0, len(vals), 12):
            out.append("    " + ",".join(vals[i:i + 12]) + ",")
        out.append("};")
        row.append("{%d, %d, px_%s_%d}" % (sz, sz, name, sz))
    table.append("    {" + ", ".join(row) + "},")
    if len(sys.argv) > 2:
        big.resize((96, 96), Image.LANCZOS).save("%s/%s.png" % (sys.argv[2], name))

out.append("static const image_t icons[ICON_COUNT][4] = {")
out += table
out.append("};")
out.append("""
const image_t *icon_get(int id, int size) {
    if (id < 0 || id >= ICON_COUNT) id = ICON_FILE;
    int i = size <= 16 ? 0 : size <= 24 ? 1 : size <= 32 ? 2 : 3;
    return &icons[id][i];
}""")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
