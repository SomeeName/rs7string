"""Native capacity-8 next-note grid (tools/gen_nextgrid.py, core/nextgrid.h): the patched game code against the stock
code in Unicorn, 6 strings. Two machines run the same data: "stock" (the unpatched exe) and "native" (NGB* bytes
sites + NG1 / NGP / NG9 from the DLL; the stage-1 R7b / N9 pair was removed in 1.0). The grid
functions Init / Set / Prune run directly, then the whole Rnd_NeckTargets_Update 0x7E4870 runs with a fake
Register (it feeds Set) and a fake Spawn (records its arguments): the spawned targets must agree.
"""

from __future__ import annotations

import gc
import json
import random
import struct
import sys
from functools import lru_cache
from pathlib import Path

import capstone
import pefile
import pytest
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import gen_fncopy  # noqa: E402
import gen_nextgrid as ng  # noqa: E402
import sitecheck  # noqa: E402
import sitetable  # noqa: E402

REPO = TOOLS.parent
DLL = REPO / "bin" / "Release" / "dinput8.dll"
SITES = REPO / "sites.json"
pytestmark = pytest.mark.skipif(
    not gen_fncopy.EXE.exists() or not DLL.exists(),
    reason="needs original/Rocksmith2014.unpacked.exe and the built DLL",
)

GRID, STACK, RET = 0x10001000, 0x12000000, 0x13000000
HEAP, FAKE = 0x14000000, 0x15000000
NODES, SESS, NOTE_BASE = HEAP, HEAP + 0x1000, HEAP + 0x4000
FAKE_PTR = (
    HEAP + 0xF000
)  # data cell: where the REGISTER trampoline jumps (code is never rewritten: Unicorn keeps stale TBs)
STACK_SIZE = 0x20000
TOP = STACK + STACK_SIZE - 0x1000  # [esp] at the entry of Update
SET, INIT, PRUNE, REGISTER, SPAWN, UPDATE, TREE_INSERT = (
    0x7AA140,
    0x7AA0B0,
    0x7AA1B0,
    0x7A5D50,
    0x7A56F0,
    0x7E4870,
    0x7AA260,
)
TREE_ERASE = 0x7D06C0
ROW, CELLS = 0x68, 26


