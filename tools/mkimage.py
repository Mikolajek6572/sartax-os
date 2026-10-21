# Sartax OS - skladanie obrazu jadra i obrazu rozruchowego plyty
import sys, struct

mode = sys.argv[1]
if mode == "kernel":
    data = bytearray(open(sys.argv[2], "rb").read())
    ver = sys.argv[4].encode()
    assert data[8:12] == b"SXKN", "brak naglowka jadra"
    i = data.find(b"VERSION_STRING__")
    assert i > 0
    data[i:i + 16] = ver.ljust(16, b"\0")
    size = struct.unpack_from("<I", data, 16)[0]
    assert 0 <= size - len(data) < 4096, "rozmiar obrazu %d != %d" % (size, len(data))
    data += bytes(size - len(data))
    open(sys.argv[3], "wb").write(data)
    print("  jadro: %d KB, wersja %s" % (len(data) // 1024, sys.argv[4]))
elif mode == "cd":
    cd = bytearray(open(sys.argv[2], "rb").read())
    st2 = open(sys.argv[3], "rb").read()
    k = open(sys.argv[4], "rb").read()
    assert len(cd) == 2048 and len(st2) == 16384
    struct.pack_into("<I", cd, 64, len(k))
    img = bytes(cd) + st2 + k
    img += b"\0" * (-len(img) % 2048)
    open(sys.argv[5], "wb").write(img)
