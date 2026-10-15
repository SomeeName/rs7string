"""Plain (unencrypted TOC) PSARC reader/writer."""

import struct
import zlib
import hashlib
import sys

BLOCK = 65536


def read(path):
    d = open(path, "rb").read()
    magic, ver, comp, toclen, esz, n, bsz, flags = struct.unpack(">4sI4sIIIII", d[:32])
    assert magic == b"PSAR" and comp == b"zlib" and esz == 30, (magic, comp, esz)
    assert flags & 4 == 0, f"encrypted TOC (flags {flags})"
    ents = []
    for i in range(n):
        e = d[32 + i * 30 : 32 + (i + 1) * 30]
        md5, zi = e[:16], struct.unpack(">I", e[16:20])[0]
        ln = int.from_bytes(e[20:25], "big")
        off = int.from_bytes(e[25:30], "big")
        ents.append((md5, zi, ln, off))
    zbytes = 2 if bsz <= 65536 else 3
    zstart = 32 + n * 30
    nz = (toclen - zstart) // zbytes
    zl = [int.from_bytes(d[zstart + i * zbytes : zstart + (i + 1) * zbytes], "big") for i in range(nz)]
    files = []
    for md5, zi, ln, off in ents:
        out = bytearray()
        p = off
        b = zi
        while len(out) < ln:
            z = zl[b]
            if z == 0:
                chunk = d[p : p + bsz]
                p += bsz
            else:
                raw = d[p : p + z]
                p += z
                try:
                    chunk = zlib.decompress(raw)
                except zlib.error:
                    chunk = raw
            out += chunk
            b += 1
        files.append(bytes(out[:ln]))
    names = files[0].decode().split("\n")
    return dict(zip(names, files[1:])), dict(ver=ver, flags=flags, names=names)


def write(path, named, names, ver=0x00010004, flags=0):
    blobs = ["\n".join(names).encode()] + [named[n] for n in names]
    zl = []
    data = bytearray()
    ents = []
    for i, b in enumerate(blobs):
        zi = len(zl)
        off = len(data)
        for p in range(0, max(len(b), 1), BLOCK):
            chunk = b[p : p + BLOCK]
            if not chunk:
                break
            c = zlib.compress(chunk, 9)
            if len(c) < len(chunk):
                data += c
                zl.append(len(c))
            else:
                data += chunk
                zl.append(0 if len(chunk) == BLOCK else len(chunk))
        md5 = b"\0" * 16 if i == 0 else hashlib.md5(names[i - 1].encode()).digest()
        ents.append((md5, zi, len(b), off))
    n = len(blobs)
    toclen = 32 + n * 30 + len(zl) * 2
    hdr = struct.pack(">4sI4sIIIII", b"PSAR", ver, b"zlib", toclen, 30, n, BLOCK, flags)
    toc = b"".join(
        md5 + struct.pack(">I", zi) + ln.to_bytes(5, "big") + (off + toclen).to_bytes(5, "big")
        for md5, zi, ln, off in ents
    )
    zt = b"".join(z.to_bytes(2, "big") for z in zl)
    open(path, "wb").write(hdr + toc + zt + data)


if __name__ == "__main__":
    files, meta = read(sys.argv[1])
    print(meta["ver"], meta["flags"])
    for n in meta["names"]:
        print(len(files[n]), n)