def f2u(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def note(k: int) -> int:
    return NOTE_BASE + 0x100 * k


@lru_cache(maxsize=1)
def sections():
    pe = pefile.PE(str(gen_fncopy.EXE), fast_load=True)
    out = []
    for s in pe.sections:
        if s.Name.rstrip(b"\0") in (b".text", b".rdata", b".data"):
            out.append((pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress, bytes(s.get_data()[: s.Misc_VirtualSize])))
    return out


@lru_cache(maxsize=1)
def game_text():
    return gen_fncopy.load_text()


@lru_cache(maxsize=1)
def site_list():
    return sitetable.load(SITES)[1]


class Machine:
    def __init__(self, mode: str):
        assert mode in ("stock", "native")
        gc.collect()  # each Uc commits tens of MB: free the previous ones first
        self.mode = mode
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for va, data in sections():
            uc.mem_map(va, (len(data) + 0xFFF) & ~0xFFF, UC_PROT_ALL)
            uc.mem_write(va, data)
        for a, n in ((GRID - 0x1000, 0x20000), (STACK, STACK_SIZE), (RET, 0x1000), (HEAP, 0x10000), (FAKE, 0x100000)):
            uc.mem_map(a, n, UC_PROT_ALL)
        uc.mem_write(RET, b"\xf4")
        uc.mem_write(0xF5AB08, b"\xc3")  # __security_check_cookie
        uc.mem_write(TREE_INSERT, b"\xc2\x0c\x00")  # std::set insert (allocates): the tree is built by the test
        uc.mem_write(TREE_ERASE, b"\xc2\x04\x00")  # the tree release (thiscall, 1 arg; recursive): recorded
        uc.hook_add(UC_HOOK_CODE, self._memset, begin=0xF5B0C0, end=0xF5B0C0)  # the CRT memset (VMProtect-wrapped)
        self.exports = {}
        self.fake_n = 0
        uc.mem_write(REGISTER, b"\xff\x25" + struct.pack("<I", FAKE_PTR))  # Register: jmp [FAKE_PTR]
        if mode != "stock":
            self._load_dll()
            self._patch(mode)

    @staticmethod
    def _memset(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret, dst, val, n = struct.unpack("<4I", bytes(uc.mem_read(sp, 16)))
        uc.mem_write(dst, bytes([val & 0xFF]) * n)
        uc.reg_write(UC_X86_REG_EAX, dst)
        uc.reg_write(UC_X86_REG_ESP, sp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _load_dll(self):
        pe = pefile.PE(str(DLL))
        base = pe.OPTIONAL_HEADER.ImageBase
        self.uc.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF, UC_PROT_ALL)
        self.uc.mem_write(base, pe.header)
        for s in pe.sections:
            self.uc.mem_write(base + s.VirtualAddress, s.get_data())
        self.exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
        self.uc.mem_write(self.exports["Rs7_NeckLog"], b"\xc3")
        self.uc.mem_write(self.exports["g_rs7_fn_spawn"], struct.pack("<I", SPAWN))
        self.uc.mem_write(self.exports["g_rs7_fn_adv3"], struct.pack("<I", 0x7AA880))

    def _patch(self, mode: str):
        text, base = game_text()
        for s in site_list():
            if mode == "native" and s.nextgrid != "native":
                continue
            ok, msg, va, target = sitecheck.check_site(text, base, s)
            assert ok, f"{s.id}: {msg}"
            if s.kind == "bytes":
                self.uc.mem_write(va, s.write)
                continue
            stub = self.exports[f"Rs7_Stub_{s.id}"]
            self.uc.mem_write(va, b"\xe9" + struct.pack("<i", stub - (va + 5)) + b"\x90" * (s.length - 5))
            self.uc.mem_write(self.exports[f"g_rs7_resume_{s.id}"], struct.pack("<I", va + s.length))
            if f"g_rs7_target_{s.id}" in self.exports:
                self.uc.mem_write(self.exports[f"g_rs7_target_{s.id}"], struct.pack("<I", target))

    def read(self, va, n):
        return bytes(self.uc.mem_read(va, n))

    def write(self, va, data):
        self.uc.mem_write(va, data)

    def reg(self, r):
        return self.uc.reg_read(r)

    def call(self, entry, args=(), esi=0, edi=0, ecx=0, eax=0, limit=2_000_000):
        """[esp] = RET, then the stack arguments."""
        sp = STACK + 0x8000
        self.write(sp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a) for a in args))
        for r, v in (
            (UC_X86_REG_ESP, sp),
            (UC_X86_REG_ESI, esi),
            (UC_X86_REG_EDI, edi),
            (UC_X86_REG_ECX, ecx),
            (UC_X86_REG_EAX, eax),
        ):
            self.uc.reg_write(r, v)
        self.uc.emu_start(entry, RET, count=limit)

    # --- the grid functions ----------------------------------------------------------------------------------
    def init(self, string_count: int, frets: int, fill=0xAA):
        self.write(GRID - 0x100, bytes([fill]) * 0x1400)
        self.call(INIT, (frets,), esi=GRID, eax=string_count)

    def set_cell(self, s, f, nt, t):
        """Rnd_NextNoteGrid_Set from 0x7AA163 (right after the tree insert, `fld [ebp+14h]`) to its `ret 10h`."""
        ebp = STACK + 0x10000
        self.write(ebp - 8, struct.pack("<IIIIIIII", 0x2222, 0x7777, 0x1111, RET, s, f, nt, f2u(t)))
        for r, v in (
            (UC_X86_REG_EBP, ebp),
            (UC_X86_REG_ESP, ebp - 8),
            (UC_X86_REG_ESI, GRID),
            (UC_X86_REG_EDI, ebp - 4),
            (UC_X86_REG_EBX, 0x3333),
        ):
            self.uc.reg_write(r, v)
        self.uc.emu_start(0x7AA163, RET, count=10000)
        assert self.reg(UC_X86_REG_ESP) == ebp + 0x18 and self.reg(UC_X86_REG_EBP) == 0x1111

    def prune(self):
        self.call(PRUNE, edi=GRID)

    def set_tree(self, times):
        """A right-leaning chain of set nodes under the header node G+0D04h (no times: the empty set Init builds)."""
        if not times:
            return
        head = GRID + 0xD04
        nodes = [NODES + i * 0x20 for i in range(len(times))]
        blob = bytearray()
        for i, t in enumerate(times):
            parent = head if i == 0 else nodes[i - 1]
            right = nodes[i + 1] if i + 1 < len(times) else 0
            blob += struct.pack("<IIIIf", 0, parent, 0, right, t) + bytes(12)
        self.write(NODES, bytes(blob))
        self.write(head + 4, struct.pack("<I", nodes[0]))
        self.write(head + 8, struct.pack("<I", nodes[0]))
        self.write(head + 0xC, struct.pack("<I", nodes[-1]))
        self.write(head + 0x10, struct.pack("<I", len(times)))


