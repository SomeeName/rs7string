"""tools/cmp_functions.py: per-function comparison of two builds by symbol name, not by address."""

from __future__ import annotations

import struct
from pathlib import Path

import cmp_functions as c

BASE = 0x5E000000


class FakeImage:
    """Just what decode() / data_tokens() use: read, inside, name."""

    base = BASE
    size = 0x100000

    def __init__(self, blobs: dict[int, bytes], names: dict[int, str]):
        self.blobs = blobs
        self.vas = sorted(names)
        self.at = names

    def inside(self, va: int) -> bool:
        return self.base <= va < self.base + self.size

    code_ranges = [(BASE + 0x1000, BASE + 0x8000)]
    name = c.Image.name
    is_code = c.Image.is_code

    def read(self, va: int, n: int) -> bytes:
        for start, blob in self.blobs.items():
            if start <= va < start + len(blob):
                return blob[va - start : va - start + n]
        return bytes(n)


def func(at: int, callee: int, data: int, game: int = 0x873F80) -> bytes:
    code = b"\x68" + struct.pack("<I", game)  # push <game address>
    code += b"\xe8" + struct.pack("<i", callee - (at + len(code) + 5))  # call <callee>
    code += b"\xa1" + struct.pack("<I", data)  # mov eax, [<data>]
    code += b"\x74\x01\x90"  # jz +1 ; nop
    code += b"\xc3"  # ret
    return code + b"\xcc" * 7  # int3 padding


def image(at: int, callee_at: int, data_at: int, **kw) -> tuple[FakeImage, int, int]:
    code = func(at, callee_at, data_at, **kw)
    names = {at: "?F@@YAXXZ", callee_at: "?Callee@@YAXXZ", data_at: "?g_data@@3HA"}
    return FakeImage({at: code}, names), at, at + len(code)


def test_the_same_function_at_another_address_compares_equal():
    a, sa, ea = image(BASE + 0x1000, BASE + 0x22000, BASE + 0x9000)
    b, sb, eb = image(BASE + 0x3000, BASE + 0x7F00, BASE + 0xA040)
    ia, ib = c.decode(a, sa, ea), c.decode(b, sb, eb)
    assert ia == ib
    assert ia[0] == "push 0x873f80"  # game addresses stay literal
    assert ia[1] == "call <?Callee@@YAXXZ>"
    assert ia[2] == "mov eax, dword ptr [<?g_data@@3HA>]"
    assert ia[3].startswith("je L")  # a branch inside the function is an instruction position
    assert ia[-1] == "ret"  # the int3 padding is dropped


def test_a_changed_callee_game_address_or_instruction_is_detected():
    a, sa, ea = image(BASE + 0x1000, BASE + 0x22000, BASE + 0x9000)
    ia = c.decode(a, sa, ea)
    other_game, s, e = image(BASE + 0x1000, BASE + 0x22000, BASE + 0x9000, game=0x873F81)
    assert c.decode(other_game, s, e) != ia
    # the same code calling a different symbol
    b = FakeImage(
        {BASE + 0x1000: func(BASE + 0x1000, BASE + 0x22000, BASE + 0x9000)},
        {BASE + 0x1000: "?F@@YAXXZ", BASE + 0x22000: "?Other@@YAXXZ", BASE + 0x9000: "?g_data@@3HA"},
    )
    assert c.decode(b, BASE + 0x1000, BASE + 0x1000 + 24) != ia
    changed = bytearray(func(BASE + 0x1000, BASE + 0x22000, BASE + 0x9000))
    changed[-8] = 0x75  # jz -> jnz
    d = FakeImage({BASE + 0x1000: bytes(changed)}, a.at)
    assert c.decode(d, sa, ea) != ia


def test_a_jump_table_is_followed_and_compared_as_names_inside_the_function():
    def build(at: int) -> tuple[FakeImage, int, int]:
        table = at + 0x20
        code = bytearray()
        code += b"\xff\x24\x85" + struct.pack("<I", table)  # jmp [eax*4 + table]
        code += b"\xcc" * (0x20 - len(code))
        code += struct.pack("<II", at + 0x30, at + 0x32)  # table: two case labels
        code += b"\xcc" * (0x30 - len(code))
        code += b"\x31\xc0\xc3"  # case 0: xor eax,eax; ret
        code += b"\x40\xc3"  # case 1: inc eax; ret
        return FakeImage({at: bytes(code)}, {at: "?Sw@@YAXXZ"}), at, at + len(code)

    a, sa, ea = build(BASE + 0x1000)
    b, sb, eb = build(BASE + 0x5000)
    ia = c.decode(a, sa, ea)
    assert ia == c.decode(b, sb, eb)
    assert ia.count("dd L5") == 0
    entries = [x for x in ia if x.startswith("dd L")]
    assert len(entries) == 2 and entries[0] != entries[1]  # both case labels are instruction positions


