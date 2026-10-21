# Sartax OS - automatyczny test w QEMU (WSL)
# uzycie: python3 tools/qtest.py <skrypt> [--disk plik] [--cd iso] [--boot d|c]
# skrypt: linie "wait 3", "shot nazwa.png", "key ctrl-a", "type tekst", "move x y", "click", "rclick", "dclick"
import sys, os, time, socket, subprocess, argparse
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("script")
ap.add_argument("--disk")
ap.add_argument("--cd", default="build/sartax.iso")
ap.add_argument("--boot", default="d")
ap.add_argument("--out", default="build/shots")
ap.add_argument("--net", action="store_true")
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
mon = "/tmp/sartax-mon.sock"
if os.path.exists(mon):
    os.remove(mon)
cmd = ["qemu-system-x86_64", "-m", "512", "-display", "none", "-serial", "file:%s/serial.log" % a.out,
       "-monitor", "unix:%s,server,nowait" % mon, "-boot", a.boot, "-rtc", "base=utc"]
if a.cd:
    cmd += ["-drive", "file=%s,media=cdrom,if=ide,index=2" % a.cd]
if a.disk:
    cmd += ["-drive", "file=%s,format=raw,if=ide,index=0" % a.disk]
if a.net:
    cmd += ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
else:
    cmd += ["-nic", "none"]
p = subprocess.Popen(cmd)
for _ in range(50):
    if os.path.exists(mon):
        break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX)
s.connect(mon)
s.settimeout(0.5)

def m(c):
    s.sendall((c + "\n").encode())
    time.sleep(0.15)
    try:
        while s.recv(65536):
            pass
    except Exception:
        pass

mx = my = 0
KEYMAP = {" ": "spc", ".": "dot", ",": "comma", "-": "minus", "/": "slash", "_": "shift-minus", ":": "shift-semicolon"}
for line in open(a.script):
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    op, _, arg = line.partition(" ")
    if op == "wait":
        time.sleep(float(arg))
    elif op == "shot":
        m("screendump /tmp/sartax-shot.ppm")
        time.sleep(0.5)
        Image.open("/tmp/sartax-shot.ppm").save(os.path.join(a.out, arg))
        print("zrzut", arg)
    elif op == "key":
        m("sendkey " + arg)
    elif op == "type":
        for ch in arg:
            k = KEYMAP.get(ch, ch)
            if ch.isupper():
                k = "shift-" + ch.lower()
            m("sendkey " + k)
    elif op == "move":
        # mysz PS/2 jest wzgledna - najpierw do lewego gornego rogu
        x, y = map(int, arg.split())
        for _ in range(20):
            m("mouse_move -100 -100")
        for _ in range(40):
            dx, dy = max(-50, min(50, x)), max(-50, min(50, y))
            if dx == 0 and dy == 0:
                break
            m("mouse_move %d %d" % (dx, dy))
            x -= dx
            y -= dy
    elif op in ("click", "rclick"):
        b = 1 if op == "click" else 2
        m("mouse_button %d" % b)
        m("mouse_button 0")
    elif op == "dclick":
        for _ in range(2):
            m("mouse_button 1")
            m("mouse_button 0")
    elif op == "mon":
        m(arg)
m("quit")
p.wait(timeout=10)
print(open(os.path.join(a.out, "serial.log"), errors="replace").read()[-3000:])