def time_base(mode):
    return ng.T_NATIVE if mode == "native" else ng.T_STOCK


def u32s(data):
    return struct.unpack(f"<{len(data) // 4}I", data)


def cells_equal(a: Machine, b: Machine, rows=range(6), cols=CELLS):
    """The pointer block 0..A90 and the time rows of `rows` agree between two machines (each at its own base)."""
    assert a.read(GRID, 0xA90) == b.read(GRID, 0xA90)
    for s in rows:
        ta = a.read(GRID + time_base(a.mode) + s * ROW, cols * 4)
        tb = b.read(GRID + time_base(b.mode) + s * ROW, cols * 4)
        assert ta == tb, f"time row {s}"


@pytest.fixture(scope="module")
def machines():
    return {m: Machine(m) for m in ("stock", "native")}


# --- static checks -----------------------------------------------------------------------------------------


def test_generated_sites_are_current():
    doc = json.loads(SITES.read_text(encoding="utf-8"))
    have = {s["id"]: s for s in doc["sites"] if s["id"].startswith("NG")}
    want = {s["id"]: s for s in ng.build_sites() + ng.build_stub_sites()}
    assert have == want
    inc = ng.INC.read_text(encoding="utf-8")
    assert inc.count("RS7_NGB(") == len(ng.build_sites())


def test_frame_displacements_of_update_are_all_accounted_for():
    """After the edits every ebp operand of Update outside the stub spans is a stack argument, the cookie, or one of
    the six new local / grid-base slots (ebp-1060h .. -1074h)."""
    text, base = game_text()
    patched = bytearray(text)
    for va, exp, wr, *_ in ng.all_edits(text, base):
        patched[va - base : va - base + len(wr)] = wr
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    seen = set()
    for i in md.disasm(bytes(patched[ng.UPDATE[0] - base : ng.UPDATE[1] - base + 16]), ng.UPDATE[0]):
        if i.address > ng.UPDATE[1]:
            break
        if ng.NG9_VA <= i.address < ng.NG9_VA + ng.NG9_LEN or i.address == ng.NGP_VA:
            continue
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_EBP:
                seen.add(op.mem.disp)
    assert {d for d in seen if d < 0} == {-0x1060, -0x1064, -0x1068, -0x106C, -0x1070, -0x1074, -4}
    assert {d for d in seen if d > 0} <= {8, 0xC}
    assert -0x1060 + ng.GRID_NATIVE == -4  # the grid ends at the cookie
    assert ng.FRAME_NATIVE == 0x1074 and ng.FRAME_NATIVE > 0x1000


