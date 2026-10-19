"""Focused pitch reader verification without loading or rebuilding the shared DLL."""

import struct
import pytest
import capstone

pytest.importorskip("keystone", reason="generator QA needs uv run --with keystone-engine")
from keystone import Ks, KS_ARCH_X86, KS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
import gen_publish_pitch as gp
from emu_common import needs_exe

# Reads the user's game exe (tools/game_files.py): skipped without it (CI).
pytestmark = needs_exe

CODE, DATA, STACK, STOP = 0x2000000, 0x2100000, 0x2200000, 0x200F000
DET, MAT, TRACK, INPUT, RESULT, NOTE = [DATA + i * 0x10000 for i in range(6)]
BASES = [40, 45, 50, 55, 59, 64, 35, 30]


def pack(*v):
    return struct.pack("<" + "I" * len(v), *[x & 0xFFFFFFFF for x in v])


def asm(s, addr):
    return bytes(Ks(KS_ARCH_X86, KS_MODE_32).asm(s, addr)[0])


def machine(k, n=6, played=40):
    fn = gp.FNS[k]
    stock, code, rels = gp.assemble(fn)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, 0x1000000)
    u.mem_map(CODE, 0x10000)
    u.mem_map(DATA, 0x100000)
    u.mem_map(STACK, 0x100000)
    u.mem_write(fn[1], stock)
    u.mem_write(0x125B240, pack(0))
    u.mem_write(0xF5AB08, b"\xc3")
    u.mem_write(0x4E5020, asm(f"mov eax,{played}; ret 4", 0x4E5020))
    u.mem_write(0x1199E28, struct.pack("<f", 1.0))
    u.mem_write(DET + 0x134C, struct.pack("<6h", *BASES[:6]))
    u.mem_write(DET + 0x13C8, struct.pack("<8h", *BASES))
    u.mem_write(DET + 0x13D8, pack(n))
    u.mem_write(MAT + 4, pack(DET, 0))
    u.mem_write(MAT + 0x64, pack(0))
    u.mem_write(TRACK + 0x4C19C, pack(MAT))
    # Exact raw / reverse ABI stand-ins let reader tests run without a DLL build.
    raw = CODE + 0xC000
    reverse = CODE + 0xC100
    tail = CODE + 0xC200
    table = DATA + 0xF0000
    u.mem_write(table, pack(*BASES))
    u.mem_write(
        raw,
        asm(
            f"mov eax,dword ptr [{table}+esi*4]; movsx edx,word ptr [ecx+esi*2]; add eax,edx; add eax,dword ptr [esp+4]; ret",
            raw,
        ),
    )
    u.mem_write(
        reverse,
        asm(
            f"mov eax,dword ptr [{table}+edx*4]; sub ecx,eax; movsx eax,word ptr [esi+edx*2]; sub ecx,eax; mov eax,ecx; ret",
            reverse,
        ),
    )
    u.mem_write(tail, b"\xc3")
    u.mem_write(0x873F80, bytes(u.mem_read(raw, 64)))
    # Raw has an absolute table reference, so it is safe to copy to original address.
    u.mem_write(0x874050, bytes(u.mem_read(reverse, 64)))
    code = bytearray(code)
    for off, target in rels:
        target = {gp.SYN: raw, gp.SYN + 1: tail, gp.SYN + 2: reverse}.get(target, target)
        code[off : off + 4] = struct.pack("<I", (target - (CODE + off + 4)) & 0xFFFFFFFF)
    u.mem_write(CODE, bytes(code))
    return u, stock, bytes(code)


@pytest.mark.parametrize("n", [6, 7, 8])
def test_note_list_capacity_open_scan(n):
    u, stock, code = machine(0, n)
    notes = BASES[:n]
    u.mem_write(INPUT, pack(*notes))
    sp = STACK + 0x80000
    u.mem_write(sp, pack(STOP, MAT, INPUT, n, 0, 0))
    u.reg_write(UC_X86_REG_ESP, sp)
    # Stop just after duplicate filtering and open-MIDI scan, before candidate vector allocation.
    marker = bytes.fromhex("31c9 31c0 83cfff")
    offset = code.index(marker)
    u.emu_start(CODE, CODE + offset, count=10000)
    bp = u.reg_read(UC_X86_REG_EBP)
    assert list(struct.unpack("<" + str(n) + "i", u.mem_read(bp - 0xAA0, n * 4))) == notes
    assert bytes(u.mem_read(bp - 0x10, n)) == b"\0" * n
    assert u.reg_read(UC_X86_REG_ESP) == bp - 0xB00 - 12


