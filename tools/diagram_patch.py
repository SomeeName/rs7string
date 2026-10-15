"""Generate MIT reconstruction recipes from the original/edited movie copies.

Run from the repository root: uv run python tools/diagram_patch.py
Defaults read Desktop/rocksmith-assets and write only this checkout's data/.
No compiler, archives, keys or game access. Retained bytes are COPY instructions,
including individual bytes: literals contain only byte values absent from the
original tag. Neither movie nor the original class is shipped.
"""

import argparse
import hashlib
from pathlib import Path
import struct

TARGETS = {"libs/commonlib.gfx": 1103, "views/contentpanel.gfx": 28, "views/ge_game.gfx": 26}
MAGIC = b"R7DA\x01\0\0\0"
LIMIT = 16 * 1024 * 1024


def make_recipe(old, new):
    if not old or max(len(old), len(new)) > LIMIT:
        raise ValueError("tag size")
    index = {}
    for i in range(len(old) - 7):
        index.setdefault(old[i : i + 8], []).append(i)
    ops, pos = [], 0
    while pos < len(new):
        best, length = -1, 0
        for at in index.get(new[pos : pos + 8], []):
            n = 8
            while at + n < len(old) and pos + n < len(new) and old[at + n] == new[pos + n]:
                n += 1
            if n > length:
                best, length = at, n
        if best < 0:
            best = old.find(new[pos : pos + 1])
            if best >= 0:
                length = 1
                while best + length < len(old) and pos + length < len(new) and old[best + length] == new[pos + length]:
                    length += 1
        if best >= 0:
            if ops and ops[-1][0] == 0 and ops[-1][1] + ops[-1][2] == best:
                op = ops[-1]
                ops[-1] = (0, op[1], op[2] + length)
            else:
                ops.append((0, best, length))
            pos += length
        else:
            literal = new[pos : pos + 1]
            if ops and ops[-1][0] == 1:
                ops[-1] = (1, ops[-1][1] + literal)
            else:
                ops.append((1, literal))
            pos += 1
    out = MAGIC + struct.pack("<III", len(old), len(new), len(ops))
    out += hashlib.sha256(old).digest() + hashlib.sha256(new).digest()
    for op in ops:
        out += b"\0" + struct.pack("<II", *op[1:]) if op[0] == 0 else b"\1" + struct.pack("<I", len(op[1])) + op[1]
    return out


def apply_recipe(old, recipe):
    if len(recipe) < 84 or recipe[:8] != MAGIC:
        raise ValueError("recipe header")
    size, result, count = struct.unpack_from("<III", recipe, 8)
    if size != len(old) or result > LIMIT or hashlib.sha256(old).digest() != recipe[20:52]:
        raise ValueError("original hash")
    pos, out = 84, bytearray()
    for _ in range(count):
        if pos >= len(recipe):
            raise ValueError("truncated recipe")
        op = recipe[pos]
        pos += 1
        if op == 0:
            if pos + 8 > len(recipe):
                raise ValueError("copy header")
            at, n = struct.unpack_from("<II", recipe, pos)
            pos += 8
            if not n or at + n > len(old):
                raise ValueError("copy bounds")
            out += old[at : at + n]
        elif op == 1:
            if pos + 4 > len(recipe):
                raise ValueError("literal header")
            (n,) = struct.unpack_from("<I", recipe, pos)
            pos += 4
            if not n or pos + n > len(recipe):
                raise ValueError("literal bounds")
            out += recipe[pos : pos + n]
            pos += n
        else:
            raise ValueError("opcode")
        if len(out) > result:
            raise ValueError("output bounds")
    if pos != len(recipe) or len(out) != result or hashlib.sha256(out).digest() != recipe[52:84]:
        raise ValueError("result hash")
    return bytes(out)


def tag_span(data, sprite):
    if len(data) < 13 or data[:3] not in (b"GFX", b"FWS"):
        raise ValueError("uncompressed GFX/SWF required")
    if len(data) > LIMIT or struct.unpack_from("<I", data, 4)[0] != len(data):
        raise ValueError("movie length")
    nbits = data[8] >> 3
    pos = 8 + (5 + nbits * 4 + 7) // 8 + 4
    if not nbits or pos > len(data):
        raise ValueError("frame header")
    spans = []
    while pos < len(data):
        start = pos
        if pos + 2 > len(data):
            raise ValueError("tag header")
        (word,) = struct.unpack_from("<H", data, pos)
        pos += 2
        n = word & 63
        if n == 63:
            if pos + 4 > len(data):
                raise ValueError("long header")
            (n,) = struct.unpack_from("<I", data, pos)
            pos += 4
        end = pos + n
        if end > len(data):
            raise ValueError("tag length")
        if word >> 6 == 59 and n >= 2 and struct.unpack_from("<H", data, pos)[0] == sprite:
            spans.append((start, end))
        if word >> 6 == 0:
            if n or end != len(data):
                raise ValueError("End tag")
            break
        pos = end
    else:
        raise ValueError("missing End")
    if len(spans) != 1:
        raise ValueError("nonunique class tag")
    return spans[0]


def movie_recipe(old, new, sprite):
    a, b = tag_span(old, sprite)
    c, d = tag_span(new, sprite)
    if old[:4] != new[:4] or old[8:a] != new[8:c] or old[b:] != new[d:]:
        raise ValueError("more than one tag changed")
    return make_recipe(old[a:b], new[c:d])


def swap_movie(old, recipe, sprite):
    try:
        a, b = tag_span(old, sprite)
        tag = apply_recipe(old[a:b], recipe)
        out = bytearray(old[:a] + tag + old[b:])
        struct.pack_into("<I", out, 4, len(out))
        if tag_span(out, sprite) != (a, a + len(tag)):
            return None
        return bytes(out)
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path.home() / "Desktop" / "rocksmith-assets"
    parser.add_argument("--original", type=Path, default=root / "static.psarc" / "gfxassets")
    parser.add_argument("--edited", type=Path, default=root / "edited2" / "gfx")
    args = parser.parse_args()
    dest = Path(__file__).resolve().parents[1] / "data" / "chord-diagrams"
    staged = []
    for name, sprite in TARGETS.items():
        old, new = (args.original / name).read_bytes(), (args.edited / name).read_bytes()
        recipe = movie_recipe(old, new, sprite)
        if swap_movie(old, recipe, sprite) != new:
            raise ValueError("roundtrip mismatch")
        staged.append((Path(name).stem + ".rs7da", recipe))
        print(
            f"{name}: one tag; byte-identical; old={hashlib.sha256(old).hexdigest()} new={hashlib.sha256(new).hexdigest()} recipe={len(recipe)} bytes"
        )
    dest.mkdir(parents=True, exist_ok=True)
    for name, data in staged:
        (dest / name).write_bytes(data)


if __name__ == "__main__":
    main()