def test_native_edits_leave_no_stock_time_constant_in_the_grid_functions():
    """Init / Prune: no 0A90h / 270h operand is left once the edits are applied, except the pointer block's clear."""
    text, base = game_text()
    edited = {va for va, *_ in ng.all_edits(text, base)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    left = []
    for a, b in ((0x7AA0B0, 0x7AA137), (0x7AA1B0, 0x7AA25D)):
        for i in md.disasm(text[a - base : b - base], a):
            for op in i.operands:
                v = (
                    op.imm
                    if op.type == capstone.x86.X86_OP_IMM
                    else op.mem.disp
                    if op.type == capstone.x86.X86_OP_MEM
                    else None
                )
                if v in (0xA90, 0x270) and not any(i.address <= e < i.address + i.size for e in edited):
                    left.append(i.address)
    assert left == [0x7AA0D5]  # `push 0A90h`: the pointer block (unchanged)


# --- Init ---------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("strings,frets", [(6, 24), (6, 25), (6, 1), (4, 21)])
def test_init_matches_stock_and_fills_all_eight_rows(machines, strings, frets):
    stock, native = machines["stock"], machines["native"]
    stock.init(strings, frets)
    native.init(strings, frets)
    assert stock.read(GRID, 0xA90) == native.read(GRID, 0xA90)
    assert stock.read(GRID + 0xD00, 0x1C) == native.read(GRID + 0xD00, 0x1C)  # counts + tree header
    cells_equal(stock, native, rows=range(strings), cols=frets)
    for s in range(8):
        row = u32s(native.read(GRID + ng.T_NATIVE + s * ROW, ROW))
        assert row[:frets] == (f2u(2000.0),) * frets, f"row {s}"
        assert row[frets:] == (0,) * (CELLS - frets), f"row {s}"
    # nothing outside the grid is touched: the 0xAA fill survives behind the 105Ch bytes and in front of the grid
    assert native.read(GRID + ng.GRID_NATIVE, 0x40) == b"\xaa" * 0x40
    assert native.read(GRID - 0x40, 0x40) == b"\xaa" * 0x40


# --- Set / Prune --------------------------------------------------------------------------------------------


def scenario(seed: int, frets: int, strings: int = 6):
    """Random next notes: (s, f, note, time) with times from a small pool (so ties and later-than happen)."""
    rnd = random.Random(seed)
    pool = [round(rnd.uniform(0.5, 9.0), 3) for _ in range(6)]
    return pool, [(rnd.randrange(strings), rnd.randrange(frets), note(k), rnd.choice(pool)) for k in range(40)]


def drive(m: Machine, frets, pool, sets, strings=6):
    m.init(strings, frets)
    m.set_tree(sorted(set(pool)))
    for s, f, nt, t in sets:
        m.set_cell(s, f, nt, t)
    m.prune()


@pytest.mark.parametrize("seed", range(6))
@pytest.mark.parametrize("frets", [5, 24, 25])
def test_init_set_prune_match_stock_at_six_strings(machines, seed, frets):
    pool, sets = scenario(seed, frets)
    for m in machines.values():
        drive(m, frets, pool, sets)
    stock = machines["stock"]
    cells_equal(stock, machines["native"], cols=frets)
    assert stock.read(GRID + 0xD00, 0x1C) == machines["native"].read(GRID + 0xD00, 0x1C)


def ptr(m, s, f):
    return struct.unpack("<I", m.read(GRID + (s * CELLS + f) * 4, 4))[0]


def test_prune_clears_cells_later_than_the_fourth_time_for_the_active_rows_only(machines):
    """With 5 distinct times begin + 3 (4.0) is the limit; rows 6/7 are outside the active count (6)."""
    native = machines["native"]
    native.init(6, 6)
    native.set_tree([1.0, 2.0, 3.0, 4.0, 5.0])
    for k, (s, f, t) in enumerate([(0, 0, 1.0), (1, 1, 4.0), (5, 2, 5.0), (6, 1, 5.0), (7, 3, 2.0), (7, 4, 4.5)]):
        native.set_cell(s, f, note(k), t)
    native.prune()
    assert ptr(native, 0, 0) and ptr(native, 1, 1)  # 1.0 and 4.0 <= limit: kept
    assert ptr(native, 5, 2) == 0  # 5.0: pruned
    assert ptr(native, 6, 1) and ptr(native, 7, 3) and ptr(native, 7, 4)


@pytest.mark.parametrize("s", [6, 7])
@pytest.mark.parametrize("state", ["empty", "earlier", "later"])
@pytest.mark.parametrize("f", [0, 12, 24])
def test_native_set_stores_strings_6_and_7_like_the_stock_rows(machines, s, f, state):
    native = machines["native"]
    native.init(6, 25)
    old = {"earlier": 1.0, "later": 3.0}.get(state)
    if old is not None:
        native.set_cell(s, f, note(99), old)
    native.set_cell(s, f, note(1), 1.5)
    t = struct.unpack("<f", native.read(GRID + ng.T_NATIVE + s * ROW + f * 4, 4))[0]
    want = {"empty": (note(1), 1.5), "later": (note(1), 1.5), "earlier": (note(99), 1.0)}[state]
    assert (ptr(native, s, f), t) == want


def test_native_set_column_past_the_fret_count_never_stores(machines):
    """Init fills fretCount columns with 2000.0; the next column stays 0.0 and no time beats it (same as stock)."""
    native = machines["native"]
    native.init(6, 25)
    native.set_cell(7, 25, note(1), 1.5)
    assert ptr(native, 7, 25) == 0


@pytest.mark.parametrize("s,f", [(8, 0), (255, 1), (0, 26), (7, 26), (7, 255)])
def test_native_set_rejects_out_of_range_cells_without_a_write(machines, s, f):
    native = machines["native"]
    native.init(6, 26)
    before = native.read(GRID - 0x100, 0x1400)
    native.set_cell(s, f, note(1), 1.5)
    assert native.read(GRID - 0x100, 0x1400) == before


def test_native_set_keeps_the_earliest_string_6_pointer(machines):
    native = machines["native"]
    native.init(6, 25)
    for k, (f, t) in enumerate([(3, 2.0), (3, 1.0), (7, 4.0), (24, 0.5)]):
        native.set_cell(6, f, note(k), t)
    row = u32s(native.read(GRID + 6 * ROW, ROW))
    assert (row[3], row[7], row[24]) == (note(1), note(2), note(3))  # fret 3: the earlier (1.0) note wins
    assert all(row[f] == 0 for f in range(25) if f not in (3, 7, 24))


# --- the whole Rnd_NeckTargets_Update ------------------------------------------------------------------------


def fake_register(sets, tree_times, at):
    """Machine code for 0x7A5D50 (`ret 8`, stack: time, grid): builds the tree under G+0D04h and feeds Set."""
    nodes = [NODES + i * 0x20 for i in range(len(tree_times))]
    blob = bytearray()
    for i, t in enumerate(tree_times):
        right = nodes[i + 1] if i + 1 < len(tree_times) else 0
        blob += struct.pack("<IIIIf", 0, 0, 0, right, t) + bytes(12)
    c = bytearray(b"\x56\x8b\x74\x24\x0c")  # push esi; mov esi,[esp+0Ch] (the grid)
    if tree_times:
        c += (
            b"\x8d\x86\x04\x0d\x00\x00" + b"\xa3" + struct.pack("<I", nodes[0] + 4)
        )  # lea eax,[esi+0D04h]; mov [node0+4],eax
        for off, val in ((0xD08, nodes[0]), (0xD0C, nodes[0]), (0xD10, nodes[-1]), (0xD14, len(tree_times))):
            c += b"\xc7\x86" + struct.pack("<II", off, val)  # mov dword [esi+off],val
    for s, f, nt, t in sets:
        c += b"\x68" + struct.pack("<I", f2u(t)) + b"\x68" + struct.pack("<I", nt)
        c += b"\x68" + struct.pack("<I", f) + b"\x68" + struct.pack("<I", s)
        c += b"\xe8" + struct.pack("<i", SET - (at + len(c) + 5))
    c += b"\x5e\xc2\x08\x00"  # pop esi; ret 8
    return bytes(c), bytes(blob)


def run_update(m: Machine, sets, tree_times, strings=6, frets=24, dt=0.016):
    """Runs 0x7E4870 once. Returns (spawn calls, final registers, the bytes above the frame, esp at entry, cookie)."""
    # fresh code address per run: Unicorn does not invalidate translated blocks of rewritten code
    at = FAKE + 0x800 * m.fake_n
    m.fake_n += 1
    code, blob = fake_register(sets, tree_times, at)
    m.write(at, code)
    m.write(FAKE_PTR, struct.pack("<I", at))
    m.write(NODES, blob)
    m.write(SPAWN, b"\xc2\x18\x00")
    for p in sorted({n for _, _, n, _ in sets}):
        k = (p - NOTE_BASE) // 0x100
        flags = bytes([0, 0]) if k % 5 == 4 else bytes([0, 1]) if k % 3 == 0 else bytes([1, 0])
        m.write(p, bytes(0x51) + flags + bytes(0x80 - 0x53))
    info, ptra, lst = SESS + 0x800, SESS + 0x810, SESS + 0x900
    m.write(SESS, bytes(0x600))
    m.write(SESS + 0x37C, struct.pack("<I", ptra))
    m.write(ptra + 0x24, struct.pack("<I", info))
    m.write(info + 0xB, bytes([strings, frets]))
    m.write(SESS + 0x41C, struct.pack("<II", lst, lst + 4))
    m.write(lst, struct.pack("<I", note(900)))
    m.write(SESS + 0x3F0, struct.pack("<ff", 0.5, 0.25))
    m.write(STACK, b"\xcd" * STACK_SIZE)
    esp = TOP
    m.write(esp, struct.pack("<IIf", RET, SESS, dt))
    for r, v in (
        (UC_X86_REG_ESP, esp),
        (UC_X86_REG_EBP, 0x2468),
        (UC_X86_REG_EBX, 0xB0B0),
        (UC_X86_REG_ESI, 0x5151),
        (UC_X86_REG_EDI, 0xD1D1),
    ):
        m.uc.reg_write(r, v)
    calls, erase = [], []
    h = m.uc.hook_add(
        UC_HOOK_CODE,
        lambda uc, a, s, u: calls.append(struct.unpack("<6I", bytes(uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 24)))),
        begin=SPAWN,
        end=SPAWN,
    )
    h2 = m.uc.hook_add(
        UC_HOOK_CODE,
        lambda uc, a, s, u: erase.append(
            (
                uc.reg_read(UC_X86_REG_ECX),
                struct.unpack("<I", bytes(uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 4)))[0],
            )
        ),
        begin=TREE_ERASE,
        end=TREE_ERASE,
    )
    try:
        m.uc.emu_start(UPDATE, RET, count=2_000_000)
    finally:
        m.uc.hook_del(h)
        m.uc.hook_del(h2)
    regs = tuple(m.reg(r) for r in (UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI))
    # [ebp-4] (ebp = esp-4) holds the /GS cookie ^ ebp: an overrun of the grid would have changed it
    cookie = struct.unpack("<I", m.read(esp - 8, 4))[0] == struct.unpack("<I", m.read(0x125B240, 4))[0] ^ (esp - 4)
    return calls, regs, m.read(esp + 4, 0x40), esp, cookie, erase