def test_data_symbols_become_names_and_the_build_time_is_masked():
    a = FakeImage({BASE + 0x100: struct.pack("<II", BASE + 0x2200, 7)}, {BASE + 0x2200: "?x@@3HA"})
    b = FakeImage({BASE + 0x900: struct.pack("<II", BASE + 0x6640, 7)}, {BASE + 0x6640: "?x@@3HA"})
    assert c.data_tokens(a, BASE + 0x100, BASE + 0x108) == c.data_tokens(b, BASE + 0x900, BASE + 0x908)
    t1 = FakeImage({BASE: b"12:34:56\x00"}, {})
    t2 = FakeImage({BASE: b"01:02:03\x00"}, {})
    assert c.data_tokens(t1, BASE, BASE + 9) == c.data_tokens(t2, BASE, BASE + 9)
    d1 = FakeImage({BASE: b"Oct 15 2026\x00"}, {})
    assert c.data_tokens(d1, BASE, BASE + 12) == ["<__DATE__/__TIME__>"]


def test_parse_map_reads_publics_statics_and_section_classes(tmp_path: Path):
    m = tmp_path / "x.map"
    m.write_text(
        " Start         Length     Name                   Class\n"
        " 0001:00000000 00017ac0H .text$di                CODE\n"
        " 0002:00000000 000002f0H .idata$5                DATA\n"
        "\n"
        "  Address         Publics by Value              Rva+Base       Lib:Object\n"
        "\n"
        " 0001:00000010       ?Foo@@YAXXZ                5e001010 f   a.obj\n"
        " 0001:00000020       ?Inl@@YAXXZ                5e001020 f i b.obj\n"
        " 0002:00000030       ?g@@3HA                    5e002030     a.obj\n"
        "\n"
        " Static symbols\n"
        "\n"
        " 0001:00000040       ?Stat@@YAXXZ               5e001040 f   a.obj\n"
    )
    syms, classes = c.parse_map(m)
    assert classes == {1: "CODE", 2: "DATA"}
    canon = c.Names().canon  # names are stored normalised (demangled where dbghelp can)
    assert [(s.name, s.func, s.va) for s in syms] == [
        (canon("?Foo@@YAXXZ"), True, 0x5E001010),
        (canon("?Inl@@YAXXZ"), True, 0x5E001020),
        (canon("?g@@3HA"), False, 0x5E002030),
        (canon("?Stat@@YAXXZ"), True, 0x5E001040),
    ]


def test_the_line_number_argument_of_the_test_fail_call_is_masked():
    def build(at: int, push_line: bytes) -> tuple[FakeImage, int, int]:
        fail = at + 0x800
        code = b"\x68" + struct.pack("<I", BASE + 0x9000)  # push <expr string>
        code += push_line  # push <__LINE__>
        code += b"\x68" + struct.pack("<I", BASE + 0x9010)  # push <__FILE__>
        code += b"\xe8" + struct.pack("<i", fail - (at + len(code) + 5))  # call rs7test::Fail
        code += b"\xc3"
        names = {
            at: "?T@@YAXXZ",
            fail: c.Names().canon("?Fail@rs7test@@YAXPBDH0@Z"),
            BASE + 0x9000: "$SG1",
            BASE + 0x9010: "$SG2",
        }
        return FakeImage({at: code}, names), at, at + len(code)

    a, sa, ea = build(BASE + 0x1000, b"\x6a\x09")  # push 9
    b, sb, eb = build(BASE + 0x1000, b"\x68" + struct.pack("<I", 300))  # push 300 (a different encoding)
    assert c.decode(a, sa, ea)[1] == "push <__LINE__>"
    assert c.decode(a, sa, ea) == c.decode(b, sb, eb)
