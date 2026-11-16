"""Plain PSARC blocks distinguish raw bytes by their expected length."""

import struct
import zlib

import psarc_plain
import pytest


def archive_with_block(path, raw, expected):
    name = b"payload"
    toc_length = 32 + 60 + 4
    header = struct.pack(">4sI4sIIIII", b"PSAR", 0x10004, b"zlib", toc_length, 30, 2, 65536, 0)

    def entry(index, length, offset):
        return bytes(16) + struct.pack(">I", index) + length.to_bytes(5, "big") + offset.to_bytes(5, "big")

    path.write_bytes(
        header
        + entry(0, len(name), toc_length)
        + entry(1, expected, toc_length + len(name))
        + struct.pack(">HH", len(name), len(raw))
        + name
        + raw
    )


def test_raw_partial_block_that_is_a_zlib_stream_is_not_decompressed(tmp_path):
    path = tmp_path / "plain.psarc"
    payload = zlib.compress(b"a" * 100)
    psarc_plain.write(path, {"payload": payload}, ["payload"])
    assert psarc_plain.read(path)[0] == {"payload": payload}


@pytest.mark.parametrize(
    "raw,expected",
    [
        (b"bad zlib", 100),
        (zlib.compress(b"abc"), 100),
        (zlib.compress(b"x" * 100000), 100),
        (zlib.compress(b"abc")[:-1], 100),
        (zlib.compress(b"abc") + b"extra", 3),
    ],
)
def test_invalid_compressed_blocks_are_rejected(tmp_path, raw, expected):
    path = tmp_path / "bad.psarc"
    archive_with_block(path, raw, expected)
    with pytest.raises(ValueError, match="block"):
        psarc_plain.read(path)


def test_raw_truncated_block_is_rejected(tmp_path):
    path = tmp_path / "short.psarc"
    archive_with_block(path, b"raw", 3)
    path.write_bytes(path.read_bytes()[:-1])
    with pytest.raises(ValueError, match="block"):
        psarc_plain.read(path)


def test_compressed_and_full_raw_blocks_round_trip(tmp_path):
    import random

    path = tmp_path / "mixed.psarc"
    files = {"mixed": b"x" * 65536 + random.Random(7).randbytes(65536) + b"tail", "empty": b""}
    psarc_plain.write(path, files, list(files))
    assert psarc_plain.read(path)[0] == files