def analyze(k, n, s, f, played, native=True):
    u, stock, code = machine(k, n, played)
    u.mem_write(NOTE + 0x10, bytes([s, f]))
    sp = STACK + 0x80000
    u.mem_write(sp, pack(STOP, TRACK, RESULT, NOTE))
    u.reg_write(UC_X86_REG_ESP, sp)
    u.emu_start(CODE if native else gp.FNS[k][1], STOP, count=10000)
    return bytes(u.mem_read(RESULT, 12)), u.reg_read(UC_X86_REG_ESP), u.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("s", range(6))
@pytest.mark.parametrize("delta", [-10, -2, 0, 1, 12])
def test_note_analyzer_six_equivalence(s, delta):
    assert analyze(1, 6, s, 3, BASES[s] + 3 + delta) == analyze(1, 6, s, 3, BASES[s] + 3 + delta, False)


@pytest.mark.parametrize("n,s", [(7, 6), (8, 6), (8, 7)])
def test_note_analyzer_extended_exact_and_reverse(n, s):
    exact = analyze(1, n, s, 4, BASES[s] + 4)[0]
    near = analyze(1, n, s, 4, BASES[s] + 5)[0]
    assert struct.unpack("<I", exact[:4])[0] == 0
    assert struct.unpack("<I", near[:4])[0] == 1
    assert near[4:6] == bytes([5, s])


def test_generated_header_current():
    assert gp.OUT.read_text(encoding="utf-8") == gp.render()


@pytest.mark.parametrize("fn", gp.FNS)
def test_x87_instruction_sequence_and_internal_targets(fn):
    stock, code, rels = gp.assemble(fn)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    a = list(md.disasm(stock, fn[1]))
    b = list(md.disasm(code, 0))
    fp = lambda rows: [(i.mnemonic, i.op_str) for i in rows if i.mnemonic.startswith("f")]
    assert fp(a) == fp(b)
    starts = {i.address for i in b}
    for i in b:
        if i.group(capstone.CS_GRP_JUMP) and i.operands[0].type == capstone.x86.X86_OP_IMM:
            assert i.operands[0].imm in starts


def test_array_regions_and_harmonics_preserved():
    _, code, _ = gp.assemble(gp.FNS[0])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    dis = list(md.disasm(code, 0))
    assert any(i.mnemonic == "lea" and i.disp == -0x5C for i in dis)
    assert (
        sum(
            i.mnemonic == "mov" and i.op_str.startswith("eax, dword ptr [esi +") and i.disp in (4, 8, 12, 16, 20)
            for i in dis
        )
        >= 5
    )
    for base in (-0xAA0, -0xAC0, -0xAE0):
        assert base >= -0xB00 and base + 32 <= -0xA70
    regions = [set(range(base, base + 32)) for base in (-0xAA0, -0xAC0, -0xAE0)]
    assert not (regions[0] & regions[1] or regions[1] & regions[2] or regions[0] & regions[2])
    _, chord, _ = gp.assemble(gp.FNS[2])
    ch = list(md.disasm(chord, 0))
    assert any(i.mnemonic == "sub" and i.op_str == "esp, 0xb4" for i in ch)
    assert any(i.mnemonic == "cmp" and i.op_str == "eax, 0x20" for i in ch)


