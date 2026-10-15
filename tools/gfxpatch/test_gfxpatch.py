import struct
import unittest
import zlib
from pathlib import Path

import gfxpatch as gp


def tag(code, payload):
    return struct.pack("<HI", (code << 6) | 63, len(payload)) + payload


def movie(tags, compressed=False):
    # Minimal RECT (nbits=1) + frame rate/count.
    body = b"\x08\x00\x00\x0c\x01\x00" + b"".join(tags)
    return (
        (b"CFX" if compressed else b"GFX")
        + b"\x0a"
        + struct.pack("<I", len(body) + 8)
        + (zlib.compress(body) if compressed else body)
    )


class BinaryTests(unittest.TestCase):
    def test_only_target_changes(self):
        before = movie([tag(56, b"exports"), tag(59, b"\x4f\x04old"), tag(0, b"")])
        compiled = movie([tag(56, b"REWRITTEN"), tag(59, b"\x4f\x04new"), tag(0, b"")])
        result = gp.transplant(before, compiled, 1103)
        a, b = gp.parse_movie(before), gp.parse_movie(result)
        self.assertEqual(a.tags[0].raw, b.tags[0].raw)
        self.assertEqual(a.tags[2].raw, b.tags[2].raw)
        self.assertEqual(b.tags[1].payload, b"\x4f\x04new")

    def test_compressed_preserved(self):
        before = movie([tag(59, b"\x1c\x00old"), tag(0, b"")], True)
        compiled = movie([tag(59, b"\x1c\x00new"), tag(0, b"")])
        result = gp.transplant(before, compiled, 28)
        self.assertEqual(result[:3], b"CFX")
        self.assertEqual(gp.parse_movie(result).tags[0].payload, b"\x1c\x00new")

    def test_reject_malformed_missing_duplicate(self):
        for data in [b"GFX", movie([tag(0, b"")])[:-1]]:
            with self.assertRaises(ValueError):
                gp.parse_movie(data)
        data = movie([tag(59, b"\x1c\x00x"), tag(0, b"")])
        with self.assertRaises(ValueError):
            gp.transplant(data, data, 99)
        dup = movie([tag(59, b"\x1c\x00x")] * 2 + [tag(0, b"")])
        with self.assertRaises(ValueError):
            gp.transplant(dup, data, 28)

    def test_unexpected_source_rejected(self):
        with self.assertRaises(ValueError):
            gp.patch_source("class Unknown {}", 0xFFFFFF)

    def test_path_guards(self):
        with self.assertRaises(ValueError):
            gp.validate_paths(Path("C:/Program Files (x86)/Steam/steamapps/common/Rocksmith2014"), Path("output"))
        with self.assertRaises(ValueError):
            gp.validate_paths(Path("input"), Path("input/edited"))
        with self.assertRaises(ValueError):
            gp.validate_paths(Path("input"), Path("C:/src/rs7string/out"))
        with self.assertRaises(ValueError):
            gp.safe_child(Path("output"), "../input/movie.gfx")


if __name__ == "__main__":
    unittest.main()
