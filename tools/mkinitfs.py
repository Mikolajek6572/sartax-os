# Sartax OS - pliki kopiowane przez instalator na nowy dysk
import sys, os, io, math
from PIL import Image, ImageDraw, ImageFilter

ROOT = "initfs"
files = []
for dp, dn, fn in os.walk(ROOT):
    dn.sort()
    for f in sorted(fn):
        p = os.path.join(dp, f)
        files.append(("/" + os.path.relpath(p, ROOT).replace(os.sep, "/"), open(p, "rb").read()))

def bmp(im):
    b = io.BytesIO()
    im.convert("RGB").save(b, "BMP")
    return b.getvalue()

def landscape(w, h, sky1, sky2, hills, sun):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line([(0, y), (w, y)], fill=tuple(int(sky1[i] + (sky2[i] - sky1[i]) * t) for i in range(3)))
    d.ellipse((sun[0] - 34, sun[1] - 34, sun[0] + 34, sun[1] + 34), fill=(255, 230, 150))
    for k, (col, base, amp, freq, ph) in enumerate(hills):
        pts = [(0, h)]
        for x in range(0, w + 8, 8):
            y = base + amp * math.sin(x * freq + ph) + amp * 0.4 * math.sin(x * freq * 2.7 + ph * 1.3)
            pts.append((x, y))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    return im.filter(ImageFilter.SMOOTH)

files.append(("/Obrazy/Gory.bmp", bmp(landscape(480, 300, (90, 170, 230), (255, 210, 160), [
    ((120, 140, 170), 150, 40, 0.012, 0.5), ((70, 120, 110), 200, 30, 0.02, 1.7), ((40, 90, 70), 245, 20, 0.03, 0.2)],
    (360, 90)))))
files.append(("/Obrazy/Zachod slonca.bmp", bmp(landscape(480, 300, (255, 140, 60), (255, 220, 120), [
    ((180, 80, 50), 190, 25, 0.015, 2.0), ((110, 50, 40), 235, 18, 0.025, 0.7), ((60, 30, 30), 270, 10, 0.04, 1.1)],
    (240, 170)))))

out = ["// wygenerowane przez tools/mkinitfs.py - nie edytowac", '#include "../kernel.h"', '#include "../fs.h"', ""]
for i, (path, data) in enumerate(files):
    out.append("static const u8 f%d[] = {" % i)
    for j in range(0, len(data), 40):
        out.append("    " + ",".join(str(v) for v in data[j:j + 40]) + ",")
    if not data:
        out.append("0")
    out.append("};")
out.append("const initfs_file_t g_initfs[] = {")
for i, (path, data) in enumerate(files):
    out.append('    {"%s", f%d, %d},' % (path.replace('"', '\\"'), i, len(data)))
out.append("    {0, 0, 0},")
out.append("};")
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(out) + "\n")