UPDATE_TIMES = [1.0, 2.0, 3.0, 4.0, 5.0]


def update_sets(rnd: random.Random, strings: int, frets: int, count=30):
    return [(rnd.randrange(strings), rnd.randrange(frets), note(k), rnd.choice(UPDATE_TIMES)) for k in range(count)]


@pytest.mark.parametrize("seed", range(5))
def test_update_spawns_the_same_targets_at_six_strings(machines, seed):
    sets = update_sets(random.Random(seed), 6, 24)
    res = {name: run_update(m, sets, UPDATE_TIMES) for name, m in machines.items()}
    stock = res["stock"]
    assert stock[0], "the scenario must spawn something"
    native = res["native"]
    assert native[0] == stock[0]
    assert native[1] == stock[1]  # esp / ebp / ebx / esi / edi restored
    assert native[2] == stock[2]  # the caller's stack above untouched
    assert native[4] is True and stock[4] is True  # the /GS cookie slot is intact
    assert res["native"][1][0] == res["native"][3] + 12  # ret 8
    # the tree release (size != 0): this = the set header, arg = its root; both moved with the grid
    ebp = res["stock"][3] - 4
    root = NODES
    assert res["stock"][5] == [(ebp - 0x1C, root)]
    assert res["native"][5] == [(ebp - 0x35C, root)]


