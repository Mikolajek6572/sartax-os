# Sartax OS

Calkowicie wlasny system operacyjny (x86-64) - nie dziala na Linuksie ani Windowsie.

- wlasny bootloader (MBR + plyta El Torito), tryb 64-bit, grafika VBE
- jadro w C: przerwania, sterta, PS/2, mysz zintegrowana z VirtualBox, ATA, PCI
- wlasny system plikow **SartaxFS**
- pulpit, menu Start, okna, eksplorator plikow, notatnik, podglad obrazow, ustawienia
- siec: e1000, ARP, IPv4, UDP, DHCP, DNS, TCP, HTTPS (BearSSL)
- Pobieralnia: pliki, programy `.sap` i aktualizacje systemu z tego repo (`repo/index.txt`)
- Przegladarka Sartax: wlasny silnik HTML, karty, obrazki PNG/JPEG/GIF, wlasna wyszukiwarka (wyniki DuckDuckGo)

## Uruchomienie w VirtualBox

Pobierz `sartax.iso` z zakladki Releases i uruchom `UtworzVM.bat` (albo ustaw recznie:
typ *Other/Unknown (64-bit)*, dysk na kontrolerze IDE, karta sieciowa *Intel PRO/1000 MT Desktop*, NAT).

## Budowanie

W WSL (Debian): `sudo apt install nasm xorriso python3-pil fonts-dejavu-core`, potem `make`.
