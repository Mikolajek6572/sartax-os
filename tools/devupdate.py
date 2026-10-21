# Sartax OS - wgrywa nowe jadro do slotu A obrazu dysku testowego (do szybkich testow w QEMU)
import sys, struct
disk, kern = sys.argv[1], sys.argv[2]
k = open(kern, "rb").read()
with open(disk, "r+b") as f:
    f.seek(512)
    cfg = bytearray(f.read(512))
    assert cfg[:4] == b"SXCF", "dysk bez Sartax OS"
    sect = (len(k) + 511) // 512
    struct.pack_into("<I", cfg, 12, 0)
    struct.pack_into("<III", cfg, 16, 64, sect, len(k))
    f.seek(512)
    f.write(cfg)
    f.seek(64 * 512)
    f.write(k + b"\0" * (sect * 512 - len(k)))
print("wgrano jadro", len(k), "B")