def test_update_native_frame_is_1074h_and_probed():
    native = Machine("native")  # a fresh machine: Unicorn applies a memory hook only to blocks translated after it
    reads = []
    native.uc.hook_add(UC_HOOK_MEM_READ, lambda uc, a, addr, s, v, u: reads.append(addr))
    run_update(native, update_sets(random.Random(1), 6, 24), UPDATE_TIMES)
    ebp = TOP - 4
    assert ebp - 0x1000 in reads  # NGP's probe of the page the frame crosses
    # the grid (ebp-1060h .. ebp-4) was built by Init in place: counts and the set header are where the layout says
    assert struct.unpack("<HH", native.read(ebp - 0x1060 + 0xD00, 4)) == (6, 24)


@pytest.mark.parametrize("seed", range(3))
def test_update_string_6_targets_match_the_stock_loops_at_seven(machines, seed):
    """NG9 at an active count of 6 spawns row 6 exactly as the stock loops do it at 7 (the oracle that replaced the
    removed R7b / N9 comparison)."""
    sets = update_sets(random.Random(100 + seed), 7, 24)
    assert any(s == 6 for s, *_ in sets)
    six = run_update(machines["native"], sets, UPDATE_TIMES, strings=6)
    seven = run_update(machines["native"], sets, UPDATE_TIMES, strings=7)
    assert any(c[4] == 6 for c in six[0]), "string 6 must spawn something"
    assert sorted(c for c in six[0] if c[4] == 6) == sorted(c for c in seven[0] if c[4] == 6)
    assert six[1] == seven[1]


def test_update_string_7_cells_are_stored_but_not_spawned_while_the_count_is_six(machines):
    base = update_sets(random.Random(7), 7, 24)
    with7 = base + [(7, f, note(60 + f), 1.0 + f % 3) for f in range(1, 12)]
    a = run_update(machines["native"], base, UPDATE_TIMES)
    b = run_update(machines["native"], with7, UPDATE_TIMES)
    assert [c for c in b[0] if c[4] == 7] == []
    assert b[0] == a[0]
    assert b[1] == a[1] and b[4] == a[4]


def test_update_from_seven_strings_row_6_is_left_to_the_stock_loops(machines):
    """Active count 7: the stock loops enumerate row 6, NG9 must not spawn it a second time."""
    sets = [(6, f, note(k), 1.0) for f, k in enumerate((71, 72, 73, 75, 76), 1)]  # note k % 5 == 4: no spawn flags
    six = run_update(machines["native"], sets, UPDATE_TIMES, strings=6)
    seven = run_update(machines["native"], sets, UPDATE_TIMES, strings=7)
    assert len(six[0]) == len(seven[0]) == 5
    assert sorted(six[0]) == sorted(seven[0])  # same targets, once each: by NG9 at 6, by the stock loop at 7
