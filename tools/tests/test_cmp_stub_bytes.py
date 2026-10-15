"""tools/cmp_stub_bytes.py: instruction comparison that ignores placement but not code."""

from __future__ import annotations

import struct

import cmp_stub_bytes as c

IMAGE = range(0x5E000000, 0x5E100000)


def _fn(base: int, callee: int, data: int, game: int = 0x873F80) -> bytes:
    code = bytearray()
    code += b"\x68" + struct.pack("<I", game)  # push <game address>
    code += b"\xe8" + struct.pack("<i", callee - (base + len(code) + 5))  # call <other function>
    code += b"\xa1" + struct.pack("<I", data)  # mov eax, [<our data>]
    code += b"\x74\x01\x90"  # jz +1 ; nop (label inside)
    code += b"\xc3"  # ret
    return bytes(code)


def test_same_code_at_another_address_compares_equal():
    a = c.normalize(_fn(0x5E001000, 0x5E002000, 0x5E010000), 0x5E001000, IMAGE)
    b = c.normalize(_fn(0x5E003000, 0x5E007F00, 0x5E020040), 0x5E003000, IMAGE)
    assert a == b
    assert a[0] == "push 0x873f80"  # game addresses stay literal
    assert a[1] == "call IMG"
    assert a[2] == "mov eax, dword ptr [IMG]"
    assert a[3].startswith("je L+0x")  # intra-function branch becomes an offset


def test_a_changed_game_address_or_instruction_is_detected():
    a = c.normalize(_fn(0x5E001000, 0x5E002000, 0x5E010000), 0x5E001000, IMAGE)
    other_game = c.normalize(_fn(0x5E001000, 0x5E002000, 0x5E010000, game=0x873F81), 0x5E001000, IMAGE)
    assert a != other_game
    changed = bytearray(_fn(0x5E001000, 0x5E002000, 0x5E010000))
    changed[-4] = 0x75  # jz -> jnz
    assert a != c.normalize(bytes(changed), 0x5E001000, IMAGE)


def test_function_ends_at_ret_after_the_last_branch_target():
    code = _fn(0x5E001000, 0x5E002000, 0x5E010000) + b"\xcc\xcc\x90\x90"
    assert c.normalize(code, 0x5E001000, IMAGE)[-1] == "ret"
    assert len(c.normalize(code, 0x5E001000, IMAGE)) == 6
