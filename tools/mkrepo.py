# Sartax OS - buduje repo/ (pliki do Pobieralni) i repo/index.txt
# uzycie: python3 tools/mkrepo.py [--system wersja opis]
import hashlib, os, shutil, sys

ENTRIES = [
    # typ, nazwa, wersja, sciezka w systemie, plik zrodlowy, plik w repo, opis
    ("program", "Snake", "1.0", "/Programy/Snake.sap", "build/apps/snake.sap", "programy/snake.sap",
     "Klasyczny waz - zbieraj jablka, rekord zapisuje sie w Dokumentach"),
    ("program", "Kalkulator", "1.0", "/Programy/Kalkulator.sap", "build/apps/kalkulator.sap", "programy/kalkulator.sap",
     "Prosty kalkulator: + - x : i procenty, dziala tez z klawiatury"),
    ("plik", "Ciekawostki o Sartax OS", "1.0", "/Dokumenty/Ciekawostki.txt", "repo/pliki/Ciekawostki.txt", "pliki/Ciekawostki.txt",
     "Krotki tekst o tym, jak dziala Sartax OS"),
    ("plik", "Moja pierwsza strona", "1.0", "/Dokumenty/strona.html", "repo/pliki/strona.html", "pliki/strona.html",
     "Przykladowa strona HTML - otworz ja w Przegladarce Sartax"),
]

lines = ["# Sartax OS - lista plikow w Pobieralni (generuje tools/mkrepo.py)",
         "# typ|nazwa|wersja|sciezka|url|rozmiar|sha256|opis"]
for typ, name, ver, target, src, dst, desc in ENTRIES:
    out = os.path.join("repo", dst)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.abspath(src) != os.path.abspath(out):
        shutil.copyfile(src, out)
    d = open(out, "rb").read()
    lines.append("|".join([typ, name, ver, target, dst, str(len(d)), hashlib.sha256(d).hexdigest(), desc]))

# aktualizacja systemu (jadro jako zasob w Releases na GitHubie)
if len(sys.argv) >= 4 and sys.argv[1] == "--system":
    ver, desc = sys.argv[2], sys.argv[3]
    k = open("build/kernel.bin", "rb").read()
    url = "https://github.com/Mikolajek6572/sartax-os/releases/download/v%s/sartax-kernel.sxk" % ver
    lines.append("|".join(["system", "Sartax OS " + ver, ver, "/System/jadro.sxk", url, str(len(k)),
                           hashlib.sha256(k).hexdigest(), desc]))
    shutil.copyfile("build/kernel.bin", "build/sartax-kernel.sxk")

open("repo/index.txt", "w", newline="\n", encoding="utf-8").write("\n".join(lines) + "\n")
print("\n".join(lines))