def chord(n, native, hit_shift=None):
    u, stock, code = machine(2, n)
    tpl = NOTE
    u.mem_write(tpl + 0x10, pack(*BASES[:6]))
    u.mem_write(tpl + 0x58, pack(*BASES))
    u.mem_write(0x552C60, asm("mov eax,6; ret", 0x552C60))
    extra = BASES[6:n] + [-1] * (8 - n)
    u.mem_write(
        CODE + 0xC200,
        asm(f"mov dword ptr [ebp-0x7c],{extra[0]}; mov dword ptr [ebp-0x78],{extra[1]}; ret", CODE + 0xC200),
    )
    u.mem_write(0x4E6A70, asm("xor eax,eax; ret 16", 0x4E6A70))
    seen = []

    def call(uc, addr, size, _):
        sp = uc.reg_read(UC_X86_REG_ESP)
        arr = struct.unpack("<I", uc.mem_read(sp + 4, 4))[0]
        values = struct.unpack("<" + str(8 if native else 6) + "i", uc.mem_read(arr, (8 if native else 6) * 4))
        shift = uc.reg_read(UC_X86_REG_EDI)
        shift = shift if shift < 0x80000000 else shift - 0x100000000
        seen.append((shift, values))
        if shift == hit_shift:
            uc.mem_write(addr, b"\xb0\x01\xc2\x10\x00")

    u.hook_add(UC_HOOK_CODE, call, begin=0x4E6A70, end=0x4E6A70)
    sp = STACK + 0x80000
    u.mem_write(sp, pack(STOP, TRACK, RESULT))
    u.reg_write(UC_X86_REG_ESP, sp)
    u.reg_write(UC_X86_REG_ECX, tpl)
    u.emu_start(CODE if native else gp.FNS[2][1], STOP, count=10000)
    return bytes(u.mem_read(RESULT, 12)), seen, u.reg_read(UC_X86_REG_ESP)


def test_chord_six_equivalence():
    a, sa, spa = chord(6, False)
    b, sb, spb = chord(6, True)
    assert a == b and spa == spb
    assert [(s, v[:6]) for s, v in sb] == sa
    assert all(v[6:] == (-1, -1) for s, v in sb)


@pytest.mark.parametrize("n", [7, 8])
def test_chord_extended_arrays(n):
    result, seen, sp = chord(n, True)
    assert len(seen) == 5
    for shift, values in seen:
        assert values[:n] == tuple(b + shift for b in BASES[:n])
        assert values[n:] == (-1,) * (8 - n)
    assert struct.unpack("<I", result[:4])[0] == 3


def full_note_list(n, native=True, missing=(), duplicates=False):
    """Exercise complete game x87 scoring with deterministic heap/sort/CRT dependencies."""
    u, stock, code = machine(0, n)
    u.mem_map(0x2300000, 0x100000)
    owner, history, frame = DATA + 0x60000, DATA + 0x61000, DATA + 0x62000
    u.mem_write(MAT, pack(owner))
    u.mem_write(owner + 4, pack(history))
    # history: +DB8 frames, +DBC index, +DC0 capacity, +DC8 frame count (4E702C requires > 0).
    u.mem_write(history + 0xDB8, pack(frame, 0, 1, 1, 1))
    notes = [b + 4 for b in BASES[:n]]
    targets = notes[:]
    if duplicates:
        targets[-1] = targets[0]
    bins = [(v, 1.0) for s, v in enumerate(notes) if s not in missing]
    u.mem_write(frame + 0x700, struct.pack("<f", 1.0))
    u.mem_write(frame + 0xBC, pack(len(bins)))
    u.mem_write(frame + 0x1C, b"".join(struct.pack("<if", v, s) for v, s in bins))
    u.mem_write(INPUT, pack(*targets))
    # Read only these named numeric constants, never bulk-read rdata or key material.
    constants = [(0x1224418, 8), (0x1224390, 8), (0x1224870, 8), (0x1199DD4, 4), (0x1224388, 8)]
    with gp.fc.EXE.open("rb") as f:
        for va, length in constants:
            f.seek(va - 0x400000)
            u.mem_write(va, f.read(length))
    for v in notes:
        u.mem_write(0x119A398 + (v - 22) * 24, struct.pack("<6i", v, -1, -1, -1, -1, -1))
    u.mem_write(0x1360926, b"\1")  # deterministic free: game's allocator ownership branch
    u.mem_write(0x8FB9C0, b"\xc3")
    u.mem_write(0x4E3EE0, b"\xc3")
    u.mem_write(0x4E86B0, b"\xc3")
    u.mem_write(0x4E45A0, b"\xc3")
    memcpy = CODE + 0xD000
    u.mem_write(memcpy, b"\xc3")
    u.mem_write(0x237AEC8, pack(memcpy))
    u.mem_write(0x10B75E8, pack(memcpy))
    allocations = []

    def malloc(uc, a, s, _):
        dest = DATA + 0x70000 + len(allocations) * 0x10000
        allocations.append(dest)
        uc.reg_write(UC_X86_REG_EAX, dest)

    def copy(uc, a, s, _):
        sp = uc.reg_read(UC_X86_REG_ESP)
        dest, src, size = struct.unpack("<III", uc.mem_read(sp + 4, 12))
        uc.mem_write(dest, bytes(uc.mem_read(src, size)))
        uc.reg_write(UC_X86_REG_EAX, dest)

    def sort(uc, a, s, _):
        sp = uc.reg_read(UC_X86_REG_ESP)
        first, last = struct.unpack("<II", uc.mem_read(sp + 4, 8))
        rows = [bytes(uc.mem_read(p, 12)) for p in range(first, last, 12)]
        rows.sort(key=lambda r: struct.unpack("<f", r[4:8])[0], reverse=True)
        uc.mem_write(first, b"".join(rows))

    u.hook_add(UC_HOOK_CODE, malloc, begin=0x8FB9C0, end=0x8FB9C0)
    u.hook_add(UC_HOOK_CODE, copy, begin=memcpy, end=memcpy)
    u.hook_add(UC_HOOK_CODE, sort, begin=0x4E3EE0, end=0x4E3EE0)
    trace = []
    u.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: trace.append(a))
    sp = STACK + 0x80000
    u.mem_write(sp, pack(STOP, MAT, INPUT, n, 0, 1))
    u.reg_write(UC_X86_REG_ESP, sp)
    try:
        u.emu_start(CODE if native else gp.FNS[0][1], STOP, count=200000)
    except Exception as exc:
        raise AssertionError(
            f"full matcher EIP={u.reg_read(UC_X86_REG_EIP):x}, allocations={len(allocations)}, ESP={u.reg_read(UC_X86_REG_ESP):x}, trace={[hex(a) for a in trace[-10:]]}"
        ) from exc
    assert u.reg_read(UC_X86_REG_EIP) == STOP
    # Entry's EBP=sp-4 remains readable after return; production stores per-string scores here.
    bp = sp - 4
    scores = struct.unpack("<" + str(n) + "i", u.mem_read(bp - (0xAC0 if native else 0x2C), n * 4))
    return u.reg_read(UC_X86_REG_EAX) & 255, u.reg_read(UC_X86_REG_ESP), u.reg_read(UC_X86_REG_FPSW), scores


@pytest.mark.parametrize("missing", [(), (0,), (1, 3), (0, 1, 2, 3, 4, 5)])
@pytest.mark.parametrize("duplicates", [False, True])
def test_note_list_full_six_x87_equivalence(missing, duplicates):
    assert full_note_list(6, True, missing, duplicates) == full_note_list(6, False, missing, duplicates)


@pytest.mark.parametrize("n", [7, 8])
def test_note_list_full_extended_scoring(n):
    hit, sp, fpsw, scores = full_note_list(n)
    assert hit == 1 and all(v > 0 for v in scores)
    assert sp == STACK + 0x80000 + 24
    miss = full_note_list(n, missing=tuple(range(n)))
    assert miss[0] == 0 and all(v <= 0 for v in miss[3])


@pytest.mark.parametrize("n", [7, 8])
def test_note_list_extended_string_alone_missing_fails(n):
    # Only the added string's peak is absent: slots 6/7 must take part in the hit decision.
    hit, _, _, scores = full_note_list(n, missing=(n - 1,))
    assert hit == 0 and scores[n - 1] <= 0 and all(v > 0 for v in scores[: n - 1])
